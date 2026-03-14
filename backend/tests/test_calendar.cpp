#include <gtest/gtest.h>
#include "config.h"
#include "db/database.h"

class CalendarTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto config = Config::from_env();
        conn_string_ = config.pg_connection_string();
        db_ = std::make_unique<Database>(conn_string_);
        db_->run_migrations();
    }

    void SetUp() override {
        pqxx::connection conn(conn_string_);
        pqxx::work txn(conn);
        txn.exec("DELETE FROM calendar_event_rsvps");
        txn.exec("DELETE FROM calendar_event_exceptions");
        txn.exec("DELETE FROM calendar_permissions");
        txn.exec("DELETE FROM calendar_events");
        txn.exec("DELETE FROM space_file_versions");
        txn.exec("DELETE FROM space_file_permissions");
        txn.exec("DELETE FROM space_files");
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

    struct Setup {
        User user;
        Space space;
    };
    Setup create_user_and_space(const std::string& username = "alice") {
        auto user = db_->create_user(username, username, "KEY_" + username);
        auto space = db_->create_space("TestSpace", "desc", true, user.id, "write");
        return {user, space};
    }

    static std::unique_ptr<Database> db_;
    static std::string conn_string_;
};

std::unique_ptr<Database> CalendarTest::db_;
std::string CalendarTest::conn_string_;

// --- Event CRUD ---

TEST_F(CalendarTest, CreateEvent) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Team Meeting", "Weekly sync", "Room 101", "blue",
        "2026-03-15T10:00:00Z", "2026-03-15T11:00:00Z", false, "", user.id);

    EXPECT_FALSE(event.id.empty());
    EXPECT_EQ(event.title, "Team Meeting");
    EXPECT_EQ(event.description, "Weekly sync");
    EXPECT_EQ(event.location, "Room 101");
    EXPECT_EQ(event.color, "blue");
    EXPECT_EQ(event.space_id, space.id);
    EXPECT_EQ(event.created_by, user.id);
    EXPECT_FALSE(event.all_day);
    EXPECT_TRUE(event.rrule.empty());
}

TEST_F(CalendarTest, CreateAllDayEvent) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Company Holiday", "", "", "green",
        "2026-12-25T00:00:00Z", "2026-12-25T23:59:59Z", true, "", user.id);

    EXPECT_TRUE(event.all_day);
    EXPECT_EQ(event.title, "Company Holiday");
}

TEST_F(CalendarTest, CreateRecurringEvent) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Weekly Standup", "", "", "purple",
        "2026-03-02T09:00:00Z", "2026-03-02T09:30:00Z", false,
        "FREQ=WEEKLY;BYDAY=MO", user.id);

    EXPECT_EQ(event.rrule, "FREQ=WEEKLY;BYDAY=MO");
}

TEST_F(CalendarTest, FindEvent) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Test", "", "", "blue",
        "2026-03-15T10:00:00Z", "2026-03-15T11:00:00Z", false, "", user.id);

    auto found = db_->find_calendar_event(event.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->title, "Test");
    EXPECT_EQ(found->space_id, space.id);
}

TEST_F(CalendarTest, FindEventNotFound) {
    auto found = db_->find_calendar_event("00000000-0000-0000-0000-000000000000");
    EXPECT_FALSE(found.has_value());
}

TEST_F(CalendarTest, UpdateEvent) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Old Title", "old desc", "", "blue",
        "2026-03-15T10:00:00Z", "2026-03-15T11:00:00Z", false, "", user.id);

    auto updated = db_->update_calendar_event(
        event.id, "New Title", "new desc", "Room 42", "red",
        "2026-03-16T14:00:00Z", "2026-03-16T15:00:00Z", false, "");

    EXPECT_EQ(updated.title, "New Title");
    EXPECT_EQ(updated.description, "new desc");
    EXPECT_EQ(updated.location, "Room 42");
    EXPECT_EQ(updated.color, "red");
}

TEST_F(CalendarTest, DeleteEvent) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Delete Me", "", "", "blue",
        "2026-03-15T10:00:00Z", "2026-03-15T11:00:00Z", false, "", user.id);

    db_->delete_calendar_event(event.id);

    auto found = db_->find_calendar_event(event.id);
    EXPECT_FALSE(found.has_value());
}

TEST_F(CalendarTest, ListEventsInRange) {
    auto [user, space] = create_user_and_space();
    // Event in range
    db_->create_calendar_event(
        space.id, "In Range", "", "", "blue",
        "2026-03-15T10:00:00Z", "2026-03-15T11:00:00Z", false, "", user.id);
    // Event outside range
    db_->create_calendar_event(
        space.id, "Out of Range", "", "", "blue",
        "2026-04-15T10:00:00Z", "2026-04-15T11:00:00Z", false, "", user.id);

    auto events = db_->list_calendar_events(
        space.id, "2026-03-01T00:00:00Z", "2026-03-31T23:59:59Z");

    EXPECT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].title, "In Range");
}

TEST_F(CalendarTest, ListEventsIncludesRecurringBase) {
    auto [user, space] = create_user_and_space();
    // Recurring event starting before range — should be included because
    // the base start_time < range_end (handler will expand)
    db_->create_calendar_event(
        space.id, "Weekly", "", "", "blue",
        "2026-01-05T09:00:00Z", "2026-01-05T10:00:00Z", false,
        "FREQ=WEEKLY", user.id);

    auto events = db_->list_calendar_events(
        space.id, "2026-03-01T00:00:00Z", "2026-03-31T23:59:59Z");

    EXPECT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].title, "Weekly");
}

// --- Exceptions ---

TEST_F(CalendarTest, CreateException) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Weekly", "", "", "blue",
        "2026-03-02T09:00:00Z", "2026-03-02T10:00:00Z", false,
        "FREQ=WEEKLY", user.id);

    auto ex = db_->create_event_exception(
        event.id, "2026-03-09T09:00:00Z", false,
        "Modified Title", "", "", "red", "", "", false);

    EXPECT_FALSE(ex.id.empty());
    EXPECT_EQ(ex.event_id, event.id);
    EXPECT_EQ(ex.title, "Modified Title");
    EXPECT_FALSE(ex.is_deleted);
}

TEST_F(CalendarTest, CreateDeletionException) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Weekly", "", "", "blue",
        "2026-03-02T09:00:00Z", "2026-03-02T10:00:00Z", false,
        "FREQ=WEEKLY", user.id);

    auto ex = db_->create_event_exception(
        event.id, "2026-03-09T09:00:00Z", true,
        "", "", "", "", "", "", false);

    EXPECT_TRUE(ex.is_deleted);
}

TEST_F(CalendarTest, GetExceptions) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Weekly", "", "", "blue",
        "2026-03-02T09:00:00Z", "2026-03-02T10:00:00Z", false,
        "FREQ=WEEKLY", user.id);

    db_->create_event_exception(event.id, "2026-03-09T09:00:00Z", true,
                                 "", "", "", "", "", "", false);
    db_->create_event_exception(event.id, "2026-03-16T09:00:00Z", false,
                                 "Rescheduled", "", "", "", "", "", false);

    auto exceptions = db_->get_event_exceptions(event.id);
    EXPECT_EQ(exceptions.size(), 2u);
}

TEST_F(CalendarTest, DeleteException) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Weekly", "", "", "blue",
        "2026-03-02T09:00:00Z", "2026-03-02T10:00:00Z", false,
        "FREQ=WEEKLY", user.id);
    auto ex = db_->create_event_exception(event.id, "2026-03-09T09:00:00Z", true,
                                            "", "", "", "", "", "", false);

    db_->delete_event_exception(ex.id);
    auto exceptions = db_->get_event_exceptions(event.id);
    EXPECT_EQ(exceptions.size(), 0u);
}

// --- RSVP ---

TEST_F(CalendarTest, SetAndGetRsvp) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Party", "", "", "blue",
        "2026-03-20T18:00:00Z", "2026-03-20T22:00:00Z", false, "", user.id);

    db_->set_event_rsvp(event.id, user.id, "1970-01-01 00:00:00+00", "yes");

    auto rsvps = db_->get_event_rsvps(event.id, "1970-01-01 00:00:00+00");
    ASSERT_EQ(rsvps.size(), 1u);
    EXPECT_EQ(rsvps[0].user_id, user.id);
    EXPECT_EQ(rsvps[0].status, "yes");
}

TEST_F(CalendarTest, GetUserRsvp) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Party", "", "", "blue",
        "2026-03-20T18:00:00Z", "2026-03-20T22:00:00Z", false, "", user.id);

    db_->set_event_rsvp(event.id, user.id, "1970-01-01 00:00:00+00", "maybe");
    auto rsvp = db_->get_user_rsvp(event.id, user.id, "1970-01-01 00:00:00+00");
    ASSERT_TRUE(rsvp.has_value());
    EXPECT_EQ(*rsvp, "maybe");
}

TEST_F(CalendarTest, GetUserRsvpNotSet) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Party", "", "", "blue",
        "2026-03-20T18:00:00Z", "2026-03-20T22:00:00Z", false, "", user.id);

    auto rsvp = db_->get_user_rsvp(event.id, user.id, "1970-01-01 00:00:00+00");
    EXPECT_FALSE(rsvp.has_value());
}

TEST_F(CalendarTest, UpsertRsvp) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Meeting", "", "", "blue",
        "2026-03-20T10:00:00Z", "2026-03-20T11:00:00Z", false, "", user.id);

    db_->set_event_rsvp(event.id, user.id, "1970-01-01 00:00:00+00", "yes");
    db_->set_event_rsvp(event.id, user.id, "1970-01-01 00:00:00+00", "no");

    auto rsvp = db_->get_user_rsvp(event.id, user.id, "1970-01-01 00:00:00+00");
    ASSERT_TRUE(rsvp.has_value());
    EXPECT_EQ(*rsvp, "no");
}

TEST_F(CalendarTest, MultipleUserRsvps) {
    auto [alice, space] = create_user_and_space("alice");
    auto bob = db_->create_user("bob", "Bob", "KEY_BOB");
    db_->add_space_member(space.id, bob.id, "write");

    auto event = db_->create_calendar_event(
        space.id, "Team Lunch", "", "", "green",
        "2026-03-20T12:00:00Z", "2026-03-20T13:00:00Z", false, "", alice.id);

    db_->set_event_rsvp(event.id, alice.id, "1970-01-01 00:00:00+00", "yes");
    db_->set_event_rsvp(event.id, bob.id, "1970-01-01 00:00:00+00", "maybe");

    auto rsvps = db_->get_event_rsvps(event.id, "1970-01-01 00:00:00+00");
    EXPECT_EQ(rsvps.size(), 2u);
}

// --- Permissions ---

TEST_F(CalendarTest, SetAndGetPermission) {
    auto [alice, space] = create_user_and_space("alice");
    auto bob = db_->create_user("bob", "Bob", "KEY_BOB");
    db_->add_space_member(space.id, bob.id, "read");

    db_->set_calendar_permission(space.id, bob.id, "edit", alice.id);

    auto perm = db_->get_calendar_permission(space.id, bob.id);
    EXPECT_EQ(perm, "edit");
}

TEST_F(CalendarTest, UpsertPermission) {
    auto [alice, space] = create_user_and_space("alice");
    auto bob = db_->create_user("bob", "Bob", "KEY_BOB");
    db_->add_space_member(space.id, bob.id, "read");

    db_->set_calendar_permission(space.id, bob.id, "view", alice.id);
    db_->set_calendar_permission(space.id, bob.id, "owner", alice.id);

    auto perm = db_->get_calendar_permission(space.id, bob.id);
    EXPECT_EQ(perm, "owner");
}

TEST_F(CalendarTest, RemovePermission) {
    auto [alice, space] = create_user_and_space("alice");
    auto bob = db_->create_user("bob", "Bob", "KEY_BOB");
    db_->add_space_member(space.id, bob.id, "read");

    db_->set_calendar_permission(space.id, bob.id, "edit", alice.id);
    db_->remove_calendar_permission(space.id, bob.id);

    auto perm = db_->get_calendar_permission(space.id, bob.id);
    EXPECT_TRUE(perm.empty());
}

TEST_F(CalendarTest, GetAllPermissions) {
    auto [alice, space] = create_user_and_space("alice");
    auto bob = db_->create_user("bob", "Bob", "KEY_BOB");
    auto carol = db_->create_user("carol", "Carol", "KEY_CAROL");
    db_->add_space_member(space.id, bob.id, "read");
    db_->add_space_member(space.id, carol.id, "read");

    db_->set_calendar_permission(space.id, bob.id, "edit", alice.id);
    db_->set_calendar_permission(space.id, carol.id, "owner", alice.id);

    auto perms = db_->get_calendar_permissions(space.id);
    EXPECT_EQ(perms.size(), 2u);
}

TEST_F(CalendarTest, NoPermissionReturnsEmpty) {
    auto [alice, space] = create_user_and_space("alice");
    auto bob = db_->create_user("bob", "Bob", "KEY_BOB");

    auto perm = db_->get_calendar_permission(space.id, bob.id);
    EXPECT_TRUE(perm.empty());
}

// --- Cascade deletes ---

TEST_F(CalendarTest, DeleteEventCascadesExceptions) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Weekly", "", "", "blue",
        "2026-03-02T09:00:00Z", "2026-03-02T10:00:00Z", false,
        "FREQ=WEEKLY", user.id);
    db_->create_event_exception(event.id, "2026-03-09T09:00:00Z", true,
                                 "", "", "", "", "", "", false);

    db_->delete_calendar_event(event.id);

    // Exception should also be deleted via CASCADE
    auto exceptions = db_->get_event_exceptions(event.id);
    EXPECT_EQ(exceptions.size(), 0u);
}

TEST_F(CalendarTest, DeleteEventCascadesRsvps) {
    auto [user, space] = create_user_and_space();
    auto event = db_->create_calendar_event(
        space.id, "Party", "", "", "blue",
        "2026-03-20T18:00:00Z", "2026-03-20T22:00:00Z", false, "", user.id);
    db_->set_event_rsvp(event.id, user.id, "1970-01-01 00:00:00+00", "yes");

    db_->delete_calendar_event(event.id);

    auto rsvps = db_->get_event_rsvps(event.id, "1970-01-01 00:00:00+00");
    EXPECT_EQ(rsvps.size(), 0u);
}
