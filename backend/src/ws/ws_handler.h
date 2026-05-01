#pragma once
#include <App.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include "config.h"
#include "db/database.h"
#include "db/db_thread_pool.h"

using json = nlohmann::json;

namespace enclave_redis {
class RedisPubSub;
}

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
  // B.3: reverse index of every topic this socket is locally subscribed to.
  // The close handler iterates this set and scrubs the socket from
  // WsHandler::topic_subscribers_ to avoid dangling pointers.
  std::unordered_set<std::string> subscribed_topics;
};

template <bool SSL>
class WsHandler {
public:
  Database& db;
  const Config& config;
  uWS::Loop* loop_;
  DbThreadPool& pool_;

  WsHandler(
    Database& db,
    const Config& config,
    uWS::Loop* loop,
    DbThreadPool& pool,
    enclave_redis::RedisPubSub* redis_pubsub = nullptr);

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

  // B.3: install / clear the optional Redis pub/sub backend. Safe to call
  // before start; the WsHandler is constructed with nullptr and main.cpp
  // attaches the RedisPubSub once it's been wired up with this WsHandler's
  // dispatch callback.
  void set_redis_pubsub(enclave_redis::RedisPubSub* redis_pubsub) {
    redis_pubsub_ = redis_pubsub;
  }

  // B.3: invoked by the RedisPubSub subscriber thread (via loop_->defer)
  // when a non-self envelope arrives. Performs local fan-out only; does NOT
  // re-publish to Redis, otherwise we'd loop forever.
  void on_redis_message(const std::string& topic, const std::string& payload);

private:
  using WebSocket = uWS::WebSocket<SSL, true, WsUserData>;

  std::mutex mutex_;
  std::unordered_map<std::string, std::unordered_set<WebSocket*>> user_sockets_;
  // B.3: source of truth for topic-based fan-out. Replaces the old
  // "find any socket that's uWS-subscribed to topic and call publish from it"
  // workaround that was fundamentally broken under multi-instance.
  std::unordered_map<std::string, std::unordered_set<WebSocket*>> topic_subscribers_;

  enclave_redis::RedisPubSub* redis_pubsub_ = nullptr;

  void handle_message(WebSocket* ws, std::string_view raw);
  void handle_send_message(WebSocket* ws, WsUserData* data, const json& j);
  void handle_edit_message(WebSocket* ws, WsUserData* data, const json& j);
  void handle_delete_message(WebSocket* ws, WsUserData* data, const json& j);
  void handle_typing(WebSocket* ws, WsUserData* data, const json& j);
  void handle_mark_read(WebSocket* ws, WsUserData* data, const json& j);
  void handle_add_reaction(WebSocket* ws, WsUserData* data, const json& j);
  void handle_remove_reaction(WebSocket* ws, WsUserData* data, const json& j);

  // B.3 helpers. The local_subscribe / local_unsubscribe pair updates both
  // topic_subscribers_ and the per-connection reverse index, and ALSO calls
  // the underlying uWS subscribe/unsubscribe so any code path that still
  // uses uWS topic broadcast continues to work. Caller must hold mutex_.
  void local_subscribe(WebSocket* socket, const std::string& topic);
  void local_unsubscribe(WebSocket* socket, const std::string& topic);
  // Scrubs a socket from every topic it had subscribed to. Acquires mutex_.
  void local_unsubscribe_all(WebSocket* socket);
  // Synchronous fan-out to every local socket subscribed to topic. Caller
  // must hold mutex_.
  void local_fan_out(const std::string& topic, std::string_view payload);
  // Unified primitive: local fan-out + optional Redis publish. All public
  // broadcast_to_* helpers funnel through this.
  void broadcast_to_topic(const std::string& topic, const std::string& payload);

  static std::vector<std::string> parse_mentions(
    const std::string& content, const std::vector<Database::ChannelMemberUsername>& members);
  static json message_to_json(const Message& msg);
};
