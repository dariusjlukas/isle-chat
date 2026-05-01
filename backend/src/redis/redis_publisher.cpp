#include "redis/redis_publisher.h"

#include "logging/logger.h"
#include "metrics/metrics.h"
#include "redis/redis_envelope.h"

#if BACKEND_HAS_REDIS
#include <hiredis.h>
#endif

#include <cstdlib>
#include <string>

namespace enclave_redis {

#if BACKEND_HAS_REDIS
namespace {

// Parses a "redis://host[:port][/db]" URL into host/port. Returns false on
// malformed input. Defaults: port=6379. The optional path/db part is
// ignored at this stage — we only PUBLISH on a fixed channel.
bool parse_redis_url(const std::string& url, std::string& host, int& port) {
  static const std::string kScheme = "redis://";
  if (url.compare(0, kScheme.size(), kScheme) != 0) return false;
  std::string rest = url.substr(kScheme.size());
  // Strip trailing path/db, e.g. "host:6379/0" -> "host:6379"
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

RedisPublisher::RedisPublisher(const std::string& url, const std::string& instance_id)
  : url_(url), instance_id_(instance_id), enabled_(!url.empty()) {
  if (!enabled_) {
    LOG_INFO_N("redis", nullptr, "RedisPublisher disabled (REDIS_URL is empty)");
  }
}

RedisPublisher::~RedisPublisher() {
  std::lock_guard<std::mutex> lock(mutex_);
  close_locked();
}

void RedisPublisher::close_locked() {
#if BACKEND_HAS_REDIS
  if (ctx_) {
    redisFree(ctx_);
    ctx_ = nullptr;
  }
#endif
}

bool RedisPublisher::ensure_connected_locked() {
#if BACKEND_HAS_REDIS
  if (ctx_ && !ctx_->err) return true;
  if (ctx_) {
    redisFree(ctx_);
    ctx_ = nullptr;
  }
  std::string host;
  int port = 0;
  if (!parse_redis_url(url_, host, port)) {
    LOG_ERROR_N("redis", nullptr, "RedisPublisher: malformed REDIS_URL: " + url_);
    metrics::redis_health_check_failures_total().inc();
    return false;
  }
  struct timeval tv = {2, 0};  // 2s connect timeout
  ctx_ = redisConnectWithTimeout(host.c_str(), port, tv);
  if (!ctx_ || ctx_->err) {
    std::string err = ctx_ ? ctx_->errstr : "alloc failed";
    LOG_WARN_N("redis", nullptr, "RedisPublisher: connect failed: " + err);
    if (ctx_) {
      redisFree(ctx_);
      ctx_ = nullptr;
    }
    metrics::redis_health_check_failures_total().inc();
    metrics::redis_ok().set(0);
    return false;
  }
  return true;
#else
  return false;
#endif
}

bool RedisPublisher::publish(const std::string& topic, const std::string& payload) {
  if (!enabled_) return false;
#if BACKEND_HAS_REDIS
  Envelope env;
  env.instance_id = instance_id_;
  env.topic = topic;
  env.payload = payload;
  std::string encoded = env.encode();

  std::lock_guard<std::mutex> lock(mutex_);
  if (!ensure_connected_locked()) return false;

  // PUBLISH <channel> <payload>
  redisReply* reply = static_cast<redisReply*>(
    redisCommand(ctx_, "PUBLISH %s %b", kBroadcastChannel, encoded.data(), encoded.size()));
  if (!reply || ctx_->err) {
    std::string err = ctx_ ? ctx_->errstr : "no reply";
    LOG_WARN_N("redis", nullptr, "RedisPublisher: PUBLISH failed: " + err);
    if (reply) freeReplyObject(reply);
    close_locked();
    metrics::redis_health_check_failures_total().inc();
    metrics::redis_ok().set(0);
    return false;
  }
  freeReplyObject(reply);

  metrics::inc_redis_publish(topic_kind_from(topic));
  metrics::redis_ok().set(1);
  return true;
#else
  (void)topic;
  (void)payload;
  return false;
#endif
}

}  // namespace enclave_redis
