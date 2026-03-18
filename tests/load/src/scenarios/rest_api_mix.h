#pragma once

#include "../pki_identity.h"
#include "../virtual_user.h"

#include <string>
#include <vector>

struct RestApiMixShared {
  std::string channel_id;
  std::string space_id;
  std::string board_id;
  std::string board_default_column_id;
};

RestApiMixShared& ensure_rest_api_mix_setup(const std::string& base_url, StatsCollector& stats);

class RestApiMixUser : public VirtualUser {
 public:
  RestApiMixUser(const std::string& base_url, StatsCollector& stats);

  void setup() override;
  std::vector<WeightedTask> get_tasks() override;
  std::string scenario_name() const override { return "rest_api_mix"; }

 private:
  void list_channels();
  void get_channel_messages();
  void list_spaces();
  void get_space_details();
  void list_task_boards();
  void create_and_update_task();
  void list_wiki_pages();
  void create_wiki_page();
  void update_wiki_page();
  void list_calendar_events();
  void create_calendar_event();
  void list_notifications();
  void get_user_profile();
  void list_users();
  void search();

  PkiIdentity identity_;
  std::string channel_id_;
  std::string space_id_;
  std::string board_id_;
  std::string column_id_;
  std::vector<std::string> task_ids_;
  std::vector<std::string> wiki_page_ids_;
};
