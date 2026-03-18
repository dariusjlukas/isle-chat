#include "setup.h"

#include <iostream>
#include <mutex>
#include <stdexcept>

static std::once_flag admin_once;
static AdminData* global_admin = nullptr;

AdminData& ensure_admin_setup(HttpClient& http) {
  std::call_once(admin_once, [&]() {
    global_admin = new AdminData();
    global_admin->identity = PkiIdentity();

    // Step 1: Get challenge
    auto r = http.post_json("/api/auth/pki/challenge", json::object(),
                            {}, "/api/auth/pki/challenge [register]");
    if (!r.ok()) {
      throw std::runtime_error("Admin setup: challenge request failed (status " +
                               std::to_string(r.status) + "): " + r.body);
    }

    auto j = r.json_body();
    std::string challenge = j.value("challenge", "");

    // Step 2: Sign and register (first user becomes owner)
    std::string sig = global_admin->identity.sign(challenge);
    auto r2 = http.post_json("/api/auth/pki/register",
                             {{"username", "loadtest_admin"},
                              {"display_name", "Load Test Admin"},
                              {"public_key", global_admin->identity.public_key_b64url()},
                              {"challenge", challenge},
                              {"signature", sig}},
                             {}, "/api/auth/pki/register");
    if (!r2.ok()) {
      throw std::runtime_error("Admin setup: register failed (status " +
                               std::to_string(r2.status) + "): " + r2.body);
    }

    auto j2 = r2.json_body();
    global_admin->token = j2.value("token", "");
    if (j2.contains("user")) {
      global_admin->user_id = j2["user"].value("id", "");
    }

    // Enable open registration and password auth
    Headers auth = {{"Authorization", "Bearer " + global_admin->token}};
    http.put_json("/api/admin/settings", {{"registration_mode", "open"}}, auth,
                  "/api/admin/settings [setup]");
    http.put_json("/api/admin/settings",
                  {{"auth_methods", json::array({"passkey", "pki", "password"})}}, auth,
                  "/api/admin/settings [setup]");

    std::cerr << "  Admin setup complete (token: " << global_admin->token.substr(0, 8) << "...)\n";
  });
  return *global_admin;
}

std::string create_public_channel(HttpClient& http, const std::string& admin_token,
                                  const std::string& name) {
  Headers auth = {{"Authorization", "Bearer " + admin_token}};
  auto r = http.post_json("/api/channels", {{"name", name}, {"is_public", true}}, auth,
                          "/api/channels [setup]");
  return r.json_body().value("id", "");
}

std::string create_space(HttpClient& http, const std::string& admin_token,
                         const std::string& name) {
  Headers auth = {{"Authorization", "Bearer " + admin_token}};
  auto r = http.post_json(
      "/api/spaces",
      {{"name", name}, {"description", "Load test space"}, {"is_public", true}, {"default_role", "admin"}},
      auth, "/api/spaces [setup]");
  return r.json_body().value("id", "");
}

void enable_space_tools(HttpClient& http, const std::string& admin_token,
                        const std::string& space_id) {
  Headers auth = {{"Authorization", "Bearer " + admin_token}};
  for (auto& tool : {"tasks", "wiki", "files", "calendar"}) {
    http.put_json("/api/spaces/" + space_id + "/tools",
                  {{"tool", tool}, {"enabled", true}}, auth, "/api/spaces/:id/tools [setup]");
  }
}

std::string create_task_board(HttpClient& http, const std::string& admin_token,
                              const std::string& space_id) {
  Headers auth = {{"Authorization", "Bearer " + admin_token}};
  auto r = http.post_json("/api/spaces/" + space_id + "/tasks/boards",
                          {{"name", "Board " + random_hex(6)}}, auth,
                          "/api/spaces/:id/tasks/boards [setup]");
  return r.json_body().value("id", "");
}

std::string get_board_default_column(HttpClient& http, const std::string& admin_token,
                                     const std::string& space_id, const std::string& board_id) {
  Headers auth = {{"Authorization", "Bearer " + admin_token}};
  auto r =
      http.get("/api/spaces/" + space_id + "/tasks/boards/" + board_id, auth,
               "/api/spaces/:id/tasks/boards/:id [setup]");
  auto j = r.json_body();
  auto columns = j.value("columns", json::array());
  if (!columns.empty()) {
    return columns[0].value("id", "");
  }
  return "";
}

void create_wiki_page(HttpClient& http, const std::string& admin_token,
                      const std::string& space_id, const std::string& title,
                      const std::string& content) {
  Headers auth = {{"Authorization", "Bearer " + admin_token}};
  std::string body = content.empty() ? ("Load test wiki content " + random_hex(16)) : content;
  http.post_json("/api/spaces/" + space_id + "/wiki/pages",
                 {{"title", title}, {"content", body}}, auth,
                 "/api/spaces/:id/wiki/pages [setup]");
}

void create_calendar_event(HttpClient& http, const std::string& admin_token,
                           const std::string& space_id, const std::string& title) {
  Headers auth = {{"Authorization", "Bearer " + admin_token}};
  http.post_json("/api/spaces/" + space_id + "/calendar/events",
                 {{"title", title},
                  {"start_time", "2026-04-01T10:00:00Z"},
                  {"end_time", "2026-04-01T11:00:00Z"}},
                 auth, "/api/spaces/:id/calendar/events [setup]");
}

void join_channel(HttpClient& http, const std::string& channel_id) {
  http.post_json("/api/channels/" + channel_id + "/join", json::object(), {},
                 "/api/channels/:id/join [setup]");
}

void join_space(HttpClient& http, const std::string& space_id) {
  http.post_json("/api/spaces/" + space_id + "/join", json::object(), {},
                 "/api/spaces/:id/join [setup]");
}

std::string register_and_login(HttpClient& http, PkiIdentity& identity) {
  // Step 1: Get challenge
  auto r = http.post_json("/api/auth/pki/challenge", json::object(), {},
                          "/api/auth/pki/challenge [register]");
  std::string challenge = r.json_body().value("challenge", "");

  // Step 2: Sign and register
  std::string username = unique_username();
  std::string sig = identity.sign(challenge);
  auto r2 =
      http.post_json("/api/auth/pki/register",
                     {{"username", username},
                      {"display_name", "User " + username},
                      {"public_key", identity.public_key_b64url()},
                      {"challenge", challenge},
                      {"signature", sig}},
                     {}, "/api/auth/pki/register");

  auto j = r2.json_body();
  std::string token = j.value("token", "");
  http.set_auth_token(token);

  std::string user_id;
  if (j.contains("user")) {
    user_id = j["user"].value("id", "");
  }
  return user_id;
}

std::string password_register(HttpClient& http, const std::string& username,
                              const std::string& display_name, const std::string& password) {
  auto r = http.post_json("/api/auth/password/register",
                          {{"username", username}, {"display_name", display_name}, {"password", password}},
                          {}, "/api/auth/password/register");
  return r.json_body().value("token", "");
}

std::string password_login(HttpClient& http, const std::string& username,
                           const std::string& password) {
  auto r = http.post_json("/api/auth/password/login",
                          {{"username", username}, {"password", password}}, {},
                          "/api/auth/password/login");
  return r.json_body().value("token", "");
}

std::string pki_login(HttpClient& http, const PkiIdentity& identity) {
  // Step 1: Challenge with public key
  auto r = http.post_json("/api/auth/pki/challenge",
                          {{"public_key", identity.public_key_b64url()}}, {},
                          "/api/auth/pki/challenge [login]");
  std::string challenge = r.json_body().value("challenge", "");

  // Step 2: Sign and login
  std::string sig = identity.sign(challenge);
  auto r2 = http.post_json("/api/auth/pki/login",
                           {{"public_key", identity.public_key_b64url()},
                            {"challenge", challenge},
                            {"signature", sig}},
                           {}, "/api/auth/pki/login");
  return r2.json_body().value("token", "");
}
