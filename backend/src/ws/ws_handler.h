#pragma once
#include <App.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include "config.h"
#include "db/database.h"
#include "db/db_thread_pool.h"

using json = nlohmann::json;

struct WsUserData {
  std::string user_id;
  std::string username;
  std::string role;
  // P1.6: per-connection token-bucket rate limit for inbound chatty events
  // (send_message, typing). Burst kRateLimitBurst, refill kRateLimitRefillPerSec.
  // Using steady_clock so adjustments to system time don't perturb the limiter.
  // Self-contained on WsUserData — no shared mutex hot-path.
  double rl_tokens = 20.0;
  std::chrono::steady_clock::time_point rl_last_refill = std::chrono::steady_clock::now();
};

template <bool SSL>
class WsHandler {
public:
  Database& db;
  const Config& config;
  uWS::Loop* loop_;
  DbThreadPool& pool_;

  WsHandler(Database& db, const Config& config, uWS::Loop* loop, DbThreadPool& pool);

  void register_routes(uWS::TemplatedApp<SSL>& app);
  void close_all();
  void disconnect_user(const std::string& user_id);
  void send_to_user(const std::string& user_id, const std::string& message);
  void subscribe_user_to_channel(const std::string& user_id, const std::string& channel_id);
  void unsubscribe_user_from_channel(const std::string& user_id, const std::string& channel_id);
  void broadcast_to_channel(const std::string& channel_id, const std::string& message);
  void subscribe_admins_to_channel(Database& database, const std::string& channel_id);
  void subscribe_user_to_space(const std::string& user_id, const std::string& space_id);
  void unsubscribe_user_from_space(const std::string& user_id, const std::string& space_id);
  void broadcast_to_presence(const std::string& message);
  void broadcast_to_space(const std::string& space_id, const std::string& message);
  void subscribe_admins_to_space(Database& database, const std::string& space_id);
  void disconnect_non_admins(const std::string& notify_message);

private:
  std::mutex mutex_;
  std::unordered_map<std::string, std::unordered_set<uWS::WebSocket<SSL, true, WsUserData>*>>
    user_sockets_;

  void handle_message(uWS::WebSocket<SSL, true, WsUserData>* ws, std::string_view raw);
  void handle_send_message(
    uWS::WebSocket<SSL, true, WsUserData>* ws, WsUserData* data, const json& j);
  void handle_edit_message(
    uWS::WebSocket<SSL, true, WsUserData>* ws, WsUserData* data, const json& j);
  void handle_delete_message(
    uWS::WebSocket<SSL, true, WsUserData>* ws, WsUserData* data, const json& j);
  void handle_typing(uWS::WebSocket<SSL, true, WsUserData>* ws, WsUserData* data, const json& j);
  void handle_mark_read(uWS::WebSocket<SSL, true, WsUserData>* ws, WsUserData* data, const json& j);
  void handle_add_reaction(
    uWS::WebSocket<SSL, true, WsUserData>* ws, WsUserData* data, const json& j);
  void handle_remove_reaction(
    uWS::WebSocket<SSL, true, WsUserData>* ws, WsUserData* data, const json& j);

  static std::vector<std::string> parse_mentions(
    const std::string& content, const std::vector<Database::ChannelMemberUsername>& members);
  static json message_to_json(const Message& msg);
};
