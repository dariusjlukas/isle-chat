#pragma once

#include <functional>
#include <optional>
#include <string>
#include "auth/password.h"
#include "handlers/handler_utils.h"

namespace auth_utils {

struct MfaDecision {
    bool handled = false;
    json response = json::object();
};

bool is_required_setting_enabled(const std::optional<std::string>& setting);
MfaDecision build_mfa_decision(bool mfa_required, bool user_has_totp, const std::string& mfa_token);

std::string registration_eligibility_message(bool is_first_user,
                                             const std::string& registration_mode,
                                             bool has_invite_token,
                                             bool invite_is_valid);

password_auth::PasswordPolicy build_password_policy(
    const std::function<std::optional<std::string>(const std::string&)>& get_setting);

std::string generate_user_handle();
json make_user_json(const User& user, bool has_password, bool has_totp);

}  // namespace auth_utils
