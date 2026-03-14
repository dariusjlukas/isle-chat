"""Tests for calendar endpoints: CRUD, recurrence, exceptions, RSVP, permissions."""

from conftest import pki_register, auth_header


def _create_space(client, headers, name="TestSpace"):
    """Helper to create a space and enable the calendar tool."""
    r = client.post("/api/spaces", json={"name": name}, headers=headers)
    assert r.status_code == 200
    sp = r.json()
    # Enable the calendar tool
    client.put(f"/api/spaces/{sp['id']}/tools",
               json={"tool": "calendar", "enabled": True}, headers=headers)
    return sp


def _add_member_with_role(client, space_id, admin_headers, user_info, role):
    """Invite a user to a space and accept the invite so they become a member."""
    r = client.post(f"/api/spaces/{space_id}/members",
                    json={"user_id": user_info["user"]["id"], "role": role},
                    headers=admin_headers)
    assert r.status_code == 200, f"Failed to invite: {r.text}"
    # Accept the invite
    r = client.get("/api/space-invites", headers=user_info["headers"])
    assert r.status_code == 200
    invites = r.json()
    invite = next(i for i in invites if i["space_id"] == space_id)
    r = client.post(f"/api/space-invites/{invite['id']}/accept",
                    headers=user_info["headers"])
    assert r.status_code == 200


def _create_event(client, space_id, headers, title="Test Event",
                  start="2026-03-15T10:00:00Z", end="2026-03-15T11:00:00Z",
                  **kwargs):
    """Helper to create a calendar event."""
    body = {
        "title": title,
        "start_time": start,
        "end_time": end,
        **kwargs,
    }
    return client.post(f"/api/spaces/{space_id}/calendar/events",
                       json=body, headers=headers)


class TestCalendarEventsCRUD:
    def test_create_event(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Team Meeting", description="Weekly sync",
                          location="Room 101", color="blue")
        assert r.status_code == 200
        event = r.json()
        assert event["title"] == "Team Meeting"
        assert event["description"] == "Weekly sync"
        assert event["location"] == "Room 101"
        assert event["color"] == "blue"
        assert event["all_day"] is False
        assert "id" in event

    def test_create_all_day_event(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Holiday", all_day=True,
                          start="2026-12-25T00:00:00Z",
                          end="2026-12-25T23:59:59Z")
        assert r.status_code == 200
        assert r.json()["all_day"] is True

    def test_create_recurring_event(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Daily Standup",
                          rrule="FREQ=DAILY;COUNT=5")
        assert r.status_code == 200
        assert r.json()["rrule"] == "FREQ=DAILY;COUNT=5"

    def test_create_event_empty_title_rejected(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"], title="")
        assert r.status_code == 400

    def test_create_event_long_title_rejected(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="x" * 256)
        assert r.status_code == 400

    def test_get_single_event(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Get Me")
        event_id = r.json()["id"]

        r = client.get(f"/api/spaces/{sp['id']}/calendar/events/{event_id}",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert data["title"] == "Get Me"
        assert "my_rsvp" in data
        assert "my_permission" in data

    def test_list_events(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _create_event(client, sp["id"], admin_user["headers"],
                      title="A", start="2026-03-10T10:00:00Z",
                      end="2026-03-10T11:00:00Z")
        _create_event(client, sp["id"], admin_user["headers"],
                      title="B", start="2026-03-20T10:00:00Z",
                      end="2026-03-20T11:00:00Z")
        # Outside range
        _create_event(client, sp["id"], admin_user["headers"],
                      title="C", start="2026-04-15T10:00:00Z",
                      end="2026-04-15T11:00:00Z")

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01T00:00:00Z",
                    "end": "2026-03-31T23:59:59Z"},
            headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert len(data["events"]) == 2
        assert "my_permission" in data

    def test_list_events_requires_range(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = client.get(f"/api/spaces/{sp['id']}/calendar/events",
                       headers=admin_user["headers"])
        assert r.status_code == 400

    def test_update_event(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Old Title")
        event_id = r.json()["id"]

        r = client.put(f"/api/spaces/{sp['id']}/calendar/events/{event_id}",
                       json={"title": "New Title", "color": "red",
                             "location": "Room 42"},
                       headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["title"] == "New Title"
        assert r.json()["color"] == "red"
        assert r.json()["location"] == "Room 42"

    def test_delete_event(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"])
        event_id = r.json()["id"]

        r = client.delete(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}",
            headers=admin_user["headers"])
        assert r.status_code == 200

        # Should not appear in listing
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01T00:00:00Z",
                    "end": "2026-03-31T23:59:59Z"},
            headers=admin_user["headers"])
        assert len(r.json()["events"]) == 0

    def test_non_member_cannot_access(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"], "Private")
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01", "end": "2026-03-31"},
            headers=regular_user["headers"])
        assert r.status_code == 403

    def test_event_not_found(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events/00000000-0000-0000-0000-000000000000",
            headers=admin_user["headers"])
        assert r.status_code == 404


class TestCalendarRecurrence:
    def test_recurring_events_expanded(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _create_event(client, sp["id"], admin_user["headers"],
                      title="Daily", rrule="FREQ=DAILY;COUNT=5",
                      start="2026-03-10T09:00:00Z",
                      end="2026-03-10T10:00:00Z")

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01T00:00:00Z",
                    "end": "2026-03-31T23:59:59Z"},
            headers=admin_user["headers"])
        assert r.status_code == 200
        events = r.json()["events"]
        assert len(events) == 5

        # Each occurrence should have occurrence_date set
        for ev in events:
            assert ev["occurrence_date"] is not None
            assert ev["title"] == "Daily"

    def test_recurring_event_preserves_duration(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _create_event(client, sp["id"], admin_user["headers"],
                      title="Meeting", rrule="FREQ=DAILY;COUNT=2",
                      start="2026-03-10T10:00:00Z",
                      end="2026-03-10T11:30:00Z")

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-10T00:00:00Z",
                    "end": "2026-03-12T00:00:00Z"},
            headers=admin_user["headers"])
        events = r.json()["events"]
        assert len(events) == 2

        # Second occurrence should have same 1.5h duration
        assert events[1]["start_time"] == "2026-03-11T10:00:00Z"
        assert events[1]["end_time"] == "2026-03-11T11:30:00Z"

    def test_weekly_with_byday(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        # Monday March 2, 2026
        _create_event(client, sp["id"], admin_user["headers"],
                      title="MWF Class", rrule="FREQ=WEEKLY;BYDAY=MO,WE,FR",
                      start="2026-03-02T09:00:00Z",
                      end="2026-03-02T10:00:00Z")

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-02T00:00:00Z",
                    "end": "2026-03-09T00:00:00Z"},
            headers=admin_user["headers"])
        events = r.json()["events"]
        # Mon(2), Wed(4), Fri(6)
        assert len(events) == 3

    def test_events_sorted_by_start_time(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _create_event(client, sp["id"], admin_user["headers"],
                      title="Later", start="2026-03-15T14:00:00Z",
                      end="2026-03-15T15:00:00Z")
        _create_event(client, sp["id"], admin_user["headers"],
                      title="Earlier", start="2026-03-15T08:00:00Z",
                      end="2026-03-15T09:00:00Z")

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-15T00:00:00Z",
                    "end": "2026-03-16T00:00:00Z"},
            headers=admin_user["headers"])
        events = r.json()["events"]
        assert events[0]["title"] == "Earlier"
        assert events[1]["title"] == "Later"


class TestCalendarExceptions:
    def test_delete_single_occurrence(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Weekly", rrule="FREQ=WEEKLY;COUNT=3",
                          start="2026-03-02T09:00:00Z",
                          end="2026-03-02T10:00:00Z")
        event_id = r.json()["id"]

        # Delete the second occurrence (March 9)
        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/exception",
            json={"original_date": "2026-03-09T09:00:00Z",
                  "is_deleted": True},
            headers=admin_user["headers"])
        assert r.status_code == 200

        # List should show only 2 occurrences
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01T00:00:00Z",
                    "end": "2026-03-31T23:59:59Z"},
            headers=admin_user["headers"])
        events = r.json()["events"]
        assert len(events) == 2

    def test_modify_single_occurrence(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Weekly", rrule="FREQ=WEEKLY;COUNT=3",
                          start="2026-03-02T09:00:00Z",
                          end="2026-03-02T10:00:00Z")
        event_id = r.json()["id"]

        # Modify the second occurrence
        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/exception",
            json={"original_date": "2026-03-09T09:00:00Z",
                  "title": "Modified Meeting",
                  "color": "red"},
            headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["is_deleted"] is False

        # List should show modified title for second occurrence
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01T00:00:00Z",
                    "end": "2026-03-31T23:59:59Z"},
            headers=admin_user["headers"])
        events = r.json()["events"]
        assert len(events) == 3
        modified = [e for e in events if e.get("is_exception")]
        assert len(modified) == 1
        assert modified[0]["title"] == "Modified Meeting"
        assert modified[0]["color"] == "red"

    def test_exception_on_non_recurring_rejected(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Single")
        event_id = r.json()["id"]

        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/exception",
            json={"original_date": "2026-03-15T10:00:00Z",
                  "is_deleted": True},
            headers=admin_user["headers"])
        assert r.status_code == 400


class TestCalendarRSVP:
    def test_set_rsvp(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Party")
        event_id = r.json()["id"]

        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "yes"},
            headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["status"] == "yes"

    def test_change_rsvp(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Party")
        event_id = r.json()["id"]

        client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "yes"},
            headers=admin_user["headers"])
        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "no"},
            headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["status"] == "no"

    def test_rsvp_reflected_in_event_listing(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Party")
        event_id = r.json()["id"]

        client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "maybe"},
            headers=admin_user["headers"])

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01T00:00:00Z",
                    "end": "2026-03-31T23:59:59Z"},
            headers=admin_user["headers"])
        events = r.json()["events"]
        assert events[0]["my_rsvp"] == "maybe"

    def test_get_rsvps(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _create_event(client, sp["id"], admin_user["headers"],
                          title="Party")
        event_id = r.json()["id"]

        client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "yes"},
            headers=admin_user["headers"])
        client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "no"},
            headers=regular_user["headers"])

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvps",
            headers=admin_user["headers"])
        assert r.status_code == 200
        rsvps = r.json()["rsvps"]
        assert len(rsvps) == 2

    def test_invalid_rsvp_status_rejected(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"])
        event_id = r.json()["id"]

        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "invalid"},
            headers=admin_user["headers"])
        assert r.status_code == 400

    def test_rsvp_maybe(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _create_event(client, sp["id"], admin_user["headers"])
        event_id = r.json()["id"]

        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}/rsvp",
            json={"status": "maybe"},
            headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["status"] == "maybe"


class TestCalendarPermissions:
    def test_owner_gets_owner_permission(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01", "end": "2026-03-31"},
            headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["my_permission"] == "owner"

    def test_write_member_gets_edit(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01", "end": "2026-03-31"},
            headers=regular_user["headers"])
        assert r.status_code == 200
        assert r.json()["my_permission"] == "edit"

    def test_read_member_gets_view(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        _add_member_with_role(client, sp["id"], admin_user["headers"],
                              regular_user, "read")

        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01", "end": "2026-03-31"},
            headers=regular_user["headers"])
        assert r.status_code == 200
        assert r.json()["my_permission"] == "view"

    def test_view_user_cannot_create_event(self, client, admin_user,
                                            regular_user):
        sp = _create_space(client, admin_user["headers"])
        _add_member_with_role(client, sp["id"], admin_user["headers"],
                              regular_user, "read")

        r = _create_event(client, sp["id"], regular_user["headers"])
        assert r.status_code == 403

    def test_view_user_cannot_update_event(self, client, admin_user,
                                            regular_user):
        sp = _create_space(client, admin_user["headers"])
        _add_member_with_role(client, sp["id"], admin_user["headers"],
                              regular_user, "read")

        r = _create_event(client, sp["id"], admin_user["headers"])
        event_id = r.json()["id"]

        r = client.put(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}",
            json={"title": "Hacked"},
            headers=regular_user["headers"])
        assert r.status_code == 403

    def test_edit_user_can_create_event(self, client, admin_user,
                                        regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _create_event(client, sp["id"], regular_user["headers"],
                          title="User Event")
        assert r.status_code == 200

    def test_creator_can_delete_own_event(self, client, admin_user,
                                          regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _create_event(client, sp["id"], regular_user["headers"])
        event_id = r.json()["id"]

        r = client.delete(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}",
            headers=regular_user["headers"])
        assert r.status_code == 200

    def test_non_owner_cannot_delete_others_event(self, client, admin_user,
                                                   regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _create_event(client, sp["id"], admin_user["headers"])
        event_id = r.json()["id"]

        # Regular user with "edit" cannot delete admin's event
        r = client.delete(
            f"/api/spaces/{sp['id']}/calendar/events/{event_id}",
            headers=regular_user["headers"])
        assert r.status_code == 403

    def test_permission_escalation(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        # Add as read member
        _add_member_with_role(client, sp["id"], admin_user["headers"],
                              regular_user, "read")

        # Without escalation: view
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01", "end": "2026-03-31"},
            headers=regular_user["headers"])
        assert r.json()["my_permission"] == "view"

        # Escalate to edit
        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/permissions",
            json={"user_id": regular_user["user"]["id"],
                  "permission": "edit"},
            headers=admin_user["headers"])
        assert r.status_code == 200

        # Now should have edit
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01", "end": "2026-03-31"},
            headers=regular_user["headers"])
        assert r.json()["my_permission"] == "edit"

    def test_get_permissions(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        _add_member_with_role(client, sp["id"], admin_user["headers"],
                              regular_user, "read")
        client.post(
            f"/api/spaces/{sp['id']}/calendar/permissions",
            json={"user_id": regular_user["user"]["id"],
                  "permission": "edit"},
            headers=admin_user["headers"])

        r = client.get(f"/api/spaces/{sp['id']}/calendar/permissions",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert "permissions" in data
        assert "my_permission" in data
        assert len(data["permissions"]) == 1
        assert data["permissions"][0]["permission"] == "edit"

    def test_remove_permission(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        _add_member_with_role(client, sp["id"], admin_user["headers"],
                              regular_user, "read")
        client.post(
            f"/api/spaces/{sp['id']}/calendar/permissions",
            json={"user_id": regular_user["user"]["id"],
                  "permission": "edit"},
            headers=admin_user["headers"])

        r = client.delete(
            f"/api/spaces/{sp['id']}/calendar/permissions/{regular_user['user']['id']}",
            headers=admin_user["headers"])
        assert r.status_code == 200

        # Should go back to view (base from read role)
        r = client.get(
            f"/api/spaces/{sp['id']}/calendar/events",
            params={"start": "2026-03-01", "end": "2026-03-31"},
            headers=regular_user["headers"])
        assert r.json()["my_permission"] == "view"

    def test_non_owner_cannot_set_permissions(self, client, admin_user,
                                               regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/permissions",
            json={"user_id": admin_user["user"]["id"],
                  "permission": "view"},
            headers=regular_user["headers"])
        assert r.status_code == 403

    def test_invalid_permission_level_rejected(self, client, admin_user,
                                                regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = client.post(
            f"/api/spaces/{sp['id']}/calendar/permissions",
            json={"user_id": regular_user["user"]["id"],
                  "permission": "superadmin"},
            headers=admin_user["headers"])
        assert r.status_code == 400
