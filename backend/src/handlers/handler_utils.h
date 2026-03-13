#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include "db/database.h"
#include "config.h"

using json = nlohmann::json;

// Shared constants
namespace defaults {
    constexpr int INVITE_EXPIRY_HOURS = 24;
    constexpr int MAX_INVITE_EXPIRY_HOURS = 720; // 30 days
    constexpr int RECOVERY_TOKEN_EXPIRY_HOURS = 24;
    constexpr int SEARCH_MAX_RESULTS = 50;
    constexpr int MESSAGE_DEFAULT_LIMIT = 50;
    constexpr int WEBAUTHN_TIMEOUT_MS = 60000;
}

// Extract Bearer token from Authorization header
inline std::string extract_bearer_token(uWS::HttpRequest* req) {
    std::string token(req->getHeader("authorization"));
    if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
    return token;
}

int parse_int_setting_or(const std::optional<std::string>& setting, int fallback);
int64_t parse_i64_setting_or(const std::optional<std::string>& setting, int64_t fallback);
bool parse_bool_setting_or(const std::optional<std::string>& setting, bool fallback);
json parse_auth_methods_setting(const std::optional<std::string>& setting);
bool auth_methods_include(const json& methods, const std::string& method);

// Validate session and return user_id, or send 401 and return empty string
template <bool SSL>
std::string validate_session_or_401(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req,
                                     Database& db) {
    auto token = extract_bearer_token(req);
    auto user_id = db.validate_session(token);
    if (!user_id) {
        res->writeStatus("401")->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
        return "";
    }
    return *user_id;
}

// Validate session + require admin/owner role, or send 401/403 and return empty string
template <bool SSL>
std::string validate_admin_or_403(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req,
                                   Database& db) {
    auto token = extract_bearer_token(req);
    auto user_id = db.validate_session(token);
    if (!user_id) {
        res->writeStatus("401")->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
        return "";
    }
    auto user = db.find_user_by_id(*user_id);
    if (!user || (user->role != "admin" && user->role != "owner")) {
        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Admin access required"})");
        return "";
    }
    return *user_id;
}

// Validate session + require owner role, or send 401/403 and return empty string
template <bool SSL>
std::string validate_owner_or_403(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req,
                                   Database& db) {
    auto token = extract_bearer_token(req);
    auto user_id = db.validate_session(token);
    if (!user_id) {
        res->writeStatus("401")->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
        return "";
    }
    auto user = db.find_user_by_id(*user_id);
    if (!user || user->role != "owner") {
        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Owner access required"})");
        return "";
    }
    return *user_id;
}

// Get session expiry hours from DB setting or config default
inline int get_session_expiry(Database& db, const Config& config) {
    return parse_int_setting_or(db.get_setting("session_expiry_hours"),
                                config.session_expiry_hours);
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

// Space/channel role rank (owner=3, admin=2, write=1, read=0)
int space_role_rank(const std::string& role);

// Channel role rank (admin=2, write=1, read=0)
int channel_role_rank(const std::string& role);
