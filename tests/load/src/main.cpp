#include "config.h"
#include "reporter.h"
#include "setup.h"
#include "stats.h"
#include "virtual_user.h"

#include "scenarios/auth_load.h"
#include "scenarios/file_upload.h"
#include "scenarios/messaging.h"
#include "scenarios/realistic.h"
#include "scenarios/rest_api_mix.h"
#include "scenarios/search.h"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static std::atomic<bool> g_running{true};

static void signal_handler(int) { g_running = false; }

// Factory: create a VirtualUser for the given scenario name
using UserFactory = std::function<std::unique_ptr<VirtualUser>(const std::string&, StatsCollector&)>;

static std::unordered_map<std::string, UserFactory> scenario_factories() {
  return {
      {"auth_load",
       [](const std::string& url, StatsCollector& s) -> std::unique_ptr<VirtualUser> {
         return std::make_unique<AuthLoadUser>(url, s);
       }},
      {"messaging",
       [](const std::string& url, StatsCollector& s) -> std::unique_ptr<VirtualUser> {
         return std::make_unique<MessagingUser>(url, s);
       }},
      {"rest_api_mix",
       [](const std::string& url, StatsCollector& s) -> std::unique_ptr<VirtualUser> {
         return std::make_unique<RestApiMixUser>(url, s);
       }},
      {"file_upload",
       [](const std::string& url, StatsCollector& s) -> std::unique_ptr<VirtualUser> {
         return std::make_unique<FileUploadUser>(url, s);
       }},
      {"search",
       [](const std::string& url, StatsCollector& s) -> std::unique_ptr<VirtualUser> {
         return std::make_unique<SearchUser>(url, s);
       }},
      {"realistic",
       [](const std::string& url, StatsCollector& s) -> std::unique_ptr<VirtualUser> {
         return std::make_unique<RealisticUser>(url, s);
       }},
  };
}

int main(int argc, char* argv[]) {
  // Parse CLI
  CliConfig cli = parse_cli(argc, argv);

  // Global libcurl init
  curl_global_init(CURL_GLOBAL_ALL);

  // Load config files
  std::string profiles_path = cli.config_dir + "/profiles.json";
  std::string thresholds_path = cli.config_dir + "/thresholds.json";

  auto profiles = load_profiles(profiles_path);
  auto thresholds = load_thresholds(thresholds_path);

  auto it = profiles.find(cli.profile_name);
  if (it == profiles.end()) {
    std::cerr << "ERROR: Unknown profile '" << cli.profile_name << "'\n";
    std::cerr << "Available profiles:";
    for (auto& [name, _] : profiles) std::cerr << " " << name;
    std::cerr << "\n";
    return 1;
  }

  LoadProfile& profile = it->second;

  // Determine which scenarios to run
  auto factories = scenario_factories();
  std::vector<std::pair<std::string, UserFactory>> active_scenarios;

  if (!cli.scenario.empty()) {
    auto sit = factories.find(cli.scenario);
    if (sit == factories.end()) {
      std::cerr << "ERROR: Unknown scenario '" << cli.scenario << "'\n";
      std::cerr << "Available scenarios:";
      for (auto& [name, _] : factories) std::cerr << " " << name;
      std::cerr << "\n";
      return 1;
    }
    active_scenarios.push_back({cli.scenario, sit->second});
  } else {
    for (auto& [name, factory] : factories) {
      active_scenarios.push_back({name, factory});
    }
  }

  // Check server connectivity before starting
  {
    StatsCollector probe_stats;
    HttpClient probe(cli.host, probe_stats);
    auto r = probe.get("/api/health", {}, "/api/health [probe]");
    if (!r.ok()) {
      fprintf(stderr, "ERROR: Cannot reach server at %s (status %d)\n", cli.host.c_str(),
              r.status);
      fprintf(stderr, "Make sure the backend is running before starting load tests.\n");
      curl_global_cleanup();
      return 1;
    }
  }

  // Print config
  fprintf(stderr, "\n=== C++ Load Tester ===\n\n");
  fprintf(stderr, "  Host:       %s\n", cli.host.c_str());
  fprintf(stderr, "  Profile:    %s (%s)\n", profile.name.c_str(), profile.description.c_str());
  fprintf(stderr, "  Users:      %d\n", profile.users);
  fprintf(stderr, "  Spawn rate: %d/s\n", profile.spawn_rate);
  fprintf(stderr, "  Duration:   %ds\n", profile.run_time_seconds);
  fprintf(stderr, "  Scenarios:  ");
  for (auto& [name, _] : active_scenarios) fprintf(stderr, "%s ", name.c_str());
  fprintf(stderr, "\n\n");

  // Signal handling
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Create output directory
  fs::create_directories(cli.csv_dir);

  // Per-user stats collectors and threads
  struct UserThread {
    std::unique_ptr<StatsCollector> stats;
    std::unique_ptr<VirtualUser> user;
    std::thread thread;
  };

  std::vector<UserThread> user_threads;
  user_threads.reserve(profile.users);

  GlobalStats global_stats;

  // Setup phase
  fprintf(stderr, "Running scenario setup...\n");
  {
    StatsCollector setup_stats;
    HttpClient setup_http(cli.host, setup_stats);
    ensure_admin_setup(setup_http);
  }

  // Spawn users at the configured rate
  fprintf(stderr, "Spawning %d users at %d/s...\n\n", profile.users, profile.spawn_rate);

  auto test_start = std::chrono::steady_clock::now();
  double spawn_interval_us = 1'000'000.0 / profile.spawn_rate;
  int scenario_idx = 0;

  for (int i = 0; i < profile.users && g_running.load(); i++) {
    auto spawn_start = std::chrono::steady_clock::now();

    // Round-robin across active scenarios
    auto& [scenario_name, factory] = active_scenarios[scenario_idx % active_scenarios.size()];
    scenario_idx++;

    auto stats = std::make_unique<StatsCollector>();
    auto user = factory(cli.host, *stats);

    // Setup the user (register, login, join)
    try {
      user->setup();
    } catch (const std::exception& e) {
      fprintf(stderr, "  WARNING: User %d setup failed: %s\n", i, e.what());
      continue;
    }

    // Launch user thread with reduced stack size
    auto* user_ptr = user.get();
    std::thread t([user_ptr]() { user_ptr->run(g_running); });

    // Set reduced stack size via pthread attributes isn't possible after thread creation,
    // but std::thread on Linux uses default 8MB which is fine for our use case.
    // For extreme scale (>5000 users), consider using pthread_create directly.

    user_threads.push_back({std::move(stats), std::move(user), std::move(t)});

    // Wait for spawn interval
    auto spawn_end = std::chrono::steady_clock::now();
    double spawn_us =
        std::chrono::duration_cast<std::chrono::microseconds>(spawn_end - spawn_start).count();
    double remaining_us = spawn_interval_us - spawn_us;
    if (remaining_us > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(remaining_us)));
    }
  }

  fprintf(stderr, "\n  %zu users active. Running for %ds...\n\n",
          user_threads.size(), profile.run_time_seconds);

  // Main loop: collect stats periodically and print live status
  auto run_end_time = test_start + std::chrono::seconds(profile.run_time_seconds);

  while (g_running.load() && std::chrono::steady_clock::now() < run_end_time) {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Merge stats from all user threads
    for (auto& ut : user_threads) {
      auto snapshot = ut.stats->swap();
      global_stats.merge(snapshot);
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - test_start).count() / 1000.0;
    print_live_status(global_stats, (int)user_threads.size(), elapsed);
  }

  // Signal all users to stop
  g_running = false;
  fprintf(stderr, "\n\n  Stopping users...\n");

  // Join all threads
  for (auto& ut : user_threads) {
    if (ut.thread.joinable()) ut.thread.join();
  }

  // Final stats merge
  for (auto& ut : user_threads) {
    auto snapshot = ut.stats->swap();
    global_stats.merge(snapshot);
  }

  // Teardown users
  for (auto& ut : user_threads) {
    try {
      ut.user->teardown();
    } catch (...) {
    }
  }

  auto test_end = std::chrono::steady_clock::now();
  double total_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(test_end - test_start).count() / 1000.0;

  // Write report
  std::string csv_path = cli.csv_dir + "/" + cli.profile_name + "_stats.csv";
  write_stats_csv(global_stats, total_duration, csv_path);
  fprintf(stderr, "  CSV report written to: %s\n", csv_path.c_str());

  // Print summary
  print_summary(global_stats, total_duration);

  // Check thresholds
  auto agg = global_stats.aggregated();
  bool passed = true;

  if (agg.request_count > 0) {
    double failure_rate = (double)agg.failure_count / agg.request_count;
    if (failure_rate > thresholds.http_failure_rate_max) {
      fprintf(stdout, "  FAIL: Failure rate %.2f%% exceeds max %.1f%%\n",
              failure_rate * 100, thresholds.http_failure_rate_max * 100);
      passed = false;
    }
  }

  double p95 = agg.percentile(95);
  if (p95 > thresholds.http_p95_max_ms) {
    fprintf(stdout, "  FAIL: P95 latency %.0fms exceeds max %.0fms\n", p95,
            thresholds.http_p95_max_ms);
    passed = false;
  }

  double p99 = agg.percentile(99);
  if (p99 > thresholds.http_p99_max_ms) {
    fprintf(stdout, "  FAIL: P99 latency %.0fms exceeds max %.0fms\n", p99,
            thresholds.http_p99_max_ms);
    passed = false;
  }

  double rps = total_duration > 0 ? agg.request_count / total_duration : 0;
  if (thresholds.min_requests_per_second > 0 && rps < thresholds.min_requests_per_second) {
    fprintf(stdout, "  FAIL: Throughput %.1f req/s below minimum %.0f req/s\n", rps,
            thresholds.min_requests_per_second);
    passed = false;
  }

  if (passed) {
    fprintf(stdout, "  PASSED — all thresholds met\n\n");
  } else {
    fprintf(stdout, "\n");
  }

  curl_global_cleanup();
  return passed ? 0 : 1;
}
