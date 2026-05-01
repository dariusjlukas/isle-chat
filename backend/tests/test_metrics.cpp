#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "metrics/metrics.h"

// --- Counter ---------------------------------------------------------------

TEST(MetricsCounter, IncDefaultStep) {
  metrics::Counter c;
  EXPECT_EQ(c.get(), 0u);
  c.inc();
  c.inc();
  c.inc();
  EXPECT_EQ(c.get(), 3u);
}

TEST(MetricsCounter, IncCustomStep) {
  metrics::Counter c;
  c.inc(5);
  c.inc(7);
  EXPECT_EQ(c.get(), 12u);
}

TEST(MetricsCounter, ConcurrentIncrement) {
  metrics::Counter c;
  constexpr int kThreads = 8;
  constexpr int kPerThread = 10000;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&c]() {
      for (int j = 0; j < kPerThread; ++j) c.inc();
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(c.get(), static_cast<uint64_t>(kThreads) * kPerThread);
}

// --- Gauge -----------------------------------------------------------------

TEST(MetricsGauge, IncDecSet) {
  metrics::Gauge g;
  EXPECT_EQ(g.get(), 0);
  g.inc();
  g.inc();
  g.inc();
  EXPECT_EQ(g.get(), 3);
  g.dec();
  EXPECT_EQ(g.get(), 2);
  g.set(42);
  EXPECT_EQ(g.get(), 42);
  g.dec(10);
  EXPECT_EQ(g.get(), 32);
}

TEST(MetricsGauge, NegativeAllowed) {
  metrics::Gauge g;
  g.dec();
  g.dec();
  EXPECT_EQ(g.get(), -2);
}

// --- Histogram -------------------------------------------------------------

TEST(MetricsHistogram, ObserveBucketsCumulative) {
  metrics::Histogram h;
  h.observe(0.001);  // <= 0.005
  h.observe(0.02);   // <= 0.05
  h.observe(0.2);    // <= 0.5
  h.observe(2.0);    // <= 5
  h.observe(10.0);   // > 5 -> only +Inf

  // Cumulative: bucket[i] should include all observations <= kBuckets[i].
  EXPECT_EQ(h.bucket(0), 1u);  // <= 0.005: {0.001}
  EXPECT_EQ(h.bucket(1), 1u);  // <= 0.01:  {0.001}
  EXPECT_EQ(h.bucket(2), 2u);  // <= 0.05:  {0.001, 0.02}
  EXPECT_EQ(h.bucket(3), 2u);  // <= 0.1
  EXPECT_EQ(h.bucket(4), 3u);  // <= 0.5:   {..., 0.2}
  EXPECT_EQ(h.bucket(5), 3u);  // <= 1.0
  EXPECT_EQ(h.bucket(6), 4u);  // <= 5.0:   {..., 2.0}
  EXPECT_EQ(h.bucket(7), 5u);  // +Inf:     all five

  EXPECT_EQ(h.count(), 5u);
  EXPECT_NEAR(h.sum_seconds(), 0.001 + 0.02 + 0.2 + 2.0 + 10.0, 1e-3);
}

TEST(MetricsHistogram, RenderProducesValidExposition) {
  metrics::Histogram h;
  h.observe(0.003);
  h.observe(0.5);

  std::ostringstream os;
  h.render(os, "test_hist", "route=\"/foo\"");
  std::string out = os.str();

  // Sentinel: the first bucket and the +Inf bucket must be present, with
  // the route label preserved.
  EXPECT_NE(out.find("test_hist_bucket{route=\"/foo\",le=\"0.005\"}"), std::string::npos);
  EXPECT_NE(out.find("test_hist_bucket{route=\"/foo\",le=\"+Inf\"}"), std::string::npos);
  EXPECT_NE(out.find("test_hist_sum{route=\"/foo\"}"), std::string::npos);
  EXPECT_NE(out.find("test_hist_count{route=\"/foo\"}"), std::string::npos);
}

// --- render() integration --------------------------------------------------

TEST(MetricsRender, ContainsExpectedFamilies) {
  metrics::init();

  // Drive at least one observation so the labeled families have content.
  metrics::observe_http_request("GET", "/api/health", 200, 0.012);
  metrics::ws_connected_clients().set(3);
  metrics::db_pool_size().set(10);
  metrics::db_pool_in_use().set(2);
  metrics::inc_ws_messages_received("send_message");

  std::string out = metrics::render();

  // HELP/TYPE markers
  EXPECT_NE(out.find("# TYPE enclave_http_requests_total counter"), std::string::npos);
  EXPECT_NE(out.find("# TYPE enclave_http_request_duration_seconds histogram"),
            std::string::npos);
  EXPECT_NE(out.find("# TYPE enclave_ws_connected_clients gauge"), std::string::npos);
  EXPECT_NE(out.find("# TYPE enclave_ws_messages_received_total counter"), std::string::npos);
  EXPECT_NE(out.find("# TYPE enclave_db_pool_size gauge"), std::string::npos);
  EXPECT_NE(out.find("# TYPE enclave_db_pool_in_use gauge"), std::string::npos);

  // Histogram +Inf bucket sentinel.
  EXPECT_NE(out.find("enclave_http_request_duration_seconds_bucket{route=\"/api/health\",le=\"+Inf\"}"),
            std::string::npos);

  // Specific labeled counter line for the observation we just made.
  EXPECT_NE(out.find(
              "enclave_http_requests_total{method=\"GET\",route=\"/api/health\",status=\"200\"}"),
            std::string::npos);

  // WS / DB gauges
  EXPECT_NE(out.find("enclave_ws_connected_clients 3"), std::string::npos);
  EXPECT_NE(out.find("enclave_db_pool_size 10"), std::string::npos);
  EXPECT_NE(out.find("enclave_db_pool_in_use 2"), std::string::npos);

  EXPECT_NE(out.find("enclave_ws_messages_received_total{type=\"send_message\"}"),
            std::string::npos);
}

TEST(MetricsObserveHttp, AccumulatesCounterPerLabelSet) {
  metrics::init();
  metrics::observe_http_request("GET", "/api/foo", 200, 0.001);
  metrics::observe_http_request("GET", "/api/foo", 200, 0.002);
  metrics::observe_http_request("GET", "/api/foo", 500, 0.003);
  metrics::observe_http_request("POST", "/api/foo", 200, 0.004);

  std::string out = metrics::render();
  // Three distinct counter rows for /api/foo (GET-200, GET-500, POST-200)
  // plus whatever else may have been produced by other tests.
  size_t pos = 0;
  int foo_rows = 0;
  while ((pos = out.find("route=\"/api/foo\"", pos)) != std::string::npos) {
    // Only count counter rows (not histogram bucket lines).
    auto line_start = out.rfind('\n', pos);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    if (out.compare(line_start, sizeof("enclave_http_requests_total") - 1,
                    "enclave_http_requests_total") == 0) {
      ++foo_rows;
    }
    ++pos;
  }
  EXPECT_GE(foo_rows, 3);
}
