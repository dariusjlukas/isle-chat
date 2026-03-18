#pragma once

#include "stats.h"

#include <string>

// Write Locust-compatible CSV stats file
void write_stats_csv(const GlobalStats& stats, double total_duration_seconds,
                     const std::string& output_path);

// Print a live one-line status to stderr
void print_live_status(const GlobalStats& stats, int active_users, double elapsed_seconds);

// Print final summary to stdout
void print_summary(const GlobalStats& stats, double total_duration_seconds);
