#pragma once

#include "../pki_identity.h"
#include "../virtual_user.h"

#include <string>

struct SearchShared {
  std::string channel_id;
  std::string space_id;
};

SearchShared& ensure_search_setup(const std::string& base_url, StatsCollector& stats);

class SearchUser : public VirtualUser {
 public:
  SearchUser(const std::string& base_url, StatsCollector& stats);

  void setup() override;
  std::vector<WeightedTask> get_tasks() override;
  std::string scenario_name() const override { return "search"; }

 private:
  void search_messages();
  void search_composite();
  void search_with_type_filter();
  void list_channel_messages();

  std::string random_term();
  std::string random_search_type();

  PkiIdentity identity_;
  std::string channel_id_;
  std::string space_id_;
};
