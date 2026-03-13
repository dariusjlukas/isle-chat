#include "handlers/auth_utils.h"

#include <openssl/rand.h>
#include "auth/webauthn.h"

namespace auth_utils {

bool is_required_setting_enabled(const std::optional<std::string>& setting) {
    return parse_bool_setting_or(setting, false);
}

MfaDecision build_mfa_decision(bool mfa_required, bool user_has_totp, const std::string& mfa_token) {
    if (!mfa_required && !user_has_totp) {
        return {};
    }

    if (mfa_required && !user_has_totp) {
        return {
            true,
            json{{"must_setup_totp", true}, {"mfa_token", mfa_token}}
        };
    }

    return {
        true,
        json{{"mfa_required", true}, {"mfa_token", mfa_token}}
    };
}

std::string registration_eligibility_message(bool is_first_user,
                                             const std::string& registration_mode,
                                             bool has_invite_token,
                                             bool invite_is_valid) {
    if (is_first_user) return "";
    if (registration_mode == "open") return "";
    if (registration_mode == "approval") {
        return "This server requires admin approval. Please request access instead.";
    }

    if (has_invite_token) {
        return invite_is_valid ? "" : "Invalid or expired invite token";
    }

    if (registration_mode == "invite_only") {
        return "This server requires an invite token to register.";
    }
    return "Invite token required. You can also request access below.";
}

password_auth::PasswordPolicy build_password_policy(
    const std::function<std::optional<std::string>(const std::string&)>& get_setting) {
    password_auth::PasswordPolicy policy;
    policy.min_length = parse_int_setting_or(
        get_setting("password_min_length"), policy.min_length);

    policy.require_uppercase = parse_bool_setting_or(
        get_setting("password_require_uppercase"), policy.require_uppercase);
    policy.require_lowercase = parse_bool_setting_or(
        get_setting("password_require_lowercase"), policy.require_lowercase);
    policy.require_number = parse_bool_setting_or(
        get_setting("password_require_number"), policy.require_number);
    policy.require_special = parse_bool_setting_or(
        get_setting("password_require_special"), policy.require_special);
    policy.max_age_days = parse_int_setting_or(
        get_setting("password_max_age_days"), policy.max_age_days);
    policy.history_count = parse_int_setting_or(
        get_setting("password_history_count"), policy.history_count);
    return policy;
}

std::string generate_user_handle() {
    unsigned char buf[16];
    RAND_bytes(buf, sizeof(buf));
    return webauthn::base64url_encode(buf, sizeof(buf));
}

json make_user_json(const User& user, bool has_password, bool has_totp) {
    return {
        {"id", user.id},
        {"username", user.username},
        {"display_name", user.display_name},
        {"role", user.role},
        {"bio", user.bio},
        {"status", user.status},
        {"avatar_file_id", user.avatar_file_id},
        {"profile_color", user.profile_color},
        {"has_password", has_password},
        {"has_totp", has_totp}
    };
}

}  // namespace auth_utils
