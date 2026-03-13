#include "handlers/handler_utils.h"

int parse_int_setting_or(const std::optional<std::string>& setting, int fallback) {
    if (setting) {
        try { return std::stoi(*setting); } catch (...) {}
    }
    return fallback;
}

int64_t parse_i64_setting_or(const std::optional<std::string>& setting, int64_t fallback) {
    if (setting) {
        try { return std::stoll(*setting); } catch (...) {}
    }
    return fallback;
}

bool parse_bool_setting_or(const std::optional<std::string>& setting, bool fallback) {
    if (!setting) return fallback;
    if (*setting == "true") return true;
    if (*setting == "false") return false;
    return fallback;
}

json parse_auth_methods_setting(const std::optional<std::string>& setting) {
    json auth_methods = json::array({"passkey", "pki"});
    if (setting) {
        try {
            auto parsed = json::parse(*setting);
            if (parsed.is_array()) auth_methods = parsed;
        } catch (...) {}
    }
    return auth_methods;
}

bool auth_methods_include(const json& methods, const std::string& method) {
    for (const auto& item : methods) {
        if (item.is_string() && item.get<std::string>() == method) return true;
    }
    return false;
}

int server_role_rank(const std::string& role) {
    if (role == "owner") return 2;
    if (role == "admin") return 1;
    return 0;
}

int space_role_rank(const std::string& role) {
    if (role == "owner") return 3;
    if (role == "admin") return 2;
    if (role == "write") return 1;
    return 0;
}

int channel_role_rank(const std::string& role) {
    if (role == "admin") return 2;
    if (role == "write") return 1;
    return 0;
}
