#include "auth_load.h"
#include "../setup.h"

AuthLoadUser::AuthLoadUser(const std::string& base_url, StatsCollector& stats)
    : VirtualUser(base_url, stats) {}

void AuthLoadUser::setup() {
  ensure_admin_setup(http_);

  // Register this user via PKI
  identity_ = PkiIdentity();
  register_and_login(http_, identity_);

  // Also register a password user for password login tests
  pw_username_ = unique_username();
  pw_password_ = "LoadTest123!";
  password_register(http_, pw_username_, "User " + pw_username_, pw_password_);
}

std::vector<WeightedTask> AuthLoadUser::get_tasks() {
  return {
      {[this]() { token_validation(); }, 5, "token_validation"},
      {[this]() { pki_register_and_login(); }, 3, "pki_register_and_login"},
      {[this]() { password_register_and_login(); }, 2, "password_register_and_login"},
      {[this]() { pki_login_existing(); }, 1, "pki_login_existing"},
  };
}

void AuthLoadUser::token_validation() {
  http_.get("/api/users/me", {}, "/api/users/me");
}

void AuthLoadUser::pki_register_and_login() {
  PkiIdentity new_identity;
  std::string username = unique_username();

  // Challenge
  auto r = http_.post_json("/api/auth/pki/challenge", json::object(), {},
                           "/api/auth/pki/challenge [register]");
  std::string challenge = r.json_body().value("challenge", "");

  // Register
  http_.post_json("/api/auth/pki/register",
                  {{"username", username},
                   {"display_name", "User " + username},
                   {"public_key", new_identity.public_key_b64url()},
                   {"challenge", challenge},
                   {"signature", new_identity.sign(challenge)}},
                  {}, "/api/auth/pki/register");

  // Login
  pki_login(http_, new_identity);
}

void AuthLoadUser::password_register_and_login() {
  std::string username = unique_username();
  std::string pw = "LoadTest123!";
  password_register(http_, username, "User " + username, pw);
  password_login(http_, username, pw);
}

void AuthLoadUser::pki_login_existing() {
  pki_login(http_, identity_);
}
