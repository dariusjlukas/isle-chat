#pragma once

#include "http_client.h"
#include "pki_identity.h"

#include <mutex>
#include <string>

// Global admin data, created once by the first user
struct AdminData {
  std::string token;
  std::string user_id;
  PkiIdentity identity;
};

// Thread-safe global admin setup. Call from any thread; only runs once.
AdminData& ensure_admin_setup(HttpClient& http);

// Shared resources created per-scenario
struct SharedResources {
  std::string channel_id;
  std::string space_id;
  std::string board_id;
  std::string board_default_column_id;
};

// Setup functions (call behind a std::once_flag per scenario)
std::string create_public_channel(HttpClient& http, const std::string& admin_token,
                                  const std::string& name);
std::string create_space(HttpClient& http, const std::string& admin_token,
                         const std::string& name);
void enable_space_tools(HttpClient& http, const std::string& admin_token,
                        const std::string& space_id);
std::string create_task_board(HttpClient& http, const std::string& admin_token,
                              const std::string& space_id);
std::string get_board_default_column(HttpClient& http, const std::string& admin_token,
                                     const std::string& space_id, const std::string& board_id);
void create_wiki_page(HttpClient& http, const std::string& admin_token,
                      const std::string& space_id, const std::string& title,
                      const std::string& content = "");
void create_calendar_event(HttpClient& http, const std::string& admin_token,
                           const std::string& space_id, const std::string& title);

// Per-user setup helpers
void join_channel(HttpClient& http, const std::string& channel_id);
void join_space(HttpClient& http, const std::string& space_id);

// Register a new user via PKI and set auth token on the HttpClient. Returns user_id.
std::string register_and_login(HttpClient& http, PkiIdentity& identity);

// Register a new user via password. Returns token.
std::string password_register(HttpClient& http, const std::string& username,
                              const std::string& display_name, const std::string& password);

// Login via password. Returns token.
std::string password_login(HttpClient& http, const std::string& username,
                           const std::string& password);

// Login via PKI (existing identity). Returns token.
std::string pki_login(HttpClient& http, const PkiIdentity& identity);
