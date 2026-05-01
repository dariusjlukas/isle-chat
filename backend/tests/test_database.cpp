#include <gtest/gtest.h>
#include "config.h"
#include "db/database.h"

class DatabaseTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    auto config = Config::from_env();
    conn_string_ = config.pg_connection_string();
    db_ = std::make_unique<Database>(conn_string_);
    db_->run_migrations();
  }

  void SetUp() override {
    // Clean all data between tests (respecting FK constraints)
    pqxx::connection conn(conn_string_);
    pqxx::work txn(conn);
    txn.exec("DELETE FROM messages");
    txn.exec("DELETE FROM channel_members");
    txn.exec("DELETE FROM channels");
    txn.exec("DELETE FROM space_invites");
    txn.exec("DELETE FROM space_members");
    txn.exec("DELETE FROM spaces");
    txn.exec("DELETE FROM sessions");
    txn.exec("DELETE FROM auth_challenges");
    txn.exec("DELETE FROM device_tokens");
    txn.exec("DELETE FROM user_keys");
    txn.exec("DELETE FROM invite_tokens");
    txn.exec("DELETE FROM join_requests");
    txn.exec("DELETE FROM users");
    txn.commit();
  }

  static std::unique_ptr<Database> db_;
  static std::string conn_string_;
};

std::unique_ptr<Database> DatabaseTest::db_;
std::string DatabaseTest::conn_string_;

// --- Users ---

TEST_F(DatabaseTest, CreateAndFindUser) {
  auto user = db_->create_user("alice", "Alice", "PEM_KEY_A");

  EXPECT_FALSE(user.id.empty());
  EXPECT_EQ(user.username, "alice");
  EXPECT_EQ(user.display_name, "Alice");
  EXPECT_EQ(user.role, "user");
  EXPECT_FALSE(user.is_online);

  auto found_by_id = db_->find_user_by_id(user.id);
  ASSERT_TRUE(found_by_id.has_value());
  EXPECT_EQ(found_by_id->username, "alice");

  auto found_by_key = db_->find_user_by_public_key("PEM_KEY_A");
  ASSERT_TRUE(found_by_key.has_value());
  EXPECT_EQ(found_by_key->id, user.id);
}

TEST_F(DatabaseTest, CreateUserWithAdminRole) {
  auto user = db_->create_user("admin", "Admin", "PEM_KEY_ADMIN", "admin");
  EXPECT_EQ(user.role, "admin");
}

TEST_F(DatabaseTest, FindUserNotFound) {
  EXPECT_FALSE(db_->find_user_by_id("00000000-0000-0000-0000-000000000000").has_value());
  EXPECT_FALSE(db_->find_user_by_public_key("nonexistent-key").has_value());
}

TEST_F(DatabaseTest, ListUsers) {
  db_->create_user("bob", "Bob", "KEY_B");
  db_->create_user("alice", "Alice", "KEY_A");

  auto users = db_->list_users();
  EXPECT_EQ(users.size(), 2u);
}

TEST_F(DatabaseTest, CountUsers) {
  EXPECT_EQ(db_->count_users(), 0);
  db_->create_user("alice", "Alice", "KEY_A");
  EXPECT_EQ(db_->count_users(), 1);
}

TEST_F(DatabaseTest, SetUserOnline) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");

  db_->set_user_online(user.id, true);
  auto found = db_->find_user_by_id(user.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->is_online);

  db_->set_user_online(user.id, false);
  found = db_->find_user_by_id(user.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_FALSE(found->is_online);
  EXPECT_FALSE(found->last_seen.empty());
}

TEST_F(DatabaseTest, UpdateUserProfile) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");

  auto updated = db_->update_user_profile(user.id, "Alice W.", "A bio", "busy");
  EXPECT_EQ(updated.display_name, "Alice W.");
  EXPECT_EQ(updated.bio, "A bio");
  EXPECT_EQ(updated.status, "busy");
}

TEST_F(DatabaseTest, DeleteUser) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  db_->delete_user(user.id);
  EXPECT_FALSE(db_->find_user_by_id(user.id).has_value());
}

// --- Sessions ---

TEST_F(DatabaseTest, CreateAndValidateSession) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto token = db_->create_session(user.id, 24);

  EXPECT_FALSE(token.empty());

  auto user_id = db_->validate_session(token);
  ASSERT_TRUE(user_id.has_value());
  EXPECT_EQ(*user_id, user.id);
}

TEST_F(DatabaseTest, InvalidSessionToken) {
  EXPECT_FALSE(db_->validate_session("nonexistent_token").has_value());
}

TEST_F(DatabaseTest, DeleteSession) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto token = db_->create_session(user.id, 24);

  db_->delete_session(token);
  EXPECT_FALSE(db_->validate_session(token).has_value());
}

// --- Challenges ---

TEST_F(DatabaseTest, StoreAndGetChallenge) {
  db_->store_challenge("KEY_A", "CHALLENGE123");
  auto challenge = db_->get_challenge("KEY_A");
  ASSERT_TRUE(challenge.has_value());
  EXPECT_EQ(*challenge, "CHALLENGE123");
}

TEST_F(DatabaseTest, ChallengeUpsert) {
  db_->store_challenge("KEY_A", "FIRST");
  db_->store_challenge("KEY_A", "SECOND");

  auto challenge = db_->get_challenge("KEY_A");
  ASSERT_TRUE(challenge.has_value());
  EXPECT_EQ(*challenge, "SECOND");
}

TEST_F(DatabaseTest, DeleteChallenge) {
  db_->store_challenge("KEY_A", "CHALLENGE");
  db_->delete_challenge("KEY_A");
  EXPECT_FALSE(db_->get_challenge("KEY_A").has_value());
}

// --- Channels ---

TEST_F(DatabaseTest, CreateChannelAndCheckMembers) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("general", "General chat", false, user.id, {user.id});

  EXPECT_FALSE(ch.id.empty());
  EXPECT_EQ(ch.name, "general");
  EXPECT_EQ(ch.description, "General chat");
  EXPECT_TRUE(db_->is_channel_member(ch.id, user.id));

  auto member_ids = db_->get_channel_member_ids(ch.id);
  EXPECT_EQ(member_ids.size(), 1u);
  EXPECT_EQ(member_ids[0], user.id);
}

TEST_F(DatabaseTest, FindChannelById) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("test", "Test channel", false, user.id, {user.id});

  auto found = db_->find_channel_by_id(ch.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->name, "test");
}

TEST_F(DatabaseTest, AddAndRemoveChannelMember) {
  auto user_a = db_->create_user("alice", "Alice", "KEY_A");
  auto user_b = db_->create_user("bob", "Bob", "KEY_B");
  auto ch = db_->create_channel("test", "", false, user_a.id, {user_a.id});

  db_->add_channel_member(ch.id, user_b.id, "write");
  EXPECT_TRUE(db_->is_channel_member(ch.id, user_b.id));

  db_->remove_channel_member(ch.id, user_b.id);
  EXPECT_FALSE(db_->is_channel_member(ch.id, user_b.id));
}

TEST_F(DatabaseTest, MemberRoles) {
  auto user_a = db_->create_user("alice", "Alice", "KEY_A");
  auto user_b = db_->create_user("bob", "Bob", "KEY_B");
  auto ch = db_->create_channel("test", "", false, user_a.id, {user_a.id});

  // Creator gets admin role
  EXPECT_EQ(db_->get_member_role(ch.id, user_a.id), "admin");

  // Added member gets specified role
  db_->add_channel_member(ch.id, user_b.id, "write");
  EXPECT_EQ(db_->get_member_role(ch.id, user_b.id), "write");

  // Update role
  db_->update_member_role(ch.id, user_b.id, "read");
  EXPECT_EQ(db_->get_member_role(ch.id, user_b.id), "read");
}

TEST_F(DatabaseTest, EffectiveRoleForServerAdmin) {
  auto admin = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("test", "", false, user.id, {user.id});

  // Server admin gets admin effective role even without channel membership
  EXPECT_EQ(db_->get_effective_role(ch.id, admin.id), "admin");
}

TEST_F(DatabaseTest, CreateAndFindDmChannel) {
  auto user_a = db_->create_user("alice", "Alice", "KEY_A");
  auto user_b = db_->create_user("bob", "Bob", "KEY_B");

  auto dm = db_->create_channel("dm", "", true, user_a.id, {user_a.id, user_b.id}, false);

  // Find DM in both directions
  auto found1 = db_->find_dm_channel(user_a.id, user_b.id);
  ASSERT_TRUE(found1.has_value());
  EXPECT_EQ(found1->id, dm.id);

  auto found2 = db_->find_dm_channel(user_b.id, user_a.id);
  ASSERT_TRUE(found2.has_value());
  EXPECT_EQ(found2->id, dm.id);
}

TEST_F(DatabaseTest, ListPublicChannels) {
  auto user_a = db_->create_user("alice", "Alice", "KEY_A");
  auto user_b = db_->create_user("bob", "Bob", "KEY_B");

  // Channel where user_a is a member
  db_->create_channel("joined", "", false, user_a.id, {user_a.id}, true);
  // Public channel where user_a is NOT a member
  db_->create_channel("available", "", false, user_b.id, {user_b.id}, true);

  auto public_channels = db_->list_public_channels(user_a.id);
  EXPECT_EQ(public_channels.size(), 1u);
  EXPECT_EQ(public_channels[0].name, "available");
}

TEST_F(DatabaseTest, UpdateChannel) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("old", "old desc", false, user.id, {user.id}, true, "write");

  auto updated = db_->update_channel(ch.id, "new", "new desc", false, "read");
  EXPECT_EQ(updated.name, "new");
  EXPECT_EQ(updated.description, "new desc");
  EXPECT_FALSE(updated.is_public);
  EXPECT_EQ(updated.default_role, "read");
}

TEST_F(DatabaseTest, ChannelMembersWithRoles) {
  auto user_a = db_->create_user("alice", "Alice", "KEY_A");
  auto user_b = db_->create_user("bob", "Bob", "KEY_B");
  auto ch = db_->create_channel("test", "", false, user_a.id, {user_a.id, user_b.id});

  auto members = db_->get_channel_members_with_roles(ch.id);
  EXPECT_EQ(members.size(), 2u);
}

// --- Messages ---

TEST_F(DatabaseTest, CreateAndGetMessages) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("test", "", false, user.id, {user.id});

  db_->create_message(ch.id, user.id, "msg1");
  db_->create_message(ch.id, user.id, "msg2");
  db_->create_message(ch.id, user.id, "msg3");

  auto messages = db_->get_messages(ch.id);
  EXPECT_EQ(messages.size(), 3u);
  // Messages should be in chronological order
  EXPECT_EQ(messages[0].content, "msg1");
  EXPECT_EQ(messages[2].content, "msg3");
}

TEST_F(DatabaseTest, GetMessagesWithPagination) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("test", "", false, user.id, {user.id});

  for (int i = 0; i < 5; i++) {
    db_->create_message(ch.id, user.id, "msg" + std::to_string(i));
  }

  auto page1 = db_->get_messages(ch.id, 2);
  EXPECT_EQ(page1.size(), 2u);
  // Most recent messages (get_messages returns last N in chronological order)
  EXPECT_EQ(page1[0].content, "msg3");
  EXPECT_EQ(page1[1].content, "msg4");

  // Get earlier messages using cursor
  auto page2 = db_->get_messages(ch.id, 2, page1[0].created_at);
  EXPECT_EQ(page2.size(), 2u);
  EXPECT_EQ(page2[0].content, "msg1");
  EXPECT_EQ(page2[1].content, "msg2");
}

TEST_F(DatabaseTest, EditMessage) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("test", "", false, user.id, {user.id});
  auto msg = db_->create_message(ch.id, user.id, "original");

  auto edited = db_->edit_message(msg.id, user.id, "edited");
  EXPECT_EQ(edited.content, "edited");
  EXPECT_FALSE(edited.edited_at.empty());
}

TEST_F(DatabaseTest, EditMessageWrongUser) {
  auto user_a = db_->create_user("alice", "Alice", "KEY_A");
  auto user_b = db_->create_user("bob", "Bob", "KEY_B");
  auto ch = db_->create_channel("test", "", false, user_a.id, {user_a.id, user_b.id});
  auto msg = db_->create_message(ch.id, user_a.id, "original");

  EXPECT_THROW(db_->edit_message(msg.id, user_b.id, "hacked"), std::runtime_error);
}

TEST_F(DatabaseTest, DeleteMessage) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto ch = db_->create_channel("test", "", false, user.id, {user.id});
  auto msg = db_->create_message(ch.id, user.id, "to delete");

  auto deleted = db_->delete_message(msg.id, user.id);
  EXPECT_TRUE(deleted.is_deleted);
  EXPECT_TRUE(deleted.content.empty());
}

// --- Invites ---

TEST_F(DatabaseTest, CreateAndValidateInvite) {
  auto user = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");
  auto token = db_->create_invite(user.id);

  EXPECT_FALSE(token.empty());
  EXPECT_TRUE(db_->validate_invite(token));
}

TEST_F(DatabaseTest, UseInvite) {
  auto admin = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");
  auto token = db_->create_invite(admin.id);

  auto user = db_->create_user("alice", "Alice", "KEY_A");
  db_->use_invite(token, user.id);
  EXPECT_FALSE(db_->validate_invite(token));
}

TEST_F(DatabaseTest, ListInvites) {
  auto user = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");
  db_->create_invite(user.id);
  db_->create_invite(user.id);

  auto invites = db_->list_invites();
  EXPECT_EQ(invites.size(), 2u);
}

TEST_F(DatabaseTest, RevokeUnusedInvite) {
  auto user = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");
  db_->create_invite(user.id);

  auto invites = db_->list_invites();
  ASSERT_EQ(invites.size(), 1u);

  bool revoked = db_->revoke_invite(invites[0].id);
  EXPECT_TRUE(revoked);

  // Invite should be gone
  auto after = db_->list_invites();
  EXPECT_EQ(after.size(), 0u);
}

TEST_F(DatabaseTest, RevokeUsedInviteReturnsFalse) {
  auto admin = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");
  auto token = db_->create_invite(admin.id);

  auto user = db_->create_user("alice", "Alice", "KEY_A");
  db_->use_invite(token, user.id);

  auto invites = db_->list_invites();
  ASSERT_EQ(invites.size(), 1u);

  bool revoked = db_->revoke_invite(invites[0].id);
  EXPECT_FALSE(revoked);

  // Used invite should still exist
  auto after = db_->list_invites();
  EXPECT_EQ(after.size(), 1u);
}

TEST_F(DatabaseTest, RevokeNonexistentInviteReturnsFalse) {
  bool revoked = db_->revoke_invite("00000000-0000-0000-0000-000000000000");
  EXPECT_FALSE(revoked);
}

TEST_F(DatabaseTest, RevokedInviteCannotBeValidated) {
  auto user = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");
  auto token = db_->create_invite(user.id);

  EXPECT_TRUE(db_->validate_invite(token));

  auto invites = db_->list_invites();
  db_->revoke_invite(invites[0].id);

  EXPECT_FALSE(db_->validate_invite(token));
}

// --- Join Requests ---

TEST_F(DatabaseTest, CreateAndListJoinRequests) {
  auto id = db_->create_join_request("alice", "Alice", "KEY_A");
  EXPECT_FALSE(id.empty());

  auto pending = db_->list_pending_requests();
  EXPECT_EQ(pending.size(), 1u);
  EXPECT_EQ(pending[0].username, "alice");

  auto found = db_->get_join_request(id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "pending");
}

TEST_F(DatabaseTest, UpdateJoinRequestStatus) {
  auto id = db_->create_join_request("alice", "Alice", "KEY_A");
  auto admin = db_->create_user("admin", "Admin", "KEY_ADMIN", "admin");

  db_->update_join_request(id, "approved", admin.id);

  auto pending = db_->list_pending_requests();
  EXPECT_TRUE(pending.empty());

  auto found = db_->get_join_request(id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "approved");
}

// --- Device Keys ---

TEST_F(DatabaseTest, DeviceTokenLifecycle) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto token = db_->create_device_token(user.id);

  EXPECT_FALSE(token.empty());

  auto user_id = db_->validate_device_token(token);
  ASSERT_TRUE(user_id.has_value());
  EXPECT_EQ(*user_id, user.id);

  db_->mark_device_token_used(token);
  EXPECT_FALSE(db_->validate_device_token(token).has_value());
}

TEST_F(DatabaseTest, AddAndListUserKeys) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");

  // User gets a primary key on creation
  auto keys = db_->list_user_keys(user.id);
  EXPECT_EQ(keys.size(), 1u);

  db_->add_user_key(user.id, "KEY_B", "second device");
  keys = db_->list_user_keys(user.id);
  EXPECT_EQ(keys.size(), 2u);
}

TEST_F(DatabaseTest, RemoveUserKeyPreventsLastRemoval) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto keys = db_->list_user_keys(user.id);
  ASSERT_EQ(keys.size(), 1u);

  EXPECT_THROW(db_->remove_user_key(keys[0].id, user.id), std::runtime_error);
}

// --- User Avatars ---

TEST_F(DatabaseTest, SetAndClearUserAvatar) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  EXPECT_TRUE(user.avatar_file_id.empty());

  db_->set_user_avatar(user.id, "avatar-file-123");
  auto found = db_->find_user_by_id(user.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->avatar_file_id, "avatar-file-123");

  db_->clear_user_avatar(user.id);
  found = db_->find_user_by_id(user.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->avatar_file_id.empty());
}

TEST_F(DatabaseTest, UserProfileColor) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  EXPECT_TRUE(user.profile_color.empty());

  auto updated = db_->update_user_profile(user.id, "Alice", "", "", "#ff0000");
  EXPECT_EQ(updated.profile_color, "#ff0000");
}

// --- Spaces ---

TEST_F(DatabaseTest, CreateSpace) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto space = db_->create_space("Engineering", "Eng team", true, user.id, "write");

  EXPECT_FALSE(space.id.empty());
  EXPECT_EQ(space.name, "Engineering");
  EXPECT_EQ(space.description, "Eng team");
  EXPECT_TRUE(space.is_public);
  EXPECT_TRUE(space.avatar_file_id.empty());
  EXPECT_TRUE(space.profile_color.empty());
}

TEST_F(DatabaseTest, SetAndClearSpaceAvatar) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto space = db_->create_space("Team", "", true, user.id);
  EXPECT_TRUE(space.avatar_file_id.empty());

  db_->set_space_avatar(space.id, "space-avatar-456");
  auto found = db_->find_space_by_id(space.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->avatar_file_id, "space-avatar-456");

  db_->clear_space_avatar(space.id);
  found = db_->find_space_by_id(space.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->avatar_file_id.empty());
}

TEST_F(DatabaseTest, UpdateSpaceProfileColor) {
  auto user = db_->create_user("alice", "Alice", "KEY_A");
  auto space = db_->create_space("Team", "", true, user.id);

  auto updated = db_->update_space(space.id, "Team", "", true, "user", "#00ff00");
  EXPECT_EQ(updated.profile_color, "#00ff00");
}

// --- Server Lockdown ---

TEST_F(DatabaseTest, ServerLockedDownDefaultsFalse) {
  EXPECT_FALSE(db_->is_server_locked_down());
}

TEST_F(DatabaseTest, SetServerLockedDown) {
  db_->set_server_locked_down(true);
  EXPECT_TRUE(db_->is_server_locked_down());
}

TEST_F(DatabaseTest, LiftServerLockdown) {
  db_->set_server_locked_down(true);
  EXPECT_TRUE(db_->is_server_locked_down());
  db_->set_server_locked_down(false);
  EXPECT_FALSE(db_->is_server_locked_down());
}

TEST_F(DatabaseTest, SetServerLockedDownIdempotent) {
  db_->set_server_locked_down(true);
  db_->set_server_locked_down(true);
  EXPECT_TRUE(db_->is_server_locked_down());
  db_->set_server_locked_down(false);
  db_->set_server_locked_down(false);
  EXPECT_FALSE(db_->is_server_locked_down());
}

// --- Channel & Space Listing Methods ---

TEST_F(DatabaseTest, ListUserChannels) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  db_->create_channel("ch1", "First", false, alice.id, {alice.id});
  db_->create_channel("ch2", "Second", false, alice.id, {alice.id, bob.id});
  db_->create_channel("ch3", "Third", false, bob.id, {bob.id});

  auto alice_channels = db_->list_user_channels(alice.id);
  EXPECT_EQ(alice_channels.size(), 2u);

  auto bob_channels = db_->list_user_channels(bob.id);
  EXPECT_EQ(bob_channels.size(), 2u);
}

TEST_F(DatabaseTest, ListUserChannelsEmpty) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto channels = db_->list_user_channels(alice.id);
  EXPECT_TRUE(channels.empty());
}

TEST_F(DatabaseTest, ListSpaceChannels) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto space = db_->create_space("Engineering", "Eng team", true, alice.id);

  db_->create_channel("general", "", false, alice.id, {alice.id}, true, "write", space.id);
  db_->create_channel("random", "", false, alice.id, {alice.id}, true, "write", space.id);

  auto channels = db_->list_space_channels(space.id);
  EXPECT_EQ(channels.size(), 2u);
}

TEST_F(DatabaseTest, ListSpaceChannelsEmpty) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto space = db_->create_space("Empty", "", true, alice.id);

  auto channels = db_->list_space_channels(space.id);
  EXPECT_TRUE(channels.empty());
}

TEST_F(DatabaseTest, ListSpaceChannelsDoesNotIncludeOtherSpaces) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto space1 = db_->create_space("Space1", "", true, alice.id);
  auto space2 = db_->create_space("Space2", "", true, alice.id);

  db_->create_channel("ch1", "", false, alice.id, {alice.id}, true, "write", space1.id);
  db_->create_channel("ch2", "", false, alice.id, {alice.id}, true, "write", space2.id);

  auto channels = db_->list_space_channels(space1.id);
  EXPECT_EQ(channels.size(), 1u);
  EXPECT_EQ(channels[0].name, "ch1");
}

TEST_F(DatabaseTest, ListBrowsableSpaceChannels) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");
  auto space = db_->create_space("Engineering", "", true, alice.id);

  // Alice is a member of "joined"
  db_->create_channel(
    "joined", "Already joined", false, alice.id, {alice.id}, true, "write", space.id);
  // Alice is NOT a member of "available"
  db_->create_channel("available", "Can browse", false, bob.id, {bob.id}, true, "write", space.id);

  auto browsable = db_->list_browsable_space_channels(space.id, alice.id);
  EXPECT_EQ(browsable.size(), 1u);
  EXPECT_EQ(browsable[0].name, "available");
}

TEST_F(DatabaseTest, ListBrowsableSpaceChannelsWithSearch) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");
  auto space = db_->create_space("Engineering", "", true, alice.id);

  db_->create_channel("frontend", "React stuff", false, bob.id, {bob.id}, true, "write", space.id);
  db_->create_channel("backend", "C++ stuff", false, bob.id, {bob.id}, true, "write", space.id);
  db_->create_channel("devops", "Infrastructure", false, bob.id, {bob.id}, true, "write", space.id);

  // Search by name
  auto results = db_->list_browsable_space_channels(space.id, alice.id, "front");
  EXPECT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].name, "frontend");

  // Search by description
  auto results2 = db_->list_browsable_space_channels(space.id, alice.id, "Infrastructure");
  EXPECT_EQ(results2.size(), 1u);
  EXPECT_EQ(results2[0].name, "devops");

  // Search with no matches
  auto results3 = db_->list_browsable_space_channels(space.id, alice.id, "nonexistent");
  EXPECT_TRUE(results3.empty());
}

TEST_F(DatabaseTest, ListBrowsableSpaceChannelsExcludesDMs) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");
  auto space = db_->create_space("Team", "", true, alice.id);

  // Create a regular channel (not joined by alice)
  db_->create_channel("public-ch", "", false, bob.id, {bob.id}, true, "write", space.id);
  // DMs are is_direct=true, so they should be excluded from browsable list
  db_->create_channel("dm", "", true, bob.id, {bob.id, alice.id}, false, "write", space.id);

  auto browsable = db_->list_browsable_space_channels(space.id, alice.id);
  EXPECT_EQ(browsable.size(), 1u);
  EXPECT_EQ(browsable[0].name, "public-ch");
}

TEST_F(DatabaseTest, ListAllChannels) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  db_->create_channel("alpha", "", false, alice.id, {alice.id});
  db_->create_channel("beta", "", false, bob.id, {bob.id});
  // DM channels should be excluded
  db_->create_channel("dm", "", true, alice.id, {alice.id, bob.id}, false);

  auto all = db_->list_all_channels(100, 0);
  EXPECT_EQ(all.size(), 2u);
  // Ordered by name
  EXPECT_EQ(all[0].name, "alpha");
  EXPECT_EQ(all[1].name, "beta");
}

TEST_F(DatabaseTest, ListAllChannelsEmpty) {
  auto all = db_->list_all_channels(100, 0);
  EXPECT_TRUE(all.empty());
}

TEST_F(DatabaseTest, ListAllChannelsPagination) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  db_->create_channel("alpha", "", false, alice.id, {alice.id});
  db_->create_channel("beta", "", false, alice.id, {alice.id});
  db_->create_channel("gamma", "", false, alice.id, {alice.id});
  db_->create_channel("delta", "", false, alice.id, {alice.id});

  auto page1 = db_->list_all_channels(2, 0);
  ASSERT_EQ(page1.size(), 2u);
  EXPECT_EQ(page1[0].name, "alpha");
  EXPECT_EQ(page1[1].name, "beta");

  auto page2 = db_->list_all_channels(2, 2);
  ASSERT_EQ(page2.size(), 2u);
  EXPECT_EQ(page2[0].name, "delta");
  EXPECT_EQ(page2[1].name, "gamma");

  auto past_end = db_->list_all_channels(2, 100);
  EXPECT_TRUE(past_end.empty());
}

TEST_F(DatabaseTest, ListUserSpaces) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  // create_space automatically adds creator as member
  db_->create_space("Space1", "", true, alice.id);
  db_->create_space("Space2", "", true, alice.id);
  db_->create_space("Space3", "", true, bob.id);

  auto alice_spaces = db_->list_user_spaces(alice.id);
  EXPECT_EQ(alice_spaces.size(), 2u);

  auto bob_spaces = db_->list_user_spaces(bob.id);
  EXPECT_EQ(bob_spaces.size(), 1u);
}

TEST_F(DatabaseTest, ListUserSpacesEmpty) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto spaces = db_->list_user_spaces(alice.id);
  EXPECT_TRUE(spaces.empty());
}

TEST_F(DatabaseTest, ListPublicSpaces) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  // Alice is a member of these (via creation)
  db_->create_space("AliceSpace", "", true, alice.id);
  // Bob creates public spaces alice can browse
  db_->create_space("PublicBob", "Bob's public space", true, bob.id);
  // Bob creates a private space
  db_->create_space("PrivateBob", "", false, bob.id);

  auto public_spaces = db_->list_public_spaces(alice.id);
  EXPECT_EQ(public_spaces.size(), 1u);
  EXPECT_EQ(public_spaces[0].name, "PublicBob");
}

TEST_F(DatabaseTest, ListPublicSpacesWithSearch) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  db_->create_space("Frontend Team", "React developers", true, bob.id);
  db_->create_space("Backend Team", "C++ developers", true, bob.id);
  db_->create_space("DevOps", "Infrastructure", true, bob.id);

  // Search by name
  auto results = db_->list_public_spaces(alice.id, "Front");
  EXPECT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].name, "Frontend Team");

  // Search by description
  auto results2 = db_->list_public_spaces(alice.id, "Infrastructure");
  EXPECT_EQ(results2.size(), 1u);
  EXPECT_EQ(results2[0].name, "DevOps");

  // No matches
  auto results3 = db_->list_public_spaces(alice.id, "nonexistent");
  EXPECT_TRUE(results3.empty());
}

TEST_F(DatabaseTest, ListPublicSpacesExcludesJoined) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  auto space = db_->create_space("Shared", "", true, bob.id);
  db_->add_space_member(space.id, alice.id);

  auto public_spaces = db_->list_public_spaces(alice.id);
  EXPECT_TRUE(public_spaces.empty());
}

TEST_F(DatabaseTest, ListAllSpaces) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  db_->create_space("Alpha", "", true, alice.id);
  db_->create_space("Beta", "", false, bob.id);
  db_->create_space("Gamma", "", true, bob.id);

  auto all = db_->list_all_spaces(100, 0);
  EXPECT_EQ(all.size(), 3u);
  // Ordered by name
  EXPECT_EQ(all[0].name, "Alpha");
  EXPECT_EQ(all[1].name, "Beta");
  EXPECT_EQ(all[2].name, "Gamma");
}

TEST_F(DatabaseTest, ListAllSpacesEmpty) {
  auto all = db_->list_all_spaces(100, 0);
  EXPECT_TRUE(all.empty());
}

TEST_F(DatabaseTest, ListAllSpacesPagination) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  db_->create_space("Alpha", "", true, alice.id);
  db_->create_space("Beta", "", true, alice.id);
  db_->create_space("Delta", "", true, alice.id);
  db_->create_space("Gamma", "", true, alice.id);

  auto page1 = db_->list_all_spaces(2, 0);
  ASSERT_EQ(page1.size(), 2u);
  EXPECT_EQ(page1[0].name, "Alpha");
  EXPECT_EQ(page1[1].name, "Beta");

  auto page2 = db_->list_all_spaces(2, 2);
  ASSERT_EQ(page2.size(), 2u);
  EXPECT_EQ(page2[0].name, "Delta");
  EXPECT_EQ(page2[1].name, "Gamma");
}

TEST_F(DatabaseTest, UpdateUserRole) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  EXPECT_EQ(alice.role, "user");

  db_->update_user_role(alice.id, "admin");
  auto found = db_->find_user_by_id(alice.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->role, "admin");

  // Change back to user
  db_->update_user_role(alice.id, "user");
  found = db_->find_user_by_id(alice.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->role, "user");
}

TEST_F(DatabaseTest, AddConversationMember) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");
  auto charlie = db_->create_user("charlie", "Charlie", "KEY_C");

  auto convo = db_->create_conversation(alice.id, {alice.id, bob.id}, "group chat");
  EXPECT_TRUE(db_->is_channel_member(convo.id, alice.id));
  EXPECT_TRUE(db_->is_channel_member(convo.id, bob.id));
  EXPECT_FALSE(db_->is_channel_member(convo.id, charlie.id));

  // Add charlie to the conversation
  db_->add_conversation_member(convo.id, charlie.id);
  EXPECT_TRUE(db_->is_channel_member(convo.id, charlie.id));

  // Verify the added member has "write" role
  EXPECT_EQ(db_->get_member_role(convo.id, charlie.id), "write");
}

TEST_F(DatabaseTest, AddConversationMemberIdempotent) {
  auto alice = db_->create_user("alice", "Alice", "KEY_A");
  auto bob = db_->create_user("bob", "Bob", "KEY_B");

  auto convo = db_->create_conversation(alice.id, {alice.id, bob.id});

  // Adding an existing member should not throw
  EXPECT_NO_THROW(db_->add_conversation_member(convo.id, bob.id));

  // Bob should still be a member
  EXPECT_TRUE(db_->is_channel_member(convo.id, bob.id));
}
