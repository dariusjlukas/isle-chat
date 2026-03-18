#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Latency histogram: 10000 buckets at 0.1ms resolution (0 - 999.9ms) + overflow
static constexpr int HISTOGRAM_BUCKETS = 10000;
static constexpr double BUCKET_RESOLUTION_MS = 0.1;

struct EndpointStats {
  std::string request_type;  // "GET", "POST", "WSConnect", etc.
  std::string name;          // "/api/users/me", "/ws send_message", etc.
  uint64_t request_count = 0;
  uint64_t failure_count = 0;
  double sum_latency_ms = 0;
  double min_latency_ms = 1e9;
  double max_latency_ms = 0;
  double sum_response_size = 0;
  std::vector<uint64_t> histogram;  // HISTOGRAM_BUCKETS entries
  uint64_t overflow_count = 0;      // Latencies >= 1000ms

  EndpointStats() : histogram(HISTOGRAM_BUCKETS, 0) {}

  void record(double latency_ms, size_t response_size, bool success) {
    request_count++;
    if (!success) failure_count++;
    sum_latency_ms += latency_ms;
    sum_response_size += response_size;
    if (latency_ms < min_latency_ms) min_latency_ms = latency_ms;
    if (latency_ms > max_latency_ms) max_latency_ms = latency_ms;

    int bucket = static_cast<int>(latency_ms / BUCKET_RESOLUTION_MS);
    if (bucket >= 0 && bucket < HISTOGRAM_BUCKETS) {
      histogram[bucket]++;
    } else {
      overflow_count++;
    }
  }

  void merge(const EndpointStats& other) {
    request_count += other.request_count;
    failure_count += other.failure_count;
    sum_latency_ms += other.sum_latency_ms;
    sum_response_size += other.sum_response_size;
    if (other.min_latency_ms < min_latency_ms) min_latency_ms = other.min_latency_ms;
    if (other.max_latency_ms > max_latency_ms) max_latency_ms = other.max_latency_ms;
    for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
      histogram[i] += other.histogram[i];
    }
    overflow_count += other.overflow_count;
  }

  double percentile(double p) const {
    uint64_t total = request_count;
    if (total == 0) return 0;
    uint64_t target = static_cast<uint64_t>(std::ceil(p / 100.0 * total));
    uint64_t cumulative = 0;
    for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
      cumulative += histogram[i];
      if (cumulative >= target) {
        return (i + 1) * BUCKET_RESOLUTION_MS;
      }
    }
    return max_latency_ms;  // In overflow range
  }

  double avg_latency() const {
    return request_count > 0 ? sum_latency_ms / request_count : 0;
  }

  double avg_response_size() const {
    return request_count > 0 ? sum_response_size / request_count : 0;
  }
};

// Key for endpoint stats: "TYPE|name"
inline std::string stats_key(const std::string& type, const std::string& name) {
  return type + "|" + name;
}

// Thread-local stats collector. Each virtual user thread owns one.
class StatsCollector {
 public:
  void record(const std::string& request_type, const std::string& name, double latency_ms,
              size_t response_size, bool success) {
    std::string key = stats_key(request_type, name);
    auto& ep = stats_[key];
    if (ep.request_type.empty()) {
      ep.request_type = request_type;
      ep.name = name;
    }
    ep.record(latency_ms, response_size, success);
  }

  // Swap out current stats and return them (for merging by coordinator)
  std::unordered_map<std::string, EndpointStats> swap() {
    std::unordered_map<std::string, EndpointStats> out;
    std::swap(out, stats_);
    return out;
  }

  const std::unordered_map<std::string, EndpointStats>& peek() const { return stats_; }

 private:
  std::unordered_map<std::string, EndpointStats> stats_;
};

// Global stats aggregator. Collects from all thread-local collectors.
class GlobalStats {
 public:
  void merge(const std::unordered_map<std::string, EndpointStats>& snapshot) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [key, ep] : snapshot) {
      auto& global_ep = stats_[key];
      if (global_ep.request_type.empty()) {
        global_ep.request_type = ep.request_type;
        global_ep.name = ep.name;
      }
      global_ep.merge(ep);
    }
  }

  // Get a copy of all stats (thread-safe)
  std::unordered_map<std::string, EndpointStats> snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return stats_;
  }

  // Compute aggregated stats across all endpoints
  EndpointStats aggregated() const {
    std::lock_guard<std::mutex> lock(mu_);
    EndpointStats agg;
    for (auto& [key, ep] : stats_) {
      agg.merge(ep);
    }
    return agg;
  }

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, EndpointStats> stats_;
};
