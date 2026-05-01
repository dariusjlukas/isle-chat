#pragma once

#include <mutex>
#include <string>

// Forward-declare hiredis types so this header doesn't pull in hiredis.h
// in the no-Redis build configuration.
struct redisContext;

namespace enclave_redis {

// Synchronous PUBLISH wrapper. Thread-safe: each publish() takes a mutex
// around the redisContext. Suitable for low-rate broadcasts (we PUBLISH
// once per WS broadcast event, not per byte).
class RedisPublisher {
public:
  // Construct from a redis URL like "redis://host:port[/db]". An empty url
  // disables the publisher entirely (publish() returns false but never
  // touches hiredis or increments error counters).
  RedisPublisher(const std::string& url, const std::string& instance_id);
  ~RedisPublisher();

  RedisPublisher(const RedisPublisher&) = delete;
  RedisPublisher& operator=(const RedisPublisher&) = delete;

  // Builds an Envelope, JSON-encodes it, and publishes to kBroadcastChannel.
  // Increments enclave_redis_publish_total{topic_kind=...} on success and
  // enclave_redis_health_check_failures_total on failure. Returns true on
  // success, false on error or when the publisher is disabled.
  bool publish(const std::string& topic, const std::string& payload);

  bool is_enabled() const {
    return enabled_;
  }

private:
  // Connect or reconnect; called with mutex_ held. Returns true if ctx_ is
  // a valid connected context after the call. On failure ctx_ is freed and
  // left null.
  bool ensure_connected_locked();
  void close_locked();

  std::string url_;
  std::string instance_id_;
  bool enabled_ = false;

  std::mutex mutex_;
#if BACKEND_HAS_REDIS
  redisContext* ctx_ = nullptr;
#endif
};

}  // namespace enclave_redis
