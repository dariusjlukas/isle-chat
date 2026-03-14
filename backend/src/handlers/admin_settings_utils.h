#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include "handlers/handler_utils.h"

namespace admin_settings {

struct Snapshot {
    int64_t config_max_file_size = 0;
    int config_session_expiry_hours = 0;
    int64_t storage_used = 0;
    bool server_archived = false;
    bool server_locked_down = false;

    std::optional<std::string> max_file_size;
    std::optional<std::string> max_storage_size;
    std::optional<std::string> auth_methods;
    std::optional<std::string> server_name;
    std::optional<std::string> server_icon_file_id;
    std::optional<std::string> server_icon_dark_file_id;
    std::optional<std::string> registration_mode;
    std::optional<std::string> file_uploads_enabled;
    std::optional<std::string> session_expiry_hours;
    std::optional<std::string> setup_completed;
    std::optional<std::string> password_min_length;
    std::optional<std::string> password_require_uppercase;
    std::optional<std::string> password_require_lowercase;
    std::optional<std::string> password_require_number;
    std::optional<std::string> password_require_special;
    std::optional<std::string> password_max_age_days;
    std::optional<std::string> password_history_count;
    std::optional<std::string> mfa_required_password;
    std::optional<std::string> mfa_required_pki;
    std::optional<std::string> mfa_required_passkey;
};

json build_settings_response(const Snapshot& snapshot);

std::map<std::string, std::string> collect_settings_updates(const json& settings,
                                                            bool mark_setup);

}  // namespace admin_settings
