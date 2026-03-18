#pragma once

#include "../virtual_user.h"
#include "../pki_identity.h"

#include <string>

class AuthLoadUser : public VirtualUser {
 public:
  AuthLoadUser(const std::string& base_url, StatsCollector& stats);

  void setup() override;
  std::vector<WeightedTask> get_tasks() override;
  std::string scenario_name() const override { return "auth_load"; }

 private:
  void token_validation();
  void pki_register_and_login();
  void password_register_and_login();
  void pki_login_existing();

  PkiIdentity identity_;
  std::string pw_username_;
  std::string pw_password_;
};
