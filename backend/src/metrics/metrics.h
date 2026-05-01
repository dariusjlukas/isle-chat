#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>

// Lightweight Prometheus-style metrics. Roll-your-own — no prometheus-cpp.
// All metric names use the `enclave_` prefix so they are easy to distinguish
// from system / exporter metrics when scraped.
namespace metrics {

class Counter {
public:
  void inc(uint64_t v = 1) {
    value_.fetch_add(v, std::memory_order_relaxed);
  }
  uint64_t get() const {
    return value_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<uint64_t> value_{0};
};

class Gauge {
public:
  void inc(int64_t v = 1) {
    value_.fetch_add(v, std::memory_order_relaxed);
  }
  void dec(int64_t v = 1) {
    value_.fetch_sub(v, std::memory_order_relaxed);
  }
  void set(int64_t v) {
    value_.store(v, std::memory_order_relaxed);
  }
  int64_t get() const {
    return value_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<int64_t> value_{0};
};

// Histogram with fixed bucket boundaries (in seconds).
// Sum is stored as fixed-point microseconds so we can use a single atomic
// uint64_t (avoids racy double-atomic patterns).
class Histogram {
public:
  static constexpr std::array<double, 7> kBuckets{0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0};

  void observe(double seconds);
  // Render under the given metric name. `label_str` is the inner contents of
  // the label braces (e.g. `route="/api/health"`); pass empty for unlabeled.
  void render(std::ostream& os, std::string_view name, std::string_view label_str) const;

  uint64_t count() const {
    return count_.load(std::memory_order_relaxed);
  }
  double sum_seconds() const {
    return static_cast<double>(sum_us_.load(std::memory_order_relaxed)) / 1'000'000.0;
  }
  uint64_t bucket(size_t i) const {
    return bucket_counts_[i].load(std::memory_order_relaxed);
  }

private:
  // Cumulative bucket counts (Prometheus convention). Last entry = +Inf.
  std::array<std::atomic<uint64_t>, kBuckets.size() + 1> bucket_counts_{};
  std::atomic<uint64_t> count_{0};
  std::atomic<uint64_t> sum_us_{0};
};

// Boot-time initialization. Idempotent.
void init();

// HTTP request observation. Records both a counter (with method/route/status
// labels) and a histogram (with route label only — keeps cardinality bounded).
void observe_http_request(
  std::string_view method, std::string_view route, int status, double seconds);

// WS metrics.
Gauge& ws_connected_clients();
void inc_ws_messages_received(std::string_view type);

// DB pool gauges. The pool doesn't poll; main.cpp updates these on /metrics scrape.
Gauge& db_pool_size();
Gauge& db_pool_in_use();

// --- Redis pub/sub ---------------------------------------------------------
// Counter incremented on every successful PUBLISH, labeled by topic_kind
// (the part before ':' in the topic — e.g. "channel" for "channel:abc").
void inc_redis_publish(std::string_view topic_kind);
// Counter incremented on every received non-self envelope.
void inc_redis_subscribe_received(std::string_view topic_kind);
// Counter incremented when a received envelope's instance_id matches our own.
Counter& redis_self_echo_dropped_total();
// Counter incremented on connect/PUBLISH/getReply errors.
Counter& redis_health_check_failures_total();
// Counter incremented every time the subscriber thread reconnects.
Counter& redis_reconnect_total();
// 1 if both publisher and subscriber currently believe they're connected.
Gauge& redis_ok();

// Render the full /metrics text output in Prometheus exposition format.
std::string render();

// RAII timer that observes an HTTP request on `observe(status)`. If
// observe() is never called the destructor is a no-op (caller decides when
// to record).
class RequestTimer {
public:
  RequestTimer(std::string_view method, std::string_view route)
    : method_(method), route_(route), start_(std::chrono::steady_clock::now()) {}

  void observe(int status) {
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    metrics::observe_http_request(method_, route_, status, elapsed);
  }

private:
  std::string method_;
  std::string route_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace metrics
