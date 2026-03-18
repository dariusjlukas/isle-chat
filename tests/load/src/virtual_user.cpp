#include "virtual_user.h"

#include <chrono>
#include <numeric>
#include <thread>

VirtualUser::VirtualUser(const std::string& base_url, StatsCollector& stats, double wait_min,
                         double wait_max)
    : http_(base_url, stats),
      stats_(stats),
      rng_(std::random_device{}()),
      wait_min_(wait_min),
      wait_max_(wait_max) {}

void VirtualUser::random_sleep() {
  std::uniform_real_distribution<double> dist(wait_min_, wait_max_);
  double sleep_sec = dist(rng_);
  std::this_thread::sleep_for(
      std::chrono::microseconds(static_cast<int64_t>(sleep_sec * 1'000'000)));
}

void VirtualUser::run(std::atomic<bool>& running) {
  auto tasks = get_tasks();
  if (tasks.empty()) return;

  // Build cumulative weight array for weighted random selection
  std::vector<int> cumulative;
  cumulative.reserve(tasks.size());
  int total_weight = 0;
  for (auto& t : tasks) {
    total_weight += t.weight;
    cumulative.push_back(total_weight);
  }

  std::uniform_int_distribution<int> dist(1, total_weight);

  while (running.load(std::memory_order_relaxed)) {
    // Pick a task using weighted random selection
    int roll = dist(rng_);
    size_t idx = 0;
    for (size_t i = 0; i < cumulative.size(); i++) {
      if (roll <= cumulative[i]) {
        idx = i;
        break;
      }
    }

    // Execute the task
    try {
      tasks[idx].func();
    } catch (...) {
      // Swallow exceptions — stats are already recorded by HttpClient/WsClient
    }

    random_sleep();
  }
}
