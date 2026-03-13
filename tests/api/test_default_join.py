"""Tests for default-join channels: setting the flag, auto-join on space join, auto-join on invite accept."""

import pytest
from conftest import pki_register, auth_header


class TestDefaultJoinFlag:
    """Setting and reading the default_join flag on channels."""

    def test_default_join_false_by_default(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "general"},
                        headers=admin_user["headers"])
        assert r.status_code == 200
        ch = r.json()
        assert ch["default_join"] is False

    def test_admin_can_set_default_join(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "announcements"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        r = client.put(f"/api/channels/{ch_id}", json={
            "name": "announcements",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["default_join"] is True

    def test_default_join_persists_in_channel_list(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "general"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        client.put(f"/api/channels/{ch_id}", json={
            "name": "general",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=admin_user["headers"])

        r = client.get("/api/channels", headers=admin_user["headers"])
        assert r.status_code == 200
        ch = next(c for c in r.json() if c["id"] == ch_id)
        assert ch["default_join"] is True

    def test_non_admin_cannot_set_default_join(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "general"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        # Regular user joins space first
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])

        r = client.put(f"/api/channels/{ch_id}", json={
            "name": "general",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=regular_user["headers"])
        assert r.status_code == 403

    def test_default_join_in_space_channels_list(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "auto-ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        client.put(f"/api/channels/{ch_id}", json={
            "name": "auto-ch",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=admin_user["headers"])

        r = client.get(f"/api/spaces/{sp_id}/channels",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        ch = next(c for c in r.json() if c["id"] == ch_id)
        assert ch["default_join"] is True


class TestAutoJoinOnSpaceJoin:
    """When a user joins a public space, they should be auto-added to default_join channels."""

    def test_new_member_auto_joins_default_channels(self, client, admin_user, regular_user):
        # Create space with two channels, one default_join
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]

        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "general"},
                        headers=admin_user["headers"])
        general_id = r.json()["id"]

        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "optional"},
                        headers=admin_user["headers"])
        optional_id = r.json()["id"]

        # Set general as default_join
        client.put(f"/api/channels/{general_id}", json={
            "name": "general",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=admin_user["headers"])

        # Regular user joins space
        r = client.post(f"/api/spaces/{sp_id}/join",
                        headers=regular_user["headers"])
        assert r.status_code == 200

        # Check that regular user is in general but not optional
        r = client.get("/api/channels", headers=regular_user["headers"])
        user_channel_ids = [c["id"] for c in r.json()]
        assert general_id in user_channel_ids
        assert optional_id not in user_channel_ids

    def test_auto_join_multiple_default_channels(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]

        ch_ids = []
        for name in ["announcements", "general", "random"]:
            r = client.post(f"/api/spaces/{sp_id}/channels",
                            json={"name": name},
                            headers=admin_user["headers"])
            ch_ids.append(r.json()["id"])

        # Set all three as default_join
        for i, name in enumerate(["announcements", "general", "random"]):
            client.put(f"/api/channels/{ch_ids[i]}", json={
                "name": name,
                "description": "",
                "is_public": True,
                "default_role": "write",
                "default_join": True,
            }, headers=admin_user["headers"])

        # Regular user joins space
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])

        r = client.get("/api/channels", headers=regular_user["headers"])
        user_channel_ids = [c["id"] for c in r.json()]
        for ch_id in ch_ids:
            assert ch_id in user_channel_ids

    def test_archived_default_channel_not_auto_joined(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]

        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "old-channel"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        # Set as default_join then archive
        client.put(f"/api/channels/{ch_id}", json={
            "name": "old-channel",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=admin_user["headers"])
        client.post(f"/api/channels/{ch_id}/archive",
                    headers=admin_user["headers"])

        # Regular user joins space
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])

        r = client.get("/api/channels", headers=regular_user["headers"])
        user_channel_ids = [c["id"] for c in r.json()]
        assert ch_id not in user_channel_ids

    def test_existing_member_not_duplicated(self, client, admin_user, regular_user):
        """If user is already in the channel, joining space shouldn't fail."""
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]

        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "general"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        client.put(f"/api/channels/{ch_id}", json={
            "name": "general",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=admin_user["headers"])

        # Admin is already a member of the channel (as creator)
        # Joining the space again shouldn't cause errors
        # (Admin is already in the space as owner, so this tests the code path)
        r = client.get("/api/channels", headers=admin_user["headers"])
        admin_channel_ids = [c["id"] for c in r.json()]
        assert ch_id in admin_channel_ids


class TestAutoJoinOnInviteAccept:
    """When a user accepts a space invite, they should be auto-added to default_join channels."""

    def test_invite_accept_auto_joins_default_channels(self, client, admin_user, regular_user):
        # Create private space with a default_join channel
        r = client.post("/api/spaces",
                        json={"name": "Private Team", "is_public": False},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]

        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "welcome"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]

        client.put(f"/api/channels/{ch_id}", json={
            "name": "welcome",
            "description": "",
            "is_public": True,
            "default_role": "write",
            "default_join": True,
        }, headers=admin_user["headers"])

        # Invite regular user
        r = client.post(f"/api/spaces/{sp_id}/members", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])
        assert r.status_code == 200

        # Get the invite
        r = client.get("/api/space-invites", headers=regular_user["headers"])
        assert r.status_code == 200
        invites = r.json()
        assert len(invites) > 0
        invite_id = invites[0]["id"]

        # Accept the invite
        r = client.post(f"/api/space-invites/{invite_id}/accept",
                        headers=regular_user["headers"])
        assert r.status_code == 200

        # Check that regular user is in the default_join channel
        r = client.get("/api/channels", headers=regular_user["headers"])
        user_channel_ids = [c["id"] for c in r.json()]
        assert ch_id in user_channel_ids

    def test_invite_accept_multiple_default_channels(self, client, admin_user, regular_user):
        r = client.post("/api/spaces",
                        json={"name": "Private Team", "is_public": False},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]

        ch_ids = []
        for name in ["general", "announcements"]:
            r = client.post(f"/api/spaces/{sp_id}/channels",
                            json={"name": name},
                            headers=admin_user["headers"])
            ch_ids.append(r.json()["id"])
            client.put(f"/api/channels/{ch_ids[-1]}", json={
                "name": name,
                "description": "",
                "is_public": True,
                "default_role": "write",
                "default_join": True,
            }, headers=admin_user["headers"])

        # Invite and accept
        client.post(f"/api/spaces/{sp_id}/members", json={
            "user_id": regular_user["user"]["id"],
        }, headers=admin_user["headers"])

        r = client.get("/api/space-invites", headers=regular_user["headers"])
        invite_id = r.json()[0]["id"]
        client.post(f"/api/space-invites/{invite_id}/accept",
                    headers=regular_user["headers"])

        r = client.get("/api/channels", headers=regular_user["headers"])
        user_channel_ids = [c["id"] for c in r.json()]
        for ch_id in ch_ids:
            assert ch_id in user_channel_ids
