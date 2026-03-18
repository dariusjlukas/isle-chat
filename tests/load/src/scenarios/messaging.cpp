#include "messaging.h"
#include "../setup.h"

#include <iostream>
#include <mutex>

static std::once_flag messaging_once;
static MessagingShared* messaging_shared = nullptr;

MessagingShared& ensure_messaging_setup(const std::string& base_url, StatsCollector& stats) {
  std::call_once(messaging_once, [&]() {
    messaging_shared = new MessagingShared();

    // Use a temporary HttpClient for setup
    HttpClient setup_http(base_url, stats);
    auto& admin = ensure_admin_setup(setup_http);

    messaging_shared->channel_id =
        create_public_channel(setup_http, admin.token, "load-messaging");
    std::cerr << "  Messaging channel created: " << messaging_shared->channel_id << "\n";
  });
  return *messaging_shared;
}

MessagingUser::MessagingUser(const std::string& base_url, StatsCollector& stats)
    : VirtualUser(base_url, stats) {}

void MessagingUser::setup() {
  auto& shared = ensure_messaging_setup(http_.base_url(), stats_);
  channel_id_ = shared.channel_id;

  // Register and login
  identity_ = PkiIdentity();
  register_and_login(http_, identity_);

  // Join channel
  join_channel(http_, channel_id_);

  // Open WebSocket
  std::string ws_url = http_.base_url();
  // Replace http:// with ws:// and https:// with wss://
  if (ws_url.substr(0, 7) == "http://")
    ws_url = "ws://" + ws_url.substr(7);
  else if (ws_url.substr(0, 8) == "https://")
    ws_url = "wss://" + ws_url.substr(8);

  ws_ = std::make_unique<WsClient>(stats_);
  ws_->connect(ws_url + "/ws?token=" + http_.auth_token());
  ws_->start_receiver();
}

void MessagingUser::teardown() {
  if (ws_) {
    ws_->close();
  }
}

std::vector<WeightedTask> MessagingUser::get_tasks() {
  return {
      {[this]() { send_chat_message(); }, 5, "send_chat_message"},
      {[this]() { send_typing(); }, 3, "send_typing"},
      {[this]() { mark_read(); }, 2, "mark_read"},
      {[this]() { add_reaction(); }, 1, "add_reaction"},
      {[this]() { ws_ping(); }, 1, "ws_ping"},
  };
}

void MessagingUser::send_chat_message() {
  if (ws_) ws_->send_message(channel_id_);
}

void MessagingUser::send_typing() {
  if (ws_) ws_->send_typing(channel_id_);
}

void MessagingUser::mark_read() {
  if (ws_) ws_->mark_read(channel_id_);
}

void MessagingUser::add_reaction() {
  if (ws_) ws_->add_reaction("\xF0\x9F\x91\x8D");  // thumbs up
}

void MessagingUser::ws_ping() {
  if (ws_) ws_->ping();
}
