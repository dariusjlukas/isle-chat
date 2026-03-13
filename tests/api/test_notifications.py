"""Tests for notification endpoints: list, mark read, mark all read."""

import psycopg2
from conftest import auth_header, pki_register, _get_pg_dsn


def _insert_notification(dbname, user_id, notif_type, source_user_id,
                         channel_id=None, message_id=None,
                         content="test notification", is_read=False):
    """Insert a notification directly into the DB and return its id."""
    conn = psycopg2.connect(_get_pg_dsn(dbname))
    conn.autocommit = True
    cur = conn.cursor()
    cur.execute(
        """INSERT INTO notifications (user_id, type, source_user_id, channel_id,
           message_id, content, is_read)
           VALUES (%s, %s, %s, %s, %s, %s, %s)
           RETURNING id::text""",
        (user_id, notif_type, source_user_id, channel_id, message_id,
         content, is_read),
    )
    notif_id = cur.fetchone()[0]
    cur.close()
    conn.close()
    return notif_id


class TestNotificationList:
    def test_list_empty(self, client, admin_user):
        r = client.get("/api/notifications", headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert data["notifications"] == []
        assert data["unread_count"] == 0

    def test_list_returns_notifications(self, client, admin_user, regular_user,
                                        worker_db):
        # Create a channel so we can reference it
        r = client.post("/api/channels", json={"name": "general"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
            channel_id=ch_id,
            content="@admin hello",
        )

        r = client.get("/api/notifications", headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert len(data["notifications"]) == 1
        assert data["unread_count"] == 1
        n = data["notifications"][0]
        assert n["type"] == "mention"
        assert n["source_username"] == "regular"
        assert n["content"] == "@admin hello"
        assert n["is_read"] is False
        assert n["channel_id"] == ch_id

    def test_list_respects_user_isolation(self, client, admin_user,
                                          regular_user, worker_db):
        """Each user should only see their own notifications."""
        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
            content="for admin",
        )
        _insert_notification(
            worker_db,
            regular_user["user"]["id"],
            "reply",
            admin_user["user"]["id"],
            content="for regular",
        )

        # Admin should see only their notification
        r = client.get("/api/notifications", headers=admin_user["headers"])
        data = r.json()
        assert len(data["notifications"]) == 1
        assert data["notifications"][0]["content"] == "for admin"

        # Regular user should see only their notification
        r = client.get("/api/notifications", headers=regular_user["headers"])
        data = r.json()
        assert len(data["notifications"]) == 1
        assert data["notifications"][0]["content"] == "for regular"

    def test_list_ordered_by_most_recent(self, client, admin_user,
                                         regular_user, worker_db):
        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
            content="first",
        )
        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "reply",
            regular_user["user"]["id"],
            content="second",
        )

        r = client.get("/api/notifications", headers=admin_user["headers"])
        items = r.json()["notifications"]
        assert len(items) == 2
        # Most recent first
        assert items[0]["content"] == "second"
        assert items[1]["content"] == "first"

    def test_list_includes_read_and_unread(self, client, admin_user,
                                           regular_user, worker_db):
        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
            content="unread one",
            is_read=False,
        )
        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "reply",
            regular_user["user"]["id"],
            content="read one",
            is_read=True,
        )

        r = client.get("/api/notifications", headers=admin_user["headers"])
        data = r.json()
        assert len(data["notifications"]) == 2
        assert data["unread_count"] == 1

    def test_list_with_limit_and_offset(self, client, admin_user,
                                        regular_user, worker_db):
        for i in range(5):
            _insert_notification(
                worker_db,
                admin_user["user"]["id"],
                "mention",
                regular_user["user"]["id"],
                content=f"notif {i}",
            )

        r = client.get("/api/notifications?limit=2&offset=0",
                        headers=admin_user["headers"])
        assert len(r.json()["notifications"]) == 2

        r = client.get("/api/notifications?limit=2&offset=3",
                        headers=admin_user["headers"])
        assert len(r.json()["notifications"]) == 2

    def test_list_unauthenticated(self, client):
        r = client.get("/api/notifications")
        assert r.status_code == 401


class TestMarkNotificationRead:
    def test_mark_single_read(self, client, admin_user, regular_user,
                               worker_db):
        nid = _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
        )

        r = client.post(f"/api/notifications/{nid}/read",
                         headers=admin_user["headers"])
        assert r.status_code == 200

        # Verify it's now read
        r = client.get("/api/notifications", headers=admin_user["headers"])
        data = r.json()
        assert data["unread_count"] == 0
        assert data["notifications"][0]["is_read"] is True

    def test_mark_read_only_affects_own(self, client, admin_user,
                                         regular_user, worker_db):
        """A user cannot mark another user's notification as read."""
        nid = _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
        )

        # Regular user tries to mark admin's notification
        r = client.post(f"/api/notifications/{nid}/read",
                         headers=regular_user["headers"])
        assert r.status_code == 200  # Succeeds silently but doesn't affect admin

        # Admin's notification should still be unread
        r = client.get("/api/notifications", headers=admin_user["headers"])
        assert r.json()["unread_count"] == 1

    def test_mark_read_unauthenticated(self, client):
        r = client.post("/api/notifications/some-id/read")
        assert r.status_code == 401


class TestMarkAllRead:
    def test_mark_all_read(self, client, admin_user, regular_user, worker_db):
        for i in range(3):
            _insert_notification(
                worker_db,
                admin_user["user"]["id"],
                "mention",
                regular_user["user"]["id"],
                content=f"notif {i}",
            )

        r = client.get("/api/notifications", headers=admin_user["headers"])
        assert r.json()["unread_count"] == 3

        r = client.post("/api/notifications/read-all",
                         headers=admin_user["headers"])
        assert r.status_code == 200

        r = client.get("/api/notifications", headers=admin_user["headers"])
        data = r.json()
        assert data["unread_count"] == 0
        assert all(n["is_read"] for n in data["notifications"])

    def test_mark_all_read_only_affects_own(self, client, admin_user,
                                             regular_user, worker_db):
        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
        )
        _insert_notification(
            worker_db,
            regular_user["user"]["id"],
            "mention",
            admin_user["user"]["id"],
        )

        # Admin marks all their notifications read
        client.post("/api/notifications/read-all",
                     headers=admin_user["headers"])

        # Regular user's notifications should be untouched
        r = client.get("/api/notifications", headers=regular_user["headers"])
        assert r.json()["unread_count"] == 1

    def test_mark_all_read_unauthenticated(self, client):
        r = client.post("/api/notifications/read-all")
        assert r.status_code == 401


class TestNotificationFields:
    def test_notification_has_channel_name(self, client, admin_user,
                                           regular_user, worker_db):
        r = client.post("/api/channels", json={"name": "test-channel"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "mention",
            regular_user["user"]["id"],
            channel_id=ch_id,
        )

        r = client.get("/api/notifications", headers=admin_user["headers"])
        n = r.json()["notifications"][0]
        assert n["channel_name"] == "test-channel"

    def test_reply_notification_type(self, client, admin_user, regular_user,
                                     worker_db):
        _insert_notification(
            worker_db,
            admin_user["user"]["id"],
            "reply",
            regular_user["user"]["id"],
            content="replied to your message",
        )

        r = client.get("/api/notifications", headers=admin_user["headers"])
        n = r.json()["notifications"][0]
        assert n["type"] == "reply"
        assert n["source_user_id"] == regular_user["user"]["id"]
