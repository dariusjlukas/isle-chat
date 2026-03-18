#include "reporter.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

static std::string csv_escape(const std::string& s) {
  if (s.find(',') != std::string::npos || s.find('"') != std::string::npos) {
    std::string escaped = "\"";
    for (char c : s) {
      if (c == '"') escaped += "\"\"";
      else escaped += c;
    }
    escaped += "\"";
    return escaped;
  }
  return s;
}

static std::string fmt_double(double v) {
  std::ostringstream ss;
  ss << std::setprecision(15) << v;
  return ss.str();
}

void write_stats_csv(const GlobalStats& stats, double total_duration_seconds,
                     const std::string& output_path) {
  auto all = stats.snapshot();
  auto agg = stats.aggregated();

  std::ofstream f(output_path);
  if (!f.is_open()) {
    std::cerr << "ERROR: Cannot open CSV output: " << output_path << "\n";
    return;
  }

  // Locust CSV header
  f << "Type,Name,Request Count,Failure Count,Median Response Time,"
    << "Average Response Time,Min Response Time,Max Response Time,"
    << "Average Content Size,Requests/s,Failures/s,"
    << "50%,66%,75%,80%,90%,95%,98%,99%,99.9%,99.99%,100%\n";

  // Collect and sort endpoint keys for deterministic output
  std::vector<std::string> keys;
  keys.reserve(all.size());
  for (auto& [k, _] : all) keys.push_back(k);
  std::sort(keys.begin(), keys.end());

  auto write_row = [&](const std::string& type, const std::string& name,
                        const EndpointStats& ep) {
    double rps = total_duration_seconds > 0 ? ep.request_count / total_duration_seconds : 0;
    double fps = total_duration_seconds > 0 ? ep.failure_count / total_duration_seconds : 0;

    f << csv_escape(type) << "," << csv_escape(name) << ","
      << ep.request_count << "," << ep.failure_count << ","
      << fmt_double(ep.percentile(50)) << ","
      << fmt_double(ep.avg_latency()) << ","
      << fmt_double(ep.min_latency_ms) << ","
      << fmt_double(ep.max_latency_ms) << ","
      << fmt_double(ep.avg_response_size()) << ","
      << fmt_double(rps) << ","
      << fmt_double(fps) << ","
      << fmt_double(ep.percentile(50)) << ","
      << fmt_double(ep.percentile(66)) << ","
      << fmt_double(ep.percentile(75)) << ","
      << fmt_double(ep.percentile(80)) << ","
      << fmt_double(ep.percentile(90)) << ","
      << fmt_double(ep.percentile(95)) << ","
      << fmt_double(ep.percentile(98)) << ","
      << fmt_double(ep.percentile(99)) << ","
      << fmt_double(ep.percentile(99.9)) << ","
      << fmt_double(ep.percentile(99.99)) << ","
      << fmt_double(ep.percentile(100)) << "\n";
  };

  for (auto& key : keys) {
    auto& ep = all[key];
    write_row(ep.request_type, ep.name, ep);
  }

  // Aggregated row (empty Type, Name="Aggregated")
  write_row("", "Aggregated", agg);
}

void print_live_status(const GlobalStats& stats, int active_users, double elapsed_seconds) {
  auto agg = stats.aggregated();
  double rps = elapsed_seconds > 0 ? agg.request_count / elapsed_seconds : 0;
  double p50 = agg.percentile(50);
  double p95 = agg.percentile(95);
  double fail_pct = agg.request_count > 0
                        ? 100.0 * agg.failure_count / agg.request_count
                        : 0;

  fprintf(stderr,
          "\r  Users: %4d | RPS: %8.1f | P50: %6.1fms | P95: %6.1fms | "
          "Reqs: %lu | Fail: %.1f%% | %.0fs   ",
          active_users, rps, p50, p95, (unsigned long)agg.request_count, fail_pct, elapsed_seconds);
  fflush(stderr);
}

void print_summary(const GlobalStats& stats, double total_duration_seconds) {
  auto agg = stats.aggregated();
  double rps = total_duration_seconds > 0 ? agg.request_count / total_duration_seconds : 0;
  double fps = total_duration_seconds > 0 ? agg.failure_count / total_duration_seconds : 0;

  fprintf(stdout, "\n\n=== Load Test Results ===\n\n");
  fprintf(stdout, "  Duration:     %.1f seconds\n", total_duration_seconds);
  fprintf(stdout, "  Total Reqs:   %lu\n", (unsigned long)agg.request_count);
  fprintf(stdout, "  Failures:     %lu (%.2f%%)\n", (unsigned long)agg.failure_count,
          agg.request_count > 0 ? 100.0 * agg.failure_count / agg.request_count : 0.0);
  fprintf(stdout, "  Throughput:   %.1f req/s\n", rps);
  fprintf(stdout, "  Fail Rate:    %.1f fail/s\n", fps);
  fprintf(stdout, "  P50 Latency:  %.1f ms\n", agg.percentile(50));
  fprintf(stdout, "  P95 Latency:  %.1f ms\n", agg.percentile(95));
  fprintf(stdout, "  P99 Latency:  %.1f ms\n", agg.percentile(99));
  fprintf(stdout, "  Min Latency:  %.1f ms\n", agg.min_latency_ms);
  fprintf(stdout, "  Max Latency:  %.1f ms\n", agg.max_latency_ms);
  fprintf(stdout, "\n");
}
