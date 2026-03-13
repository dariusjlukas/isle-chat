#include <gtest/gtest.h>
#include <map>
#include "handlers/auth_utils.h"

using json = nlohmann::json;

TEST(AuthUtils, RequiredSettingEnabledOnlyForTrueString) {
    EXPECT_TRUE(auth_utils::is_required_setting_enabled(std::optional<std::string>{"true"}));
    EXPECT_FALSE(auth_utils::is_required_setting_enabled(std::optional<std::string>{"false"}));
    EXPECT_FALSE(auth_utils::is_required_setting_enabled(std::optional<std::string>{"TRUE"}));
    EXPECT_FALSE(auth_utils::is_required_setting_enabled(std::nullopt));
}

TEST(AuthUtils, BuildMfaDecisionCoversAllBranches) {
    auto none = auth_utils::build_mfa_decision(false, false, "token-1");
    EXPECT_FALSE(none.handled);

    auto setup = auth_utils::build_mfa_decision(true, false, "token-2");
    EXPECT_TRUE(setup.handled);
    EXPECT_TRUE(setup.response.at("must_setup_totp").get<bool>());
    EXPECT_EQ(setup.response.at("mfa_token"), "token-2");

    auto required = auth_utils::build_mfa_decision(false, true, "token-3");
    EXPECT_TRUE(required.handled);
    EXPECT_TRUE(required.response.at("mfa_required").get<bool>());
    EXPECT_EQ(required.response.at("mfa_token"), "token-3");
}

TEST(AuthUtils, RegistrationEligibilityAllowsFirstUser) {
    EXPECT_EQ(auth_utils::registration_eligibility_message(true, "invite", false, false), "");
}

TEST(AuthUtils, RegistrationEligibilityHandlesModesAndInviteValidation) {
    EXPECT_EQ(auth_utils::registration_eligibility_message(false, "open", false, false), "");
    EXPECT_EQ(auth_utils::registration_eligibility_message(false, "approval", false, false),
              "This server requires admin approval. Please request access instead.");
    EXPECT_EQ(auth_utils::registration_eligibility_message(false, "invite_only", false, false),
              "This server requires an invite token to register.");
    EXPECT_EQ(auth_utils::registration_eligibility_message(false, "invite", false, false),
              "Invite token required. You can also request access below.");
    EXPECT_EQ(auth_utils::registration_eligibility_message(false, "invite", true, false),
              "Invalid or expired invite token");
    EXPECT_EQ(auth_utils::registration_eligibility_message(false, "invite_only", true, true), "");
}

TEST(AuthUtils, BuildPasswordPolicyUsesDefaultsWhenMissing) {
    std::map<std::string, std::string> settings;
    auto policy = auth_utils::build_password_policy(
        [&settings](const std::string& key) -> std::optional<std::string> {
            auto it = settings.find(key);
            if (it == settings.end()) return std::nullopt;
            return it->second;
        });

    EXPECT_EQ(policy.min_length, 8);
    EXPECT_TRUE(policy.require_uppercase);
    EXPECT_TRUE(policy.require_lowercase);
    EXPECT_TRUE(policy.require_number);
    EXPECT_FALSE(policy.require_special);
    EXPECT_EQ(policy.max_age_days, 0);
    EXPECT_EQ(policy.history_count, 0);
}

TEST(AuthUtils, BuildPasswordPolicyAppliesOverridesAndIgnoresInvalidValues) {
    std::map<std::string, std::string> settings = {
        {"password_min_length", "14"},
        {"password_require_uppercase", "false"},
        {"password_require_lowercase", "false"},
        {"password_require_number", "false"},
        {"password_require_special", "true"},
        {"password_max_age_days", "30"},
        {"password_history_count", "invalid"}
    };

    auto policy = auth_utils::build_password_policy(
        [&settings](const std::string& key) -> std::optional<std::string> {
            auto it = settings.find(key);
            if (it == settings.end()) return std::nullopt;
            return it->second;
        });

    EXPECT_EQ(policy.min_length, 14);
    EXPECT_FALSE(policy.require_uppercase);
    EXPECT_FALSE(policy.require_lowercase);
    EXPECT_FALSE(policy.require_number);
    EXPECT_TRUE(policy.require_special);
    EXPECT_EQ(policy.max_age_days, 30);
    EXPECT_EQ(policy.history_count, 0);
}

TEST(AuthUtils, MakeUserJsonIncludesComputedFlags) {
    User user;
    user.id = "user-1";
    user.username = "alice";
    user.display_name = "Alice";
    user.role = "admin";
    user.bio = "bio";
    user.status = "busy";
    user.avatar_file_id = "avatar-1";
    user.profile_color = "#abcdef";

    json result = auth_utils::make_user_json(user, true, false);

    EXPECT_EQ(result.at("id"), "user-1");
    EXPECT_EQ(result.at("username"), "alice");
    EXPECT_EQ(result.at("display_name"), "Alice");
    EXPECT_EQ(result.at("role"), "admin");
    EXPECT_EQ(result.at("bio"), "bio");
    EXPECT_EQ(result.at("status"), "busy");
    EXPECT_EQ(result.at("avatar_file_id"), "avatar-1");
    EXPECT_EQ(result.at("profile_color"), "#abcdef");
    EXPECT_TRUE(result.at("has_password").get<bool>());
    EXPECT_FALSE(result.at("has_totp").get<bool>());
}

TEST(AuthUtils, GenerateUserHandleReturnsBase64UrlLookingValue) {
    auto first = auth_utils::generate_user_handle();
    auto second = auth_utils::generate_user_handle();

    EXPECT_FALSE(first.empty());
    EXPECT_NE(first, second);
    for (char c : first) {
        EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_');
    }
}
