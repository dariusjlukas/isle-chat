#pragma once

#include "http_client.h"
#include "stats.h"

#include <atomic>
#include <functional>
#include <random>
#include <string>
#include <vector>

struct WeightedTask {
  std::function<void()> func;
  int weight;
  std::string name;
};

class VirtualUser {
 public:
  VirtualUser(const std::string& base_url, StatsCollector& stats, double wait_min = 0.05,
              double wait_max = 0.2);
  virtual ~VirtualUser() = default;

  VirtualUser(const VirtualUser&) = delete;
  VirtualUser& operator=(const VirtualUser&) = delete;

  // Called once before the main loop starts. Register, login, join resources.
  virtual void setup() = 0;

  // Called once after the main loop ends.
  virtual void teardown() {}

  // Return the weighted tasks for this scenario.
  virtual std::vector<WeightedTask> get_tasks() = 0;

  // Main loop: pick task, execute, sleep. Runs until `running` becomes false.
  void run(std::atomic<bool>& running);

  // Name of this scenario (for logging)
  virtual std::string scenario_name() const = 0;

 protected:
  HttpClient http_;
  StatsCollector& stats_;
  std::mt19937 rng_;
  double wait_min_;
  double wait_max_;

  // Sleep for a random duration between wait_min and wait_max
  void random_sleep();
};
