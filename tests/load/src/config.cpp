#include "config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

int parse_duration(const std::string& s) {
  if (s.empty()) return 0;
  char unit = s.back();
  std::string num = s.substr(0, s.size() - 1);
  int val = std::stoi(num);
  if (unit == 'm') return val * 60;
  if (unit == 'h') return val * 3600;
  return val;  // default: seconds
}

std::unordered_map<std::string, LoadProfile> load_profiles(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) throw std::runtime_error("Cannot open profiles: " + path);

  nlohmann::json j;
  f >> j;

  std::unordered_map<std::string, LoadProfile> profiles;
  for (auto& [name, obj] : j.items()) {
    LoadProfile p;
    p.name = name;
    p.users = obj.value("users", 20);
    p.spawn_rate = obj.value("spawn_rate", 5);
    p.run_time_seconds = parse_duration(obj.value("run_time", "120s"));
    p.description = obj.value("description", "");
    profiles[name] = p;
  }
  return profiles;
}

Thresholds load_thresholds(const std::string& path) {
  Thresholds t;
  std::ifstream f(path);
  if (!f.is_open()) return t;

  nlohmann::json j;
  f >> j;

  t.http_failure_rate_max = j.value("http_failure_rate_max", t.http_failure_rate_max);
  t.http_p95_max_ms = j.value("http_p95_max_ms", t.http_p95_max_ms);
  t.http_p99_max_ms = j.value("http_p99_max_ms", t.http_p99_max_ms);
  t.ws_message_delivery_rate_min =
      j.value("ws_message_delivery_rate_min", t.ws_message_delivery_rate_min);
  t.ws_roundtrip_p95_max_ms = j.value("ws_roundtrip_p95_max_ms", t.ws_roundtrip_p95_max_ms);
  t.ws_roundtrip_p99_max_ms = j.value("ws_roundtrip_p99_max_ms", t.ws_roundtrip_p99_max_ms);
  t.min_requests_per_second = j.value("min_requests_per_second", t.min_requests_per_second);
  return t;
}

CliConfig parse_cli(int argc, char* argv[]) {
  CliConfig cfg;

  // Auto-detect config dir relative to binary location
  // Binary is in tests/load/build/, config is in tests/load/config/
  fs::path exe_dir = fs::path(argv[0]).parent_path();
  if (exe_dir.empty()) exe_dir = ".";
  fs::path config_candidate = exe_dir / "../config";
  if (fs::exists(config_candidate / "profiles.json")) {
    cfg.config_dir = fs::canonical(config_candidate).string();
  } else {
    // Fallback: try ./config (running from tests/load/)
    config_candidate = "config";
    if (fs::exists(config_candidate / "profiles.json")) {
      cfg.config_dir = fs::canonical(config_candidate).string();
    }
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--host" || arg == "-h") && i + 1 < argc) {
      cfg.host = argv[++i];
    } else if ((arg == "--profile" || arg == "-p") && i + 1 < argc) {
      cfg.profile_name = argv[++i];
    } else if ((arg == "--scenario" || arg == "-s") && i + 1 < argc) {
      cfg.scenario = argv[++i];
    } else if (arg == "--csv-dir" && i + 1 < argc) {
      cfg.csv_dir = argv[++i];
    } else if (arg == "--config-dir" && i + 1 < argc) {
      cfg.config_dir = argv[++i];
    } else if (arg == "--help") {
      std::cout << "Usage: loadtest [options]\n"
                << "  --host URL          Server URL (default: http://localhost:9001)\n"
                << "  --profile NAME      Load profile (default: ci)\n"
                << "  --scenario NAME     Run only this scenario (default: all)\n"
                << "  --csv-dir DIR       CSV output directory (default: reports)\n"
                << "  --config-dir DIR    Config directory with profiles.json\n"
                << "  --help              Show this help\n"
                << "\nScenarios: auth_load, messaging, rest_api_mix, file_upload, search, "
                   "realistic\n"
                << "Profiles: baseline, moderate, stress, spike, ci, max_throughput\n";
      exit(0);
    }
  }
  return cfg;
}
