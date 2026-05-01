#pragma once
#include <App.h>
#include <openssl/rand.h>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include "config.h"
#include "db/database.h"

using json = nlohmann::json;

namespace handler_utils {
// Parses an int from a query-param-style string_view.
// Returns nullopt if the input is empty, not a valid int, out of int range,
// or contains trailing non-numeric characters. Leading ASCII whitespace is trimmed.
// Clamping/defaulting is the caller's responsibility.
std::optional<int> safe_parse_int(std::string_view s);
std::optional<int64_t> safe_parse_int64(std::string_view s);

// Convenience overload: returns default_val if parse fails.
inline int safe_parse_int(std::string_view s, int default_val) {
  auto v = safe_parse_int(s);
  return v.value_or(default_val);
}
inline int64_t safe_parse_int64(std::string_view s, int64_t default_val) {
  auto v = safe_parse_int64(s);
  return v.value_or(default_val);
}
}  // namespace handler_utils

// Shared constants
namespace defaults {
constexpr int INVITE_EXPIRY_HOURS = 24;
constexpr int MAX_INVITE_EXPIRY_HOURS = 720;  // 30 days
constexpr int RECOVERY_TOKEN_EXPIRY_HOURS = 24;
constexpr int SEARCH_MAX_RESULTS = 50;
constexpr int MESSAGE_DEFAULT_LIMIT = 50;
constexpr int WEBAUTHN_TIMEOUT_MS = 60000;
}  // namespace defaults

// Extract the session token from the request's `session` cookie.
// Returns empty string if the cookie is missing.
//
// History: prior to P1.4 Release C, this also fell back to the
// `Authorization: Bearer ...` header for legacy localStorage clients.
// Bearer support has been removed; cookies are the sole auth mechanism.
inline std::string extract_session_token(uWS::HttpRequest* req) {
  std::string_view cookie_header = req->getHeader("cookie");
  if (cookie_header.empty()) return "";
  // Cookie header format: "name1=val1; name2=val2; ..."
  constexpr std::string_view kKey = "session=";
  size_t pos = 0;
  while (pos < cookie_header.size()) {
    // skip leading spaces and ';'
    while (pos < cookie_header.size() && (cookie_header[pos] == ' ' || cookie_header[pos] == ';')) {
      ++pos;
    }
    // does this token start with "session="?
    if (
      cookie_header.size() - pos >= kKey.size() && cookie_header.substr(pos, kKey.size()) == kKey) {
      size_t val_start = pos + kKey.size();
      size_t val_end = cookie_header.find(';', val_start);
      if (val_end == std::string_view::npos) val_end = cookie_header.size();
      return std::string(cookie_header.substr(val_start, val_end - val_start));
    }
    // skip to next ';'
    pos = cookie_header.find(';', pos);
    if (pos == std::string_view::npos) break;
  }
  return "";
}

// CSRF double-submit check. Returns true if the X-CSRF-Token header matches
// the `csrf` cookie. Use only for state-changing methods (POST/PUT/DELETE/PATCH).
// Wired into validate_session_or_401 et al. since P1.4 Release C.
inline bool csrf_ok(uWS::HttpRequest* req) {
  std::string_view header_token = req->getHeader("x-csrf-token");
  if (header_token.empty()) return false;
  std::string_view cookie_header = req->getHeader("cookie");
  if (cookie_header.empty()) return false;
  constexpr std::string_view kKey = "csrf=";
  size_t pos = 0;
  while (pos < cookie_header.size()) {
    while (pos < cookie_header.size() && (cookie_header[pos] == ' ' || cookie_header[pos] == ';')) {
      ++pos;
    }
    if (
      cookie_header.size() - pos >= kKey.size() && cookie_header.substr(pos, kKey.size()) == kKey) {
      size_t val_start = pos + kKey.size();
      size_t val_end = cookie_header.find(';', val_start);
      if (val_end == std::string_view::npos) val_end = cookie_header.size();
      return cookie_header.substr(val_start, val_end - val_start) == header_token;
    }
    pos = cookie_header.find(';', pos);
    if (pos == std::string_view::npos) break;
  }
  return false;
}

// Returns true if the HTTP method is one that mutates state and therefore
// requires a CSRF check.
inline bool is_state_changing_method(std::string_view method) {
  return method == "post" || method == "put" || method == "delete" || method == "patch";
}

// Send a 403 with a CSRF error and return — caller must then return immediately.
template <bool SSL>
inline void send_csrf_403(uWS::HttpResponse<SSL>* res) {
  res->writeStatus("403")
    ->writeHeader("Content-Type", "application/json")
    ->end(R"({"error":"CSRF token missing or mismatched"})");
}

// Generate a cryptographically random CSRF token (16 bytes -> 32 hex chars).
// Falls back to a non-secure source if RAND_bytes fails (should be unreachable
// in practice; avoids crashing the login flow on a transient OpenSSL hiccup).
inline std::string make_csrf_token() {
  unsigned char buf[16];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    static std::atomic<uint64_t> ctr{0};
    uint64_t v = ctr.fetch_add(1) ^ static_cast<uint64_t>(std::time(nullptr));
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << v;
    return ss.str();
  }
  std::ostringstream ss;
  for (unsigned char b : buf) {
    ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  }
  return ss.str();
}

// Emit Set-Cookie headers for the session and CSRF tokens. Must be called
// before any res->end(...) / res->writeStatus(...) that finalizes the
// response. `secure` should be true when serving over HTTPS so the cookies
// get the Secure attribute; in dev (no SSL) we omit it so localhost works.
template <bool SSL>
inline void emit_session_cookies(
  uWS::HttpResponse<SSL>* res, std::string_view session_token, int max_age_seconds, bool secure) {
  std::string csrf = make_csrf_token();
  std::string secure_attr = secure ? "; Secure" : "";
  std::string session_cookie =
    "session=" + std::string(session_token) + "; HttpOnly" + secure_attr +
    "; SameSite=Strict; Path=/; Max-Age=" + std::to_string(max_age_seconds);
  // `csrf` is intentionally NOT HttpOnly — frontend JS needs to read it to
  // send the X-CSRF-Token header (double-submit pattern).
  std::string csrf_cookie = "csrf=" + csrf + secure_attr +
                            "; SameSite=Strict; Path=/; Max-Age=" + std::to_string(max_age_seconds);
  res->writeHeader("Set-Cookie", session_cookie);
  res->writeHeader("Set-Cookie", csrf_cookie);
}

// Emit Set-Cookie headers that immediately expire the session and csrf
// cookies. Used by logout. Must be called before res->end(...).
template <bool SSL>
inline void emit_clear_session_cookies(uWS::HttpResponse<SSL>* res) {
  res->writeHeader("Set-Cookie", "session=; HttpOnly; Path=/; Max-Age=0");
  res->writeHeader("Set-Cookie", "csrf=; Path=/; Max-Age=0");
}

int parse_int_setting_or(const std::optional<std::string>& setting, int fallback);
int64_t parse_i64_setting_or(const std::optional<std::string>& setting, int64_t fallback);
bool parse_bool_setting_or(const std::optional<std::string>& setting, bool fallback);
json parse_auth_methods_setting(const std::optional<std::string>& setting);
bool auth_methods_include(const json& methods, const std::string& method);

// Validate session and return user_id, or send 401/403 and return empty string.
// State-changing methods (POST/PUT/DELETE/PATCH) additionally require a valid
// CSRF double-submit token (X-CSRF-Token header matching the `csrf` cookie).
template <bool SSL>
std::string validate_session_or_401(
  uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req, Database& db) {
  auto token = extract_session_token(req);
  auto user_id = db.validate_session(token);
  if (!user_id) {
    res->writeStatus("401")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"Unauthorized"})");
    return "";
  }
  if (is_state_changing_method(req->getMethod()) && !csrf_ok(req)) {
    send_csrf_403(res);
    return "";
  }
  return *user_id;
}

// Validate session + require admin/owner role, or send 401/403 and return empty string.
// Also enforces CSRF on state-changing methods.
template <bool SSL>
std::string validate_admin_or_403(
  uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req, Database& db) {
  auto token = extract_session_token(req);
  auto user_id = db.validate_session(token);
  if (!user_id) {
    res->writeStatus("401")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"Unauthorized"})");
    return "";
  }
  if (is_state_changing_method(req->getMethod()) && !csrf_ok(req)) {
    send_csrf_403(res);
    return "";
  }
  auto user = db.find_user_by_id(*user_id);
  if (!user || (user->role != "admin" && user->role != "owner")) {
    res->writeStatus("403")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"Admin access required"})");
    return "";
  }
  return *user_id;
}

// Validate session + require owner role, or send 401/403 and return empty string.
// Also enforces CSRF on state-changing methods.
template <bool SSL>
std::string validate_owner_or_403(
  uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req, Database& db) {
  auto token = extract_session_token(req);
  auto user_id = db.validate_session(token);
  if (!user_id) {
    res->writeStatus("401")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"Unauthorized"})");
    return "";
  }
  if (is_state_changing_method(req->getMethod()) && !csrf_ok(req)) {
    send_csrf_403(res);
    return "";
  }
  auto user = db.find_user_by_id(*user_id);
  if (!user || user->role != "owner") {
    res->writeStatus("403")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"Owner access required"})");
    return "";
  }
  return *user_id;
}

// Get session expiry hours from DB setting or config default
inline int get_session_expiry(Database& db, const Config& config) {
  return parse_int_setting_or(db.get_setting("session_expiry_hours"), config.session_expiry_hours);
}

// Parse auth_methods setting with fallback to all methods enabled
inline json get_auth_methods(Database& db) {
  return parse_auth_methods_setting(db.get_setting("auth_methods"));
}

// Check if a specific auth method is enabled
inline bool is_auth_method_enabled(Database& db, const std::string& method) {
  return auth_methods_include(get_auth_methods(db), method);
}

// Server role rank (owner=2, admin=1, user=0)
int server_role_rank(const std::string& role);

// Space role rank (owner=2, admin=1, user=0)
int space_role_rank(const std::string& role);

// Channel role rank (admin=2, write=1, read=0)
int channel_role_rank(const std::string& role);
