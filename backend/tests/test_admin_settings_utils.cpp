#include <gtest/gtest.h>
#include "handlers/admin_settings_utils.h"

using json = nlohmann::json;

TEST(AdminSettingsUtils, BuildSettingsResponseUsesDefaultsAndSafeFallbacks) {
    admin_settings::Snapshot snapshot;
    snapshot.config_max_file_size = 4096;
    snapshot.config_session_expiry_hours = 72;
    snapshot.storage_used = 111;
    snapshot.server_archived = true;
    snapshot.max_file_size = "invalid";
    snapshot.session_expiry_hours = "bad";
    snapshot.password_min_length = "oops";
    snapshot.auth_methods = "not-json";

    json result = admin_settings::build_settings_response(snapshot);

    EXPECT_EQ(result.at("max_file_size"), 4096);
    EXPECT_EQ(result.at("max_storage_size"), 0);
    EXPECT_EQ(result.at("storage_used"), 111);
    EXPECT_TRUE(result.at("server_archived").get<bool>());
    EXPECT_EQ(result.at("server_name"), "EnclaveStation");
    EXPECT_EQ(result.at("registration_mode"), "invite");
    EXPECT_TRUE(result.at("file_uploads_enabled").get<bool>());
    EXPECT_EQ(result.at("session_expiry_hours"), 72);
    EXPECT_FALSE(result.at("setup_completed").get<bool>());
    EXPECT_EQ(result.at("password_min_length"), 8);
    ASSERT_TRUE(result.at("auth_methods").is_array());
    EXPECT_EQ(result.at("auth_methods")[0], "passkey");
}

TEST(AdminSettingsUtils, BuildSettingsResponseUsesProvidedSettings) {
    admin_settings::Snapshot snapshot;
    snapshot.config_max_file_size = 4096;
    snapshot.config_session_expiry_hours = 72;
    snapshot.storage_used = 999;
    snapshot.max_file_size = "2048";
    snapshot.max_storage_size = "8192";
    snapshot.auth_methods = R"(["password","pki"])";
    snapshot.server_name = "My Server";
    snapshot.server_icon_file_id = "icon-1";
    snapshot.server_icon_dark_file_id = "icon-dark-1";
    snapshot.registration_mode = "open";
    snapshot.file_uploads_enabled = "false";
    snapshot.session_expiry_hours = "24";
    snapshot.setup_completed = "true";
    snapshot.password_min_length = "12";
    snapshot.password_require_uppercase = "false";
    snapshot.password_require_lowercase = "false";
    snapshot.password_require_number = "false";
    snapshot.password_require_special = "true";
    snapshot.password_max_age_days = "10";
    snapshot.password_history_count = "4";
    snapshot.mfa_required_password = "true";
    snapshot.mfa_required_pki = "true";
    snapshot.mfa_required_passkey = "true";

    json result = admin_settings::build_settings_response(snapshot);

    EXPECT_EQ(result.at("max_file_size"), 2048);
    EXPECT_EQ(result.at("max_storage_size"), 8192);
    EXPECT_EQ(result.at("storage_used"), 999);
    EXPECT_EQ(result.at("server_name"), "My Server");
    EXPECT_EQ(result.at("server_icon_file_id"), "icon-1");
    EXPECT_EQ(result.at("server_icon_dark_file_id"), "icon-dark-1");
    EXPECT_EQ(result.at("registration_mode"), "open");
    EXPECT_FALSE(result.at("file_uploads_enabled").get<bool>());
    EXPECT_EQ(result.at("session_expiry_hours"), 24);
    EXPECT_TRUE(result.at("setup_completed").get<bool>());
    EXPECT_EQ(result.at("password_min_length"), 12);
    EXPECT_FALSE(result.at("password_require_uppercase").get<bool>());
    EXPECT_FALSE(result.at("password_require_lowercase").get<bool>());
    EXPECT_FALSE(result.at("password_require_number").get<bool>());
    EXPECT_TRUE(result.at("password_require_special").get<bool>());
    EXPECT_EQ(result.at("password_max_age_days"), 10);
    EXPECT_EQ(result.at("password_history_count"), 4);
    EXPECT_TRUE(result.at("mfa_required_password").get<bool>());
    EXPECT_TRUE(result.at("mfa_required_pki").get<bool>());
    EXPECT_TRUE(result.at("mfa_required_passkey").get<bool>());
    ASSERT_EQ(result.at("auth_methods").size(), 2u);
    EXPECT_EQ(result.at("auth_methods")[0], "password");
}

TEST(AdminSettingsUtils, CollectSettingsUpdatesCapturesValidValues) {
    json body = {
        {"max_file_size", 2048},
        {"max_storage_size", 8192},
        {"auth_methods", json::array({"password", "pki"})},
        {"server_name", "Server"},
        {"registration_mode", "approval"},
        {"file_uploads_enabled", false},
        {"session_expiry_hours", 12},
        {"password_min_length", 14},
        {"password_require_uppercase", false},
        {"password_require_lowercase", true},
        {"password_require_number", false},
        {"password_require_special", true},
        {"password_max_age_days", 22},
        {"password_history_count", 5},
        {"mfa_required_password", true},
        {"mfa_required_pki", false},
        {"mfa_required_passkey", true}
    };

    auto updates = admin_settings::collect_settings_updates(body, true);

    EXPECT_EQ(updates.at("max_file_size"), "2048");
    EXPECT_EQ(updates.at("max_storage_size"), "8192");
    EXPECT_EQ(updates.at("auth_methods"), R"(["password","pki"])");
    EXPECT_EQ(updates.at("server_name"), "Server");
    EXPECT_EQ(updates.at("registration_mode"), "approval");
    EXPECT_EQ(updates.at("file_uploads_enabled"), "false");
    EXPECT_EQ(updates.at("session_expiry_hours"), "12");
    EXPECT_EQ(updates.at("password_min_length"), "14");
    EXPECT_EQ(updates.at("password_require_uppercase"), "false");
    EXPECT_EQ(updates.at("password_require_lowercase"), "true");
    EXPECT_EQ(updates.at("password_require_number"), "false");
    EXPECT_EQ(updates.at("password_require_special"), "true");
    EXPECT_EQ(updates.at("password_max_age_days"), "22");
    EXPECT_EQ(updates.at("password_history_count"), "5");
    EXPECT_EQ(updates.at("mfa_required_password"), "true");
    EXPECT_EQ(updates.at("mfa_required_pki"), "false");
    EXPECT_EQ(updates.at("mfa_required_passkey"), "true");
    EXPECT_EQ(updates.at("setup_completed"), "true");
}

TEST(AdminSettingsUtils, CollectSettingsUpdatesRejectsInvalidAuthMethods) {
    EXPECT_THROW(
        admin_settings::collect_settings_updates(json{{"auth_methods", json::array()}}, false),
        std::runtime_error);
    EXPECT_THROW(
        admin_settings::collect_settings_updates(
            json{{"auth_methods", json::array({"password", "magic"})}}, false),
        std::runtime_error);
}

TEST(AdminSettingsUtils, CollectSettingsUpdatesRejectsInvalidModesAndLimits) {
    EXPECT_THROW(
        admin_settings::collect_settings_updates(json{{"registration_mode", "closed"}}, false),
        std::runtime_error);
    EXPECT_THROW(
        admin_settings::collect_settings_updates(json{{"session_expiry_hours", 0}}, false),
        std::runtime_error);
    EXPECT_THROW(
        admin_settings::collect_settings_updates(json{{"password_min_length", 0}}, false),
        std::runtime_error);
    EXPECT_THROW(
        admin_settings::collect_settings_updates(json{{"password_max_age_days", -1}}, false),
        std::runtime_error);
    EXPECT_THROW(
        admin_settings::collect_settings_updates(json{{"password_history_count", -1}}, false),
        std::runtime_error);
}
