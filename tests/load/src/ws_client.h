#pragma once

#include "stats.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

using json = nlohmann::json;

class WsClient {
 public:
  WsClient(StatsCollector& stats);
  ~WsClient();

  WsClient(const WsClient&) = delete;
  WsClient& operator=(const WsClient&) = delete;

  // Connect to WebSocket server. URL should be ws:// or wss://
  bool connect(const std::string& url);

  // Send a JSON payload
  bool send(const json& payload, const std::string& stat_name);

  // Start background receiver thread
  void start_receiver();

  // Close connection and stop receiver
  void close();

  bool connected() const { return connected_.load(); }

  // Last received message ID (set by receiver thread)
  std::string last_message_id();
  std::string last_message_ts();

  // Correlation tracking for round-trip measurement
  void track_send(const std::string& correlation_id);

  // Counters
  uint64_t messages_sent() const { return messages_sent_.load(); }
  uint64_t messages_received() const { return messages_received_.load(); }

  // High-level send helpers matching the Locust WS client
  void send_message(const std::string& channel_id, const std::string& content = "");
  void send_typing(const std::string& channel_id);
  void mark_read(const std::string& channel_id);
  void add_reaction(const std::string& emoji = "\xF0\x9F\x91\x8D");  // thumbs up UTF-8
  void ping();

 private:
  void receive_loop();
  void ping_loop();

  StatsCollector& stats_;
  CURL* curl_ = nullptr;
  std::atomic<bool> connected_{false};

  std::thread receiver_thread_;
  std::thread pinger_thread_;

  // Last received message tracking
  std::mutex msg_mu_;
  std::string last_msg_id_;
  std::string last_msg_ts_;

  // Correlation tracking
  std::mutex corr_mu_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> pending_sends_;

  std::atomic<uint64_t> messages_sent_{0};
  std::atomic<uint64_t> messages_received_{0};
};
