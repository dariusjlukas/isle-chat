#include "metrics/metrics.h"

#include <iomanip>
#include <memory>
#include <sstream>

namespace metrics {

namespace {

// --- Labeled HTTP counter family -------------------------------------------
// Map key is the rendered inner-label string, e.g.
//   `method="GET",route="/api/health",status="200"`.
// Wrapped in a mutex so render() can iterate safely while inc() races.
struct LabeledCounters {
  std::mutex mu;
  std::unordered_map<std::string, uint64_t> values;
};

LabeledCounters& http_requests_total() {
  static LabeledCounters c;
  return c;
}

// --- Labeled HTTP duration histogram family --------------------------------
// Keyed by route only — keeping label cardinality low (status/method blow up
// the bucket count for low value).
struct LabeledHistograms {
  std::mutex mu;
  // unique_ptr so reference returned to inc-side stays valid across rehash
  std::unordered_map<std::string, std::unique_ptr<Histogram>> values;
};

LabeledHistograms& http_request_duration_seconds() {
  static LabeledHistograms h;
  return h;
}

// --- WS message-type counter family ----------------------------------------
LabeledCounters& ws_messages_received_total() {
  static LabeledCounters c;
  return c;
}

// --- Singleton gauges ------------------------------------------------------
Gauge& ws_connected_clients_g() {
  static Gauge g;
  return g;
}

Gauge& db_pool_size_g() {
  static Gauge g;
  return g;
}

Gauge& db_pool_in_use_g() {
  static Gauge g;
  return g;
}

// --- Redis pub/sub families ------------------------------------------------
LabeledCounters& redis_publish_total_family() {
  static LabeledCounters c;
  return c;
}

LabeledCounters& redis_subscribe_received_total_family() {
  static LabeledCounters c;
  return c;
}

Counter& redis_self_echo_dropped_total_c() {
  static Counter c;
  return c;
}

Counter& redis_health_check_failures_total_c() {
  static Counter c;
  return c;
}

Counter& redis_reconnect_total_c() {
  static Counter c;
  return c;
}

Gauge& redis_ok_g() {
  static Gauge g;
  return g;
}

// Escape Prometheus label values per the exposition format.
// (We don't expect special chars in our labels — this is a defensive cheap
// pass.)
std::string escape_label(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

}  // namespace

// --- Histogram --------------------------------------------------------------

void Histogram::observe(double seconds) {
  count_.fetch_add(1, std::memory_order_relaxed);
  // Clamp negative durations to 0 to avoid wraparound when stored as unsigned us.
  uint64_t us = seconds > 0.0 ? static_cast<uint64_t>(seconds * 1'000'000.0) : 0;
  sum_us_.fetch_add(us, std::memory_order_relaxed);
  for (size_t i = 0; i < kBuckets.size(); ++i) {
    if (seconds <= kBuckets[i]) {
      bucket_counts_[i].fetch_add(1, std::memory_order_relaxed);
    }
  }
  bucket_counts_[kBuckets.size()].fetch_add(1, std::memory_order_relaxed);  // +Inf
}

void Histogram::render(std::ostream& os, std::string_view name, std::string_view label_str) const {
  auto label_prefix = [&](const std::string& le_clause) {
    std::string result = "{";
    if (!label_str.empty()) {
      result += label_str;
      result += ',';
    }
    result += le_clause;
    result += '}';
    return result;
  };

  for (size_t i = 0; i < kBuckets.size(); ++i) {
    std::ostringstream le;
    le << "le=\"";
    le << kBuckets[i];
    le << "\"";
    os << name << "_bucket" << label_prefix(le.str()) << " "
       << bucket_counts_[i].load(std::memory_order_relaxed) << "\n";
  }
  os << name << "_bucket" << label_prefix("le=\"+Inf\"") << " "
     << bucket_counts_[kBuckets.size()].load(std::memory_order_relaxed) << "\n";

  std::string only_label_braces;
  if (!label_str.empty()) {
    only_label_braces = "{";
    only_label_braces += label_str;
    only_label_braces += "}";
  }
  os << name << "_sum" << only_label_braces << " " << sum_seconds() << "\n";
  os << name << "_count" << only_label_braces << " " << count_.load(std::memory_order_relaxed)
     << "\n";
}

// --- Public API -------------------------------------------------------------

void init() {
  // Touch every singleton so first scrape returns predictable zero values.
  (void)http_requests_total();
  (void)http_request_duration_seconds();
  (void)ws_messages_received_total();
  (void)ws_connected_clients_g();
  (void)db_pool_size_g();
  (void)db_pool_in_use_g();
  (void)redis_publish_total_family();
  (void)redis_subscribe_received_total_family();
  (void)redis_self_echo_dropped_total_c();
  (void)redis_health_check_failures_total_c();
  (void)redis_reconnect_total_c();
  (void)redis_ok_g();
}

void observe_http_request(
  std::string_view method, std::string_view route, int status, double seconds) {
  // Counter: {method, route, status}
  {
    std::ostringstream key;
    key << "method=\"" << escape_label(method) << "\",route=\"" << escape_label(route)
        << "\",status=\"" << status << "\"";
    auto& c = http_requests_total();
    std::lock_guard<std::mutex> lock(c.mu);
    ++c.values[key.str()];
  }

  // Histogram: {route} only
  {
    std::string key = "route=\"";
    key += escape_label(route);
    key += "\"";
    auto& h = http_request_duration_seconds();
    Histogram* hist;
    {
      std::lock_guard<std::mutex> lock(h.mu);
      auto it = h.values.find(key);
      if (it == h.values.end()) {
        auto inserted = h.values.emplace(key, std::make_unique<Histogram>());
        hist = inserted.first->second.get();
      } else {
        hist = it->second.get();
      }
    }
    hist->observe(seconds);
  }
}

Gauge& ws_connected_clients() {
  return ws_connected_clients_g();
}

void inc_ws_messages_received(std::string_view type) {
  std::string key = "type=\"";
  key += escape_label(type);
  key += "\"";
  auto& c = ws_messages_received_total();
  std::lock_guard<std::mutex> lock(c.mu);
  ++c.values[key];
}

Gauge& db_pool_size() {
  return db_pool_size_g();
}

Gauge& db_pool_in_use() {
  return db_pool_in_use_g();
}

void inc_redis_publish(std::string_view topic_kind) {
  std::string key = "topic_kind=\"";
  key += escape_label(topic_kind);
  key += "\"";
  auto& c = redis_publish_total_family();
  std::lock_guard<std::mutex> lock(c.mu);
  ++c.values[key];
}

void inc_redis_subscribe_received(std::string_view topic_kind) {
  std::string key = "topic_kind=\"";
  key += escape_label(topic_kind);
  key += "\"";
  auto& c = redis_subscribe_received_total_family();
  std::lock_guard<std::mutex> lock(c.mu);
  ++c.values[key];
}

Counter& redis_self_echo_dropped_total() {
  return redis_self_echo_dropped_total_c();
}

Counter& redis_health_check_failures_total() {
  return redis_health_check_failures_total_c();
}

Counter& redis_reconnect_total() {
  return redis_reconnect_total_c();
}

Gauge& redis_ok() {
  return redis_ok_g();
}

std::string render() {
  std::ostringstream os;
  // Use enough precision for histogram sums.
  os << std::fixed << std::setprecision(6);

  // ----- HTTP requests counter -----
  os << "# HELP enclave_http_requests_total Total HTTP requests by method, route, status.\n"
     << "# TYPE enclave_http_requests_total counter\n";
  {
    auto& c = http_requests_total();
    std::lock_guard<std::mutex> lock(c.mu);
    for (const auto& [labels, value] : c.values) {
      os << "enclave_http_requests_total{" << labels << "} " << value << "\n";
    }
  }

  // ----- HTTP request duration histogram -----
  os << "# HELP enclave_http_request_duration_seconds HTTP request duration in seconds.\n"
     << "# TYPE enclave_http_request_duration_seconds histogram\n";
  {
    auto& h = http_request_duration_seconds();
    std::lock_guard<std::mutex> lock(h.mu);
    for (const auto& [labels, hist] : h.values) {
      hist->render(os, "enclave_http_request_duration_seconds", labels);
    }
  }

  // ----- WS connected clients gauge -----
  os << "# HELP enclave_ws_connected_clients Currently connected WebSocket clients.\n"
     << "# TYPE enclave_ws_connected_clients gauge\n"
     << "enclave_ws_connected_clients " << ws_connected_clients_g().get() << "\n";

  // ----- WS messages received counter -----
  os << "# HELP enclave_ws_messages_received_total WebSocket inbound messages by type.\n"
     << "# TYPE enclave_ws_messages_received_total counter\n";
  {
    auto& c = ws_messages_received_total();
    std::lock_guard<std::mutex> lock(c.mu);
    for (const auto& [labels, value] : c.values) {
      os << "enclave_ws_messages_received_total{" << labels << "} " << value << "\n";
    }
  }

  // ----- DB pool gauges -----
  os << "# HELP enclave_db_pool_size Total connections in the pool.\n"
     << "# TYPE enclave_db_pool_size gauge\n"
     << "enclave_db_pool_size " << db_pool_size_g().get() << "\n"
     << "# HELP enclave_db_pool_in_use Connections currently checked out.\n"
     << "# TYPE enclave_db_pool_in_use gauge\n"
     << "enclave_db_pool_in_use " << db_pool_in_use_g().get() << "\n";

  // ----- Redis publish counter -----
  os << "# HELP enclave_redis_publish_total Redis PUBLISH calls by topic kind.\n"
     << "# TYPE enclave_redis_publish_total counter\n";
  {
    auto& c = redis_publish_total_family();
    std::lock_guard<std::mutex> lock(c.mu);
    for (const auto& [labels, value] : c.values) {
      os << "enclave_redis_publish_total{" << labels << "} " << value << "\n";
    }
  }

  // ----- Redis subscribe-received counter -----
  os << "# HELP enclave_redis_subscribe_received_total Non-self envelopes "
        "received from Redis by topic kind.\n"
     << "# TYPE enclave_redis_subscribe_received_total counter\n";
  {
    auto& c = redis_subscribe_received_total_family();
    std::lock_guard<std::mutex> lock(c.mu);
    for (const auto& [labels, value] : c.values) {
      os << "enclave_redis_subscribe_received_total{" << labels << "} " << value << "\n";
    }
  }

  // ----- Redis self-echo dropped counter -----
  os << "# HELP enclave_redis_self_echo_dropped_total Envelopes dropped "
        "because they originated on this instance.\n"
     << "# TYPE enclave_redis_self_echo_dropped_total counter\n"
     << "enclave_redis_self_echo_dropped_total " << redis_self_echo_dropped_total_c().get() << "\n";

  // ----- Redis health-check failures -----
  os << "# HELP enclave_redis_health_check_failures_total Redis client "
        "errors (connect/publish/getReply).\n"
     << "# TYPE enclave_redis_health_check_failures_total counter\n"
     << "enclave_redis_health_check_failures_total " << redis_health_check_failures_total_c().get()
     << "\n";

  // ----- Redis reconnects -----
  os << "# HELP enclave_redis_reconnect_total Subscriber thread reconnects.\n"
     << "# TYPE enclave_redis_reconnect_total counter\n"
     << "enclave_redis_reconnect_total " << redis_reconnect_total_c().get() << "\n";

  // ----- Redis ok gauge -----
  os << "# HELP enclave_redis_ok 1 if Redis pub/sub is healthy, 0 otherwise.\n"
     << "# TYPE enclave_redis_ok gauge\n"
     << "enclave_redis_ok " << redis_ok_g().get() << "\n";

  return os.str();
}

}  // namespace metrics
