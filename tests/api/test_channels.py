"""Tests for channel endpoints: CRUD, membership, messages."""

import pytest
from conftest import auth_header, pki_register


class TestChannelCreation:
    def test_create_public_channel(self, client, admin_user):
        r = client.post("/api/channels", json={
            "name": "general",
            "description": "General chat",
            "is_public": True,
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        ch = r.json()
        assert ch["name"] == "general"
        assert ch["is_public"] is True
        assert ch["my_role"] == "admin"

    def test_create_private_channel(self, client, admin_user):
        r = client.post("/api/channels", json={
            "name": "secret",
            "is_public": False,
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["is_public"] is False

    def test_create_channel_with_members(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={
            "name": "team",
            "member_ids": [regular_user["user"]["id"]],
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        members = r.json().get("members", [])
        member_ids = [m["id"] for m in members]
        assert regular_user["user"]["id"] in member_ids

    def test_create_channel_unauthenticated(self, client):
        r = client.post("/api/channels", json={"name": "test"})
        assert r.status_code == 401

    def test_create_channel_with_default_role(self, client, admin_user):
        r = client.post("/api/channels", json={
            "name": "readonly",
            "default_role": "read",
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["default_role"] == "read"


class TestChannelListing:
    def test_list_own_channels(self, client, admin_user):
        client.post("/api/channels", json={"name": "ch1"},
                    headers=admin_user["headers"])
        r = client.get("/api/channels", headers=admin_user["headers"])
        assert r.status_code == 200
        names = [ch["name"] for ch in r.json()]
        assert "ch1" in names

    def test_list_public_channels(self, client, admin_user, regular_user):
        client.post("/api/channels", json={"name": "pub1", "is_public": True},
                    headers=admin_user["headers"])
        r = client.get("/api/channels/public", headers=regular_user["headers"])
        assert r.status_code == 200
        names = [ch["name"] for ch in r.json()]
        assert "pub1" in names

    def test_private_channel_not_in_public_list(self, client, admin_user, regular_user):
        client.post("/api/channels", json={"name": "priv1", "is_public": False},
                    headers=admin_user["headers"])
        r = client.get("/api/channels/public", headers=regular_user["headers"])
        names = [ch["name"] for ch in r.json()]
        assert "priv1" not in names


class TestChannelJoinLeave:
    def test_join_public_channel(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "open"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.post(f"/api/channels/{ch_id}/join",
                        headers=regular_user["headers"])
        assert r.status_code == 200

    def test_cannot_join_private_channel(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "priv", "is_public": False},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.post(f"/api/channels/{ch_id}/join",
                        headers=regular_user["headers"])
        assert r.status_code == 403

    def test_leave_channel(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "leaveme"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/channels/{ch_id}/leave",
                        headers=regular_user["headers"])
        assert r.status_code == 200

    def test_last_admin_cannot_leave(self, client, regular_user):
        # Regular user creates channel (becomes sole admin)
        r = client.post("/api/channels", json={"name": "myonly"},
                        headers=regular_user["headers"])
        ch_id = r.json()["id"]
        r = client.post(f"/api/channels/{ch_id}/leave",
                        headers=regular_user["headers"])
        assert r.status_code == 400
        assert r.json().get("last_admin") is True


class TestChannelMembership:
    def test_add_member(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "team"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        assert r.status_code == 200

    def test_non_admin_cannot_add_member(self, client, admin_user, regular_user,
                                         second_regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        # Add regular_user as non-admin member
        client.post(f"/api/channels/{ch_id}/join",
                    headers=regular_user["headers"])
        # regular_user tries to add second_regular_user
        r = client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": second_regular_user["user"]["id"],
        }, headers=regular_user["headers"])
        assert r.status_code == 403

    def test_update_member_role(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        r = client.put(
            f"/api/channels/{ch_id}/members/{regular_user['user']['id']}",
            json={"role": "admin"},
            headers=admin_user["headers"],
        )
        assert r.status_code == 200

    def test_remove_member(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        r = client.delete(
            f"/api/channels/{ch_id}/members/{regular_user['user']['id']}",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200


class TestChannelUpdate:
    def test_admin_can_update_channel(self, client, admin_user):
        r = client.post("/api/channels", json={"name": "old"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.put(f"/api/channels/{ch_id}", json={
            "name": "new",
            "description": "updated",
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["name"] == "new"

    def test_non_admin_cannot_update(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/join",
                    headers=regular_user["headers"])
        r = client.put(f"/api/channels/{ch_id}", json={"name": "hacked"},
                       headers=regular_user["headers"])
        assert r.status_code == 403


class TestChannelMessages:
    def test_get_messages(self, client, admin_user):
        r = client.post("/api/channels", json={"name": "msgs"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.get(f"/api/channels/{ch_id}/messages",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        assert isinstance(r.json(), list)

    def test_non_member_cannot_read_messages(self, client, admin_user, regular_user):
        r = client.post("/api/channels",
                        json={"name": "priv", "is_public": False},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.get(f"/api/channels/{ch_id}/messages",
                       headers=regular_user["headers"])
        # Server admins can read all channels, so this checks regular user access
        # The admin_user is server admin so use regular user for the negative test
        # regular_user is not a member of this private channel
        assert r.status_code == 403


class TestDirectMessages:
    def test_create_dm(self, client, admin_user, regular_user):
        r = client.post("/api/channels/dm", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        dm = r.json()
        assert dm["is_direct"] is True

    def test_dm_idempotent(self, client, admin_user, regular_user):
        r1 = client.post("/api/channels/dm", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        r2 = client.post("/api/channels/dm", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        assert r1.json()["id"] == r2.json()["id"]


class TestChannelArchive:
    def test_admin_can_archive(self, client, admin_user):
        r = client.post("/api/channels", json={"name": "archiveme"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.post(f"/api/channels/{ch_id}/archive",
                        headers=admin_user["headers"])
        assert r.status_code == 200

    def test_admin_can_unarchive(self, client, admin_user):
        r = client.post("/api/channels", json={"name": "archiveme"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/archive",
                    headers=admin_user["headers"])
        r = client.post(f"/api/channels/{ch_id}/unarchive",
                        headers=admin_user["headers"])
        assert r.status_code == 200

    def test_non_admin_cannot_archive(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/channels/{ch_id}/archive",
                        headers=regular_user["headers"])
        assert r.status_code == 403
