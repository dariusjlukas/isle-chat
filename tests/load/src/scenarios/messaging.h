#pragma once

#include "../pki_identity.h"
#include "../virtual_user.h"
#include "../ws_client.h"

#include <memory>
#include <string>

// Shared channel for messaging scenario
struct MessagingShared {
  std::string channel_id;
};

MessagingShared& ensure_messaging_setup(const std::string& base_url, StatsCollector& stats);

class MessagingUser : public VirtualUser {
 public:
  MessagingUser(const std::string& base_url, StatsCollector& stats);

  void setup() override;
  void teardown() override;
  std::vector<WeightedTask> get_tasks() override;
  std::string scenario_name() const override { return "messaging"; }

 private:
  void send_chat_message();
  void send_typing();
  void mark_read();
  void add_reaction();
  void ws_ping();

  PkiIdentity identity_;
  std::unique_ptr<WsClient> ws_;
  std::string channel_id_;
};
