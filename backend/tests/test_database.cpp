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

    auto dm = db_->create_channel("dm", "", true, user_a.id,
                                   {user_a.id, user_b.id}, false);

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

TEST_F(DatabaseTest, FindGeneralChannel) {
    EXPECT_FALSE(db_->find_general_channel().has_value());

    auto user = db_->create_user("alice", "Alice", "KEY_A");
    db_->create_channel("general", "General", false, user.id, {user.id});

    auto found = db_->find_general_channel();
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "general");
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
