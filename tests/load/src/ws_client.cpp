#include "ws_client.h"
#include "pki_identity.h"

#include <chrono>
#include <cstring>
#include <iostream>

WsClient::WsClient(StatsCollector& stats) : stats_(stats) {}

WsClient::~WsClient() { close(); }

bool WsClient::connect(const std::string& url) {
  curl_ = curl_easy_init();
  if (!curl_) return false;

  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);  // WebSocket mode
  curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);

  auto start = std::chrono::steady_clock::now();
  CURLcode res = curl_easy_perform(curl_);
  auto end = std::chrono::steady_clock::now();
  double latency_ms =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

  if (res == CURLE_OK) {
    connected_ = true;
    stats_.record("WSConnect", "/ws [connect]", latency_ms, 0, true);
    return true;
  } else {
    stats_.record("WSConnect", "/ws [connect]", latency_ms, 0, false);
    curl_easy_cleanup(curl_);
    curl_ = nullptr;
    return false;
  }
}

bool WsClient::send(const json& payload, const std::string& stat_name) {
  if (!connected_ || !curl_) return false;

  std::string data = payload.dump();
  size_t sent = 0;

  auto start = std::chrono::steady_clock::now();
  CURLcode res = curl_ws_send(curl_, data.c_str(), data.size(), &sent, 0, CURLWS_TEXT);
  auto end = std::chrono::steady_clock::now();
  double latency_ms =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

  bool ok = (res == CURLE_OK);
  stats_.record("WSSend", stat_name, latency_ms, data.size(), ok);
  return ok;
}

void WsClient::start_receiver() {
  receiver_thread_ = std::thread(&WsClient::receive_loop, this);
  pinger_thread_ = std::thread(&WsClient::ping_loop, this);

  // Brief pause for initial server messages
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void WsClient::close() {
  connected_ = false;

  if (receiver_thread_.joinable()) receiver_thread_.join();
  if (pinger_thread_.joinable()) pinger_thread_.join();

  if (curl_) {
    // Send close frame
    size_t sent = 0;
    curl_ws_send(curl_, "", 0, &sent, 0, CURLWS_CLOSE);
    curl_easy_cleanup(curl_);
    curl_ = nullptr;
  }
}

std::string WsClient::last_message_id() {
  std::lock_guard<std::mutex> lock(msg_mu_);
  return last_msg_id_;
}

std::string WsClient::last_message_ts() {
  std::lock_guard<std::mutex> lock(msg_mu_);
  return last_msg_ts_;
}

void WsClient::track_send(const std::string& correlation_id) {
  std::lock_guard<std::mutex> lock(corr_mu_);
  pending_sends_[correlation_id] = std::chrono::steady_clock::now();
}

void WsClient::send_message(const std::string& channel_id, const std::string& content) {
  std::string corr_id = random_hex(16);
  std::string msg = content.empty() ? ("load test msg " + corr_id)
                                    : (content + " [" + corr_id + "]");

  track_send(corr_id);
  messages_sent_++;
  send({{"type", "send_message"}, {"channel_id", channel_id}, {"content", msg}},
       "/ws send_message");
}

void WsClient::send_typing(const std::string& channel_id) {
  send({{"type", "typing"}, {"channel_id", channel_id}}, "/ws typing");
}

void WsClient::mark_read(const std::string& channel_id) {
  std::string msg_id = last_message_id();
  if (msg_id.empty()) return;

  std::string ts = last_message_ts();
  if (ts.empty()) ts = "2026-01-01T00:00:00Z";

  send({{"type", "mark_read"},
        {"channel_id", channel_id},
        {"message_id", msg_id},
        {"timestamp", ts}},
       "/ws mark_read");
}

void WsClient::add_reaction(const std::string& emoji) {
  std::string msg_id = last_message_id();
  if (msg_id.empty()) return;

  send({{"type", "add_reaction"}, {"message_id", msg_id}, {"emoji", emoji}},
       "/ws add_reaction");
}

void WsClient::ping() {
  send({{"type", "ping"}}, "/ws ping");
}

void WsClient::receive_loop() {
  char buf[65536];

  while (connected_.load()) {
    size_t nread = 0;
    const struct curl_ws_frame* frame = nullptr;

    CURLcode res = curl_ws_recv(curl_, buf, sizeof(buf), &nread, &frame);

    if (res == CURLE_AGAIN) {
      // No data available, brief sleep and retry
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    if (res != CURLE_OK || !frame) {
      if (connected_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      continue;
    }

    if (frame->flags & CURLWS_CLOSE) {
      connected_ = false;
      break;
    }

    if (!(frame->flags & CURLWS_TEXT) || nread == 0) continue;

    std::string data(buf, nread);
    auto msg = json::parse(data, nullptr, false);
    if (msg.is_discarded()) continue;

    std::string msg_type = msg.value("type", "");

    if (msg_type == "new_message") {
      messages_received_++;
      auto message = msg.value("message", json::object());
      std::string mid = message.value("id", "");
      std::string mts = message.value("created_at", "");
      std::string content = message.value("content", "");

      {
        std::lock_guard<std::mutex> lock(msg_mu_);
        last_msg_id_ = mid;
        last_msg_ts_ = mts;
      }

      // Check for correlation ID
      std::lock_guard<std::mutex> lock(corr_mu_);
      for (auto it = pending_sends_.begin(); it != pending_sends_.end(); ++it) {
        if (content.find(it->first) != std::string::npos) {
          auto rtt = std::chrono::steady_clock::now() - it->second;
          double rtt_ms =
              std::chrono::duration_cast<std::chrono::microseconds>(rtt).count() / 1000.0;
          stats_.record("WSRoundTrip", "/ws message_roundtrip", rtt_ms, nread, true);
          pending_sends_.erase(it);
          break;
        }
      }
    } else if (msg_type == "error") {
      std::string err_msg = msg.value("message", "unknown");
      stats_.record("WSRecv", "/ws error", 0, nread, false);
    }
    // Other types (typing, read_receipt, user_online, pong) silently consumed
  }
}

void WsClient::ping_loop() {
  while (connected_.load()) {
    // Sleep 30 seconds in small intervals so we can check connected_ flag
    for (int i = 0; i < 300 && connected_.load(); i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (connected_.load() && curl_) {
      json ping_msg = {{"type", "ping"}};
      std::string data = ping_msg.dump();
      size_t sent = 0;
      curl_ws_send(curl_, data.c_str(), data.size(), &sent, 0, CURLWS_TEXT);
    }
  }
}
