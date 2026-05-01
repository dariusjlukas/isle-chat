#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

// Forward-declare hiredis types so this header doesn't pull in hiredis.h
// in the no-Redis build configuration.
struct redisContext;

namespace enclave_redis {

// Background-thread Redis SUBSCRIBE consumer. Calls a user-supplied
// dispatch function for each non-self envelope. The dispatch function is
// invoked on the subscriber thread; the caller is expected to bounce
// through loop->defer() to get back onto the uSockets event loop.
class RedisSubscriber {
public:
  using DispatchFn = std::function<void(std::string topic, std::shared_ptr<std::string> payload)>;

  // Constructs but does not start the thread. Empty url -> the subscriber
  // is permanently disabled and start()/stop() are no-ops.
  RedisSubscriber(const std::string& url, const std::string& instance_id, DispatchFn dispatch);
  ~RedisSubscriber();

  RedisSubscriber(const RedisSubscriber&) = delete;
  RedisSubscriber& operator=(const RedisSubscriber&) = delete;

  // Idempotent. Spawns the worker thread on first call.
  void start();

  // Idempotent. Signals stop, joins the worker thread.
  void stop();

  bool is_enabled() const {
    return enabled_;
  }

private:
  void run();

  std::string url_;
  std::string instance_id_;
  DispatchFn dispatch_;
  bool enabled_ = false;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> started_{false};
  std::thread thread_;
};

}  // namespace enclave_redis
