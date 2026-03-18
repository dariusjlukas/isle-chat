#pragma once

#include "../pki_identity.h"
#include "../virtual_user.h"
#include "../ws_client.h"

#include <memory>
#include <string>
#include <vector>

struct RealisticShared {
  std::string channel_id;
  std::string space_id;
  std::string board_id;
  std::string board_default_column_id;
};

RealisticShared& ensure_realistic_setup(const std::string& base_url, StatsCollector& stats);

class RealisticUser : public VirtualUser {
 public:
  RealisticUser(const std::string& base_url, StatsCollector& stats);

  void setup() override;
  void teardown() override;
  std::vector<WeightedTask> get_tasks() override;
  std::string scenario_name() const override { return "realistic"; }

 private:
  // WebSocket messaging (30%)
  void send_chat_message();
  void send_typing();
  void mark_read();
  void add_reaction();

  // Channel/space browsing (25%)
  void list_channels();
  void get_channel_messages();
  void list_spaces();

  // Task/wiki/calendar CRUD (20%)
  void create_task();
  void list_wiki_pages();
  void list_calendar_events();
  void list_task_boards();

  // Notifications + profile (10%)
  void list_notifications();
  void get_user_profile();

  // Search (10%)
  void search_general();
  void search_composite();

  // File upload/download (5%)
  void upload_small_file();
  void download_file();

  PkiIdentity identity_;
  std::unique_ptr<WsClient> ws_;
  std::string channel_id_;
  std::string space_id_;
  std::string board_id_;
  std::string column_id_;
  std::vector<std::string> uploaded_file_ids_;
  std::vector<std::string> task_ids_;
};
