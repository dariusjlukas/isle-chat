#include "redis/redis_subscriber.h"

#include "logging/logger.h"
#include "metrics/metrics.h"
#include "redis/redis_envelope.h"

#if BACKEND_HAS_REDIS
#include <hiredis.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

namespace enclave_redis {

#if BACKEND_HAS_REDIS
namespace {

// Mirror of redis_publisher.cpp's parser. Kept local to avoid tempting
// callers to share state across the two classes.
bool parse_redis_url(const std::string& url, std::string& host, int& port) {
  static const std::string kScheme = "redis://";
  if (url.compare(0, kScheme.size(), kScheme) != 0) return false;
  std::string rest = url.substr(kScheme.size());
  auto slash = rest.find('/');
  if (slash != std::string::npos) rest = rest.substr(0, slash);
  if (rest.empty()) return false;
  auto colon = rest.find(':');
  if (colon == std::string::npos) {
    host = rest;
    port = 6379;
    return true;
  }
  host = rest.substr(0, colon);
  if (host.empty()) return false;
  std::string port_str = rest.substr(colon + 1);
  if (port_str.empty()) return false;
  char* end = nullptr;
  long p = std::strtol(port_str.c_str(), &end, 10);
  if (end == port_str.c_str() || *end != '\0' || p <= 0 || p > 65535) return false;
  port = static_cast<int>(p);
  return true;
}

}  // namespace
#endif  // BACKEND_HAS_REDIS

RedisSubscriber::RedisSubscriber(
  const std::string& url, const std::string& instance_id, DispatchFn dispatch)
  : url_(url), instance_id_(instance_id), dispatch_(std::move(dispatch)), enabled_(!url.empty()) {
  if (!enabled_) {
    LOG_INFO_N("redis", nullptr, "RedisSubscriber disabled (REDIS_URL is empty)");
  }
}

RedisSubscriber::~RedisSubscriber() {
  stop();
}

void RedisSubscriber::start() {
  if (!enabled_) return;
  bool was = false;
  if (!started_.compare_exchange_strong(was, true)) return;
  thread_ = std::thread([this]() { run(); });
}

void RedisSubscriber::stop() {
  if (!enabled_) return;
  stop_requested_.store(true);
  if (thread_.joinable()) thread_.join();
}

void RedisSubscriber::run() {
#if BACKEND_HAS_REDIS
  // Backoff schedule: 1, 2, 4, 8, 30 seconds (cap).
  const int kBackoffSchedule[] = {1, 2, 4, 8, 30};
  size_t backoff_idx = 0;

  while (!stop_requested_.load()) {
    std::string host;
    int port = 0;
    if (!parse_redis_url(url_, host, port)) {
      LOG_ERROR_N("redis", nullptr, "RedisSubscriber: malformed REDIS_URL: " + url_);
      metrics::redis_health_check_failures_total().inc();
      metrics::redis_ok().set(0);
      // Permanent config error — sleep at the cap and retry (operator may
      // fix the env in place; we don't want to spin).
      std::this_thread::sleep_for(std::chrono::seconds(30));
      continue;
    }

    struct timeval tv = {2, 0};
    redisContext* ctx = redisConnectWithTimeout(host.c_str(), port, tv);
    if (!ctx || ctx->err) {
      std::string err = ctx ? ctx->errstr : "alloc failed";
      LOG_WARN_N("redis", nullptr, "RedisSubscriber: connect failed: " + err);
      if (ctx) redisFree(ctx);
      metrics::redis_health_check_failures_total().inc();
      metrics::redis_ok().set(0);
      int sleep_s = kBackoffSchedule[backoff_idx];
      if (backoff_idx + 1 < sizeof(kBackoffSchedule) / sizeof(kBackoffSchedule[0])) ++backoff_idx;
      // Sleep in 200ms slices so we wake quickly on stop_requested_.
      for (int i = 0; i < sleep_s * 5 && !stop_requested_.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }

    backoff_idx = 0;
    metrics::redis_ok().set(1);

    // SUBSCRIBE is fire-and-forget; we read replies in a loop.
    redisReply* sub =
      static_cast<redisReply*>(redisCommand(ctx, "SUBSCRIBE %s", kBroadcastChannel));
    if (!sub || ctx->err) {
      std::string err = ctx->err ? ctx->errstr : "no reply";
      LOG_WARN_N("redis", nullptr, "RedisSubscriber: SUBSCRIBE failed: " + err);
      if (sub) freeReplyObject(sub);
      redisFree(ctx);
      metrics::redis_health_check_failures_total().inc();
      metrics::redis_ok().set(0);
      metrics::redis_reconnect_total().inc();
      continue;
    }
    freeReplyObject(sub);
    LOG_INFO_N(
      "redis", nullptr, std::string("RedisSubscriber: subscribed to ") + kBroadcastChannel);

    // Block on getReply. Use a 1s read timeout so we can periodically
    // check stop_requested_; longer timeouts would delay shutdown.
    struct timeval read_tv = {1, 0};
    redisSetTimeout(ctx, read_tv);

    while (!stop_requested_.load()) {
      void* reply_v = nullptr;
      int rc = redisGetReply(ctx, &reply_v);
      if (rc != REDIS_OK) {
        // EAGAIN/ETIMEDOUT shows up as REDIS_ERR_IO with a specific errno.
        // We can't cleanly distinguish "timeout" from "real error" in
        // 1.2.0 without inspecting errno, so just check err and reconnect
        // on real disconnect. A timeout sets err but ctx remains usable
        // until we get an actual TCP error.
        if (ctx->err == REDIS_ERR_IO) {
          // Could be a benign read timeout (we set 1s) — try again. If
          // the socket is actually dead the next redisGetReply will fail
          // immediately with the same err and we'll keep looping. We
          // detect a true disconnect by err != 0 AND the read returning
          // immediately on retry; for simplicity we treat any non-EAGAIN
          // as fatal.
          // Heuristic: if errno is EAGAIN/EWOULDBLOCK it's a timeout.
          // hiredis stuffs the OS error into errstr in that case.
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Clear err so subsequent calls work.
            ctx->err = 0;
            ctx->errstr[0] = '\0';
            continue;
          }
        }
        std::string err = ctx->errstr[0] ? ctx->errstr : "io error";
        LOG_WARN_N("redis", nullptr, "RedisSubscriber: getReply failed: " + err);
        metrics::redis_health_check_failures_total().inc();
        break;
      }

      auto* reply = static_cast<redisReply*>(reply_v);
      // Subscription messages are arrays: ["message", <channel>, <payload>]
      if (
        reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3 && reply->element[0] &&
        reply->element[0]->type == REDIS_REPLY_STRING &&
        std::string_view(reply->element[0]->str, reply->element[0]->len) == "message" &&
        reply->element[2] && reply->element[2]->type == REDIS_REPLY_STRING) {
        std::string_view raw(reply->element[2]->str, reply->element[2]->len);
        auto env = Envelope::decode(raw);
        if (env) {
          if (is_self_echo(*env, instance_id_)) {
            metrics::redis_self_echo_dropped_total().inc();
          } else {
            metrics::inc_redis_subscribe_received(topic_kind_from(env->topic));
            try {
              auto payload_ptr = std::make_shared<std::string>(std::move(env->payload));
              dispatch_(env->topic, std::move(payload_ptr));
            } catch (const std::exception& e) {
              LOG_WARN_N(
                "redis", nullptr, std::string("RedisSubscriber: dispatch threw: ") + e.what());
            }
          }
        } else {
          LOG_WARN_N("redis", nullptr, "RedisSubscriber: malformed envelope dropped");
          metrics::redis_health_check_failures_total().inc();
        }
      }
      // Other reply types (subscribe/unsubscribe acks, pings) are ignored.
      freeReplyObject(reply);
    }

    redisFree(ctx);
    if (!stop_requested_.load()) {
      metrics::redis_reconnect_total().inc();
      metrics::redis_ok().set(0);
      // Brief sleep before reconnect attempt; the connect-failure branch
      // above will apply backoff if it can't reconnect.
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  metrics::redis_ok().set(0);
#endif
}

}  // namespace enclave_redis
