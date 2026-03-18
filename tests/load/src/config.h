#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

struct LoadProfile {
  std::string name;
  int users = 20;
  int spawn_rate = 5;
  int run_time_seconds = 120;
  std::string description;
};

struct Thresholds {
  double http_failure_rate_max = 0.01;
  double http_p95_max_ms = 2000;
  double http_p99_max_ms = 5000;
  double ws_message_delivery_rate_min = 0.99;
  double ws_roundtrip_p95_max_ms = 1000;
  double ws_roundtrip_p99_max_ms = 3000;
  double min_requests_per_second = 10;
};

struct CliConfig {
  std::string host = "http://localhost:9001";
  std::string profile_name = "ci";
  std::string scenario;  // Empty = all scenarios
  std::string csv_dir = "reports";
  std::string config_dir;  // Auto-detected relative to binary
};

// Parse profiles.json into a map of name -> LoadProfile
std::unordered_map<std::string, LoadProfile> load_profiles(const std::string& path);

// Parse thresholds.json
Thresholds load_thresholds(const std::string& path);

// Parse CLI arguments, returns CliConfig
CliConfig parse_cli(int argc, char* argv[]);

// Parse "120s", "300s", "5m" into seconds
int parse_duration(const std::string& s);
