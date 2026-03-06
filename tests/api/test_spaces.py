"""Tests for space endpoints: CRUD, membership, space channels."""

import pytest
from conftest import pki_register


class TestSpaceCreation:
    def test_create_public_space(self, client, admin_user):
        r = client.post("/api/spaces", json={
            "name": "Engineering",
            "description": "Engineering team",
            "is_public": True,
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        sp = r.json()
        assert sp["name"] == "Engineering"
        assert sp["is_public"] is True
        assert sp["my_role"] == "owner"

    def test_create_private_space(self, client, admin_user):
        r = client.post("/api/spaces", json={
            "name": "Secret",
            "is_public": False,
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["is_public"] is False

    def test_create_space_unauthenticated(self, client):
        r = client.post("/api/spaces", json={"name": "test"})
        assert r.status_code == 401


class TestSpaceListing:
    def test_list_own_spaces(self, client, admin_user):
        client.post("/api/spaces", json={"name": "MySpace"},
                    headers=admin_user["headers"])
        r = client.get("/api/spaces", headers=admin_user["headers"])
        assert r.status_code == 200
        names = [s["name"] for s in r.json()]
        assert "MySpace" in names

    def test_list_public_spaces(self, client, admin_user, regular_user):
        client.post("/api/spaces", json={"name": "Public1", "is_public": True},
                    headers=admin_user["headers"])
        r = client.get("/api/spaces/public", headers=regular_user["headers"])
        assert r.status_code == 200
        names = [s["name"] for s in r.json()]
        assert "Public1" in names

    def test_private_space_hidden_from_public(self, client, admin_user, regular_user):
        client.post("/api/spaces", json={"name": "Hidden", "is_public": False},
                    headers=admin_user["headers"])
        r = client.get("/api/spaces/public", headers=regular_user["headers"])
        names = [s["name"] for s in r.json()]
        assert "Hidden" not in names


class TestSpaceJoinLeave:
    def test_join_public_space(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Open"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/join",
                        headers=regular_user["headers"])
        assert r.status_code == 200

    def test_cannot_join_private_space(self, client, admin_user, regular_user):
        r = client.post("/api/spaces",
                        json={"name": "Private", "is_public": False},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/join",
                        headers=regular_user["headers"])
        assert r.status_code == 403

    def test_leave_space(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Leavable"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/leave",
                        headers=regular_user["headers"])
        assert r.status_code == 200

    def test_last_owner_cannot_leave(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Mine"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/leave",
                        headers=admin_user["headers"])
        assert r.status_code == 400
        assert r.json().get("last_owner") is True


class TestSpaceMembership:
    def test_invite_user_to_space(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/members", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        assert r.status_code == 200

    def test_non_admin_cannot_invite(self, client, admin_user, regular_user,
                                      second_regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/members", json={
            "user_id": second_regular_user["user"]["id"],
        }, headers=regular_user["headers"])
        assert r.status_code == 403

    def test_update_member_role(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.put(
            f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
            json={"role": "admin"},
            headers=admin_user["headers"],
        )
        assert r.status_code == 200

    def test_only_owner_can_promote_to_owner(self, client, admin_user, regular_user,
                                              second_regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        # Add regular_user as admin
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        client.put(
            f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
            json={"role": "admin"},
            headers=admin_user["headers"],
        )
        # Add second user
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=second_regular_user["headers"])
        # Admin tries to promote to owner - should fail
        r = client.put(
            f"/api/spaces/{sp_id}/members/{second_regular_user['user']['id']}",
            json={"role": "owner"},
            headers=regular_user["headers"],
        )
        assert r.status_code == 403

    def test_remove_space_member(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.delete(
            f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200


class TestSpaceUpdate:
    def test_owner_can_update(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Old"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.put(f"/api/spaces/{sp_id}", json={
            "name": "New",
            "description": "Updated",
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["name"] == "New"

    def test_non_admin_cannot_update(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.put(f"/api/spaces/{sp_id}", json={"name": "Hacked"},
                       headers=regular_user["headers"])
        assert r.status_code == 403


class TestSpaceChannels:
    def test_create_channel_in_space(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/channels", json={
            "name": "general",
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["name"] == "general"

    def test_list_space_channels(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/channels",
                    json={"name": "ch1"}, headers=admin_user["headers"])
        r = client.get(f"/api/spaces/{sp_id}/channels",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        names = [ch["name"] for ch in r.json()]
        assert "ch1" in names

    def test_non_admin_cannot_create_space_channel(self, client, admin_user,
                                                     regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "hacked"},
                        headers=regular_user["headers"])
        assert r.status_code == 403


class TestSpaceArchive:
    def test_owner_can_archive(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Old"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/archive",
                        headers=admin_user["headers"])
        assert r.status_code == 200

    def test_owner_can_unarchive(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Old"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/archive",
                    headers=admin_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/unarchive",
                        headers=admin_user["headers"])
        assert r.status_code == 200

    def test_non_owner_cannot_archive(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/archive",
                        headers=regular_user["headers"])
        assert r.status_code == 403
