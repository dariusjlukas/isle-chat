#include <gtest/gtest.h>
#include "handlers/handler_utils.h"

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
