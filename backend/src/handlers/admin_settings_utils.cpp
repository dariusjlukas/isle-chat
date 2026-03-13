#include "handlers/admin_settings_utils.h"

namespace admin_settings {

json build_settings_response(const Snapshot& snapshot) {
    return {
        {"max_file_size", parse_i64_setting_or(snapshot.max_file_size,
                                                snapshot.config_max_file_size)},
        {"max_storage_size", parse_i64_setting_or(snapshot.max_storage_size, 0)},
        {"storage_used", snapshot.storage_used},
        {"auth_methods", parse_auth_methods_setting(snapshot.auth_methods)},
        {"server_name", snapshot.server_name.value_or("EnclaveStation")},
        {"server_icon_file_id", snapshot.server_icon_file_id.value_or("")},
        {"server_icon_dark_file_id", snapshot.server_icon_dark_file_id.value_or("")},
        {"registration_mode", snapshot.registration_mode.value_or("invite")},
        {"file_uploads_enabled", parse_bool_setting_or(snapshot.file_uploads_enabled, true)},
        {"session_expiry_hours", parse_int_setting_or(snapshot.session_expiry_hours,
                                                       snapshot.config_session_expiry_hours)},
        {"setup_completed", parse_bool_setting_or(snapshot.setup_completed, false)},
        {"server_archived", snapshot.server_archived},
        {"password_min_length", parse_int_setting_or(snapshot.password_min_length, 8)},
        {"password_require_uppercase",
         parse_bool_setting_or(snapshot.password_require_uppercase, true)},
        {"password_require_lowercase",
         parse_bool_setting_or(snapshot.password_require_lowercase, true)},
        {"password_require_number",
         parse_bool_setting_or(snapshot.password_require_number, true)},
        {"password_require_special",
         parse_bool_setting_or(snapshot.password_require_special, false)},
        {"password_max_age_days", parse_int_setting_or(snapshot.password_max_age_days, 0)},
        {"password_history_count", parse_int_setting_or(snapshot.password_history_count, 0)},
        {"mfa_required_password",
         parse_bool_setting_or(snapshot.mfa_required_password, false)},
        {"mfa_required_pki", parse_bool_setting_or(snapshot.mfa_required_pki, false)},
        {"mfa_required_passkey",
         parse_bool_setting_or(snapshot.mfa_required_passkey, false)}
    };
}

std::map<std::string, std::string> collect_settings_updates(const json& settings,
                                                            bool mark_setup) {
    std::map<std::string, std::string> updates;

    if (settings.contains("max_file_size")) {
        updates["max_file_size"] = std::to_string(settings.at("max_file_size").get<int64_t>());
    }
    if (settings.contains("max_storage_size")) {
        updates["max_storage_size"] = std::to_string(settings.at("max_storage_size").get<int64_t>());
    }
    if (settings.contains("auth_methods")) {
        const auto& arr = settings.at("auth_methods");
        if (!arr.is_array() || arr.empty()) {
            throw std::runtime_error("auth_methods must be a non-empty array");
        }
        for (const auto& method : arr) {
            auto value = method.get<std::string>();
            if (value != "passkey" && value != "pki" && value != "password") {
                throw std::runtime_error("Invalid auth method: " + value);
            }
        }
        updates["auth_methods"] = arr.dump();
    }
    if (settings.contains("server_name")) {
        updates["server_name"] = settings.at("server_name").get<std::string>();
    }
    if (settings.contains("registration_mode")) {
        auto mode = settings.at("registration_mode").get<std::string>();
        if (mode != "invite" && mode != "invite_only" &&
            mode != "approval" && mode != "open") {
            throw std::runtime_error("Invalid registration mode: " + mode);
        }
        updates["registration_mode"] = mode;
    }
    if (settings.contains("file_uploads_enabled")) {
        updates["file_uploads_enabled"] =
            settings.at("file_uploads_enabled").get<bool>() ? "true" : "false";
    }
    if (settings.contains("session_expiry_hours")) {
        int hours = settings.at("session_expiry_hours").get<int>();
        if (hours <= 0) throw std::runtime_error("Session expiry must be positive");
        updates["session_expiry_hours"] = std::to_string(hours);
    }
    if (settings.contains("password_min_length")) {
        int value = settings.at("password_min_length").get<int>();
        if (value < 1) throw std::runtime_error("Minimum password length must be at least 1");
        updates["password_min_length"] = std::to_string(value);
    }
    if (settings.contains("password_require_uppercase")) {
        updates["password_require_uppercase"] =
            settings.at("password_require_uppercase").get<bool>() ? "true" : "false";
    }
    if (settings.contains("password_require_lowercase")) {
        updates["password_require_lowercase"] =
            settings.at("password_require_lowercase").get<bool>() ? "true" : "false";
    }
    if (settings.contains("password_require_number")) {
        updates["password_require_number"] =
            settings.at("password_require_number").get<bool>() ? "true" : "false";
    }
    if (settings.contains("password_require_special")) {
        updates["password_require_special"] =
            settings.at("password_require_special").get<bool>() ? "true" : "false";
    }
    if (settings.contains("password_max_age_days")) {
        int value = settings.at("password_max_age_days").get<int>();
        if (value < 0) throw std::runtime_error("Password max age must be non-negative");
        updates["password_max_age_days"] = std::to_string(value);
    }
    if (settings.contains("password_history_count")) {
        int value = settings.at("password_history_count").get<int>();
        if (value < 0) throw std::runtime_error("Password history count must be non-negative");
        updates["password_history_count"] = std::to_string(value);
    }
    if (settings.contains("mfa_required_password")) {
        updates["mfa_required_password"] =
            settings.at("mfa_required_password").get<bool>() ? "true" : "false";
    }
    if (settings.contains("mfa_required_pki")) {
        updates["mfa_required_pki"] =
            settings.at("mfa_required_pki").get<bool>() ? "true" : "false";
    }
    if (settings.contains("mfa_required_passkey")) {
        updates["mfa_required_passkey"] =
            settings.at("mfa_required_passkey").get<bool>() ? "true" : "false";
    }

    if (mark_setup) {
        updates["setup_completed"] = "true";
    }

    return updates;
}

}  // namespace admin_settings
