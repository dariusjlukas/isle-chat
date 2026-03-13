#include <gtest/gtest.h>
#include "handlers/handler_utils.h"

using json = nlohmann::json;

// --- Server Role Rank Tests ---

TEST(ServerRoleRank, Owner) {
    EXPECT_EQ(server_role_rank("owner"), 2);
}

TEST(ServerRoleRank, Admin) {
    EXPECT_EQ(server_role_rank("admin"), 1);
}

TEST(ServerRoleRank, User) {
    EXPECT_EQ(server_role_rank("user"), 0);
}

TEST(ServerRoleRank, UnknownRole) {
    EXPECT_EQ(server_role_rank("moderator"), 0);
    EXPECT_EQ(server_role_rank(""), 0);
    EXPECT_EQ(server_role_rank("superadmin"), 0);
}

TEST(ServerRoleRank, Ordering) {
    EXPECT_GT(server_role_rank("owner"), server_role_rank("admin"));
    EXPECT_GT(server_role_rank("admin"), server_role_rank("user"));
    EXPECT_GT(server_role_rank("owner"), server_role_rank("user"));
}

// --- Space Role Rank Tests ---

TEST(SpaceRoleRank, Owner) {
    EXPECT_EQ(space_role_rank("owner"), 3);
}

TEST(SpaceRoleRank, Admin) {
    EXPECT_EQ(space_role_rank("admin"), 2);
}

TEST(SpaceRoleRank, Write) {
    EXPECT_EQ(space_role_rank("write"), 1);
}

TEST(SpaceRoleRank, Read) {
    EXPECT_EQ(space_role_rank("read"), 0);
}

TEST(SpaceRoleRank, UnknownRole) {
    EXPECT_EQ(space_role_rank("viewer"), 0);
    EXPECT_EQ(space_role_rank(""), 0);
}

TEST(SpaceRoleRank, Ordering) {
    EXPECT_GT(space_role_rank("owner"), space_role_rank("admin"));
    EXPECT_GT(space_role_rank("admin"), space_role_rank("write"));
    EXPECT_GT(space_role_rank("write"), space_role_rank("read"));
}

// --- Channel Role Rank Tests ---

TEST(ChannelRoleRank, Admin) {
    EXPECT_EQ(channel_role_rank("admin"), 2);
}

TEST(ChannelRoleRank, Write) {
    EXPECT_EQ(channel_role_rank("write"), 1);
}

TEST(ChannelRoleRank, Read) {
    EXPECT_EQ(channel_role_rank("read"), 0);
}

TEST(ChannelRoleRank, UnknownRole) {
    EXPECT_EQ(channel_role_rank("owner"), 0);
    EXPECT_EQ(channel_role_rank(""), 0);
    EXPECT_EQ(channel_role_rank("moderator"), 0);
}

TEST(ChannelRoleRank, Ordering) {
    EXPECT_GT(channel_role_rank("admin"), channel_role_rank("write"));
    EXPECT_GT(channel_role_rank("write"), channel_role_rank("read"));
}

// --- Default Constants Tests ---

TEST(HandlerDefaults, InviteExpiry) {
    EXPECT_EQ(defaults::INVITE_EXPIRY_HOURS, 24);
    EXPECT_EQ(defaults::MAX_INVITE_EXPIRY_HOURS, 720);
}

TEST(HandlerDefaults, RecoveryTokenExpiry) {
    EXPECT_EQ(defaults::RECOVERY_TOKEN_EXPIRY_HOURS, 24);
}

TEST(HandlerDefaults, SearchAndMessageLimits) {
    EXPECT_EQ(defaults::SEARCH_MAX_RESULTS, 50);
    EXPECT_EQ(defaults::MESSAGE_DEFAULT_LIMIT, 50);
}

TEST(HandlerDefaults, WebAuthnTimeout) {
    EXPECT_EQ(defaults::WEBAUTHN_TIMEOUT_MS, 60000);
}

// --- Setting Parsing Tests ---

TEST(HandlerSettings, ParseIntSettingUsesValueWhenValid) {
    EXPECT_EQ(parse_int_setting_or(std::optional<std::string>{"24"}, 8), 24);
}

TEST(HandlerSettings, ParseIntSettingFallsBackWhenMissingOrInvalid) {
    EXPECT_EQ(parse_int_setting_or(std::nullopt, 8), 8);
    EXPECT_EQ(parse_int_setting_or(std::optional<std::string>{"oops"}, 8), 8);
}

TEST(HandlerSettings, ParseInt64SettingUsesFallbackForInvalidValue) {
    EXPECT_EQ(parse_i64_setting_or(std::optional<std::string>{"1024"}, 9), 1024);
    EXPECT_EQ(parse_i64_setting_or(std::optional<std::string>{"bad"}, 9), 9);
}

TEST(HandlerSettings, ParseBoolSettingRecognizesTrueAndFalse) {
    EXPECT_TRUE(parse_bool_setting_or(std::optional<std::string>{"true"}, false));
    EXPECT_FALSE(parse_bool_setting_or(std::optional<std::string>{"false"}, true));
}

TEST(HandlerSettings, ParseBoolSettingFallsBackForUnknownValue) {
    EXPECT_TRUE(parse_bool_setting_or(std::optional<std::string>{"TRUE"}, true));
    EXPECT_FALSE(parse_bool_setting_or(std::optional<std::string>{"TRUE"}, false));
    EXPECT_TRUE(parse_bool_setting_or(std::nullopt, true));
}

TEST(HandlerSettings, ParseAuthMethodsSettingUsesDefaultMethods) {
    auto methods = parse_auth_methods_setting(std::nullopt);
    ASSERT_TRUE(methods.is_array());
    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0], "passkey");
    EXPECT_EQ(methods[1], "pki");
}

TEST(HandlerSettings, ParseAuthMethodsSettingAcceptsValidJsonArray) {
    auto methods = parse_auth_methods_setting(std::optional<std::string>{R"(["password","pki"])"});
    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0], "password");
    EXPECT_EQ(methods[1], "pki");
}

TEST(HandlerSettings, ParseAuthMethodsSettingFallsBackForInvalidJson) {
    auto methods = parse_auth_methods_setting(std::optional<std::string>{"not-json"});
    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0], "passkey");
    EXPECT_EQ(methods[1], "pki");
}

TEST(HandlerSettings, ParseAuthMethodsSettingFallsBackForNonArrayJson) {
    auto methods = parse_auth_methods_setting(std::optional<std::string>{R"({"method":"password"})"});
    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0], "passkey");
    EXPECT_EQ(methods[1], "pki");
}

TEST(HandlerSettings, AuthMethodsIncludeMatchesOnlyStrings) {
    json methods = json::array({"password", 123, "pki"});
    EXPECT_TRUE(auth_methods_include(methods, "password"));
    EXPECT_TRUE(auth_methods_include(methods, "pki"));
    EXPECT_FALSE(auth_methods_include(methods, "passkey"));
}
