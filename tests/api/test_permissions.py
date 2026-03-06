"""Security-focused tests: privilege escalation, RBAC enforcement, access control."""

import pytest
from conftest import PKIIdentity, auth_header, pki_register


class TestPrivilegeEscalation:
    """Verify that regular users cannot escalate to admin privileges."""

    def test_regular_user_cannot_access_admin_settings(self, client, admin_user,
                                                        regular_user):
        r = client.get("/api/admin/settings", headers=regular_user["headers"])
        assert r.status_code == 403

    def test_regular_user_cannot_create_invites(self, client, admin_user, regular_user):
        r = client.post("/api/admin/invites", json={},
                        headers=regular_user["headers"])
        assert r.status_code == 403

    def test_regular_user_cannot_change_own_role(self, client, admin_user, regular_user):
        r = client.put(
            f"/api/admin/users/{regular_user['user']['id']}/role",
            json={"role": "owner"},
            headers=regular_user["headers"],
        )
        assert r.status_code == 403

    def test_admin_cannot_promote_above_own_rank(self, client, admin_user, regular_user):
        """Admins cannot promote anyone to owner (above their rank)."""
        # Promote to admin first
        client.put(
            f"/api/admin/users/{regular_user['user']['id']}/role",
            json={"role": "admin"},
            headers=admin_user["headers"],
        )
        # Create another user
        data = pki_register(client, "user3", "User Three")
        # Admin tries to promote user3 to owner - should fail
        r = client.put(
            f"/api/admin/users/{data['user']['id']}/role",
            json={"role": "owner"},
            headers=regular_user["headers"],
        )
        assert r.status_code == 403

    def test_admin_cannot_archive_server(self, client, admin_user, regular_user):
        """Only owners can archive the server."""
        client.put(
            f"/api/admin/users/{regular_user['user']['id']}/role",
            json={"role": "admin"},
            headers=admin_user["headers"],
        )
        r = client.post("/api/admin/archive-server",
                        headers=regular_user["headers"])
        assert r.status_code == 403


class TestChannelAccessControl:
    """Verify channel-level access restrictions."""

    def test_non_member_cannot_read_private_channel(self, client, admin_user,
                                                      regular_user):
        r = client.post("/api/channels",
                        json={"name": "secret", "is_public": False},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        r = client.get(f"/api/channels/{ch_id}/messages",
                       headers=regular_user["headers"])
        assert r.status_code == 403

    def test_read_role_user_listed_as_member(self, client, admin_user, regular_user):
        """A user with read role should be able to access messages."""
        r = client.post("/api/channels",
                        json={"name": "readonly", "default_role": "read"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": regular_user["user"]["id"],
            "role": "read",
        }, headers=admin_user["headers"])
        r = client.get(f"/api/channels/{ch_id}/messages",
                       headers=regular_user["headers"])
        assert r.status_code == 200

    def test_write_role_cannot_manage_members(self, client, admin_user,
                                               regular_user, second_regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        # Add regular_user with write role
        client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": regular_user["user"]["id"],
            "role": "write",
        }, headers=admin_user["headers"])
        # Write user tries to add another member
        r = client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": second_regular_user["user"]["id"],
        }, headers=regular_user["headers"])
        assert r.status_code == 403

    def test_write_role_cannot_update_channel(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": regular_user["user"]["id"],
            "role": "write",
        }, headers=admin_user["headers"])
        r = client.put(f"/api/channels/{ch_id}", json={"name": "pwned"},
                       headers=regular_user["headers"])
        assert r.status_code == 403

    def test_write_role_cannot_archive_channel(self, client, admin_user, regular_user):
        r = client.post("/api/channels", json={"name": "ch"},
                        headers=admin_user["headers"])
        ch_id = r.json()["id"]
        client.post(f"/api/channels/{ch_id}/members", json={
            "user_id": regular_user["user"]["id"],
            "role": "write",
        }, headers=admin_user["headers"])
        r = client.post(f"/api/channels/{ch_id}/archive",
                        headers=regular_user["headers"])
        assert r.status_code == 403


class TestSpaceAccessControl:
    """Verify space-level access restrictions."""

    def test_non_member_cannot_access_private_space(self, client, admin_user,
                                                      regular_user):
        r = client.post("/api/spaces",
                        json={"name": "Secret", "is_public": False},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.post(f"/api/spaces/{sp_id}/join",
                        headers=regular_user["headers"])
        assert r.status_code == 403

    def test_write_member_cannot_create_space_channel(self, client, admin_user,
                                                        regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/channels",
                        json={"name": "nope"},
                        headers=regular_user["headers"])
        assert r.status_code == 403

    def test_write_member_cannot_update_space(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.put(f"/api/spaces/{sp_id}", json={"name": "Hacked"},
                       headers=regular_user["headers"])
        assert r.status_code == 403

    def test_write_member_cannot_invite(self, client, admin_user, regular_user,
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

    def test_non_owner_cannot_archive_space(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/archive",
                        headers=regular_user["headers"])
        assert r.status_code == 403


class TestServerAdminBypass:
    """Server admins get automatic admin access to channels/spaces."""

    def test_server_admin_can_read_any_channel(self, client, admin_user, regular_user):
        # Regular user creates a private channel
        r = client.post("/api/channels",
                        json={"name": "private", "is_public": False},
                        headers=regular_user["headers"])
        ch_id = r.json()["id"]
        # Server admin (admin_user) should still be able to read it
        r = client.get(f"/api/channels/{ch_id}/messages",
                       headers=admin_user["headers"])
        assert r.status_code == 200

    def test_server_admin_can_join_private_channel(self, client, admin_user,
                                                     regular_user):
        r = client.post("/api/channels",
                        json={"name": "private", "is_public": False},
                        headers=regular_user["headers"])
        ch_id = r.json()["id"]
        r = client.post(f"/api/channels/{ch_id}/join",
                        headers=admin_user["headers"])
        assert r.status_code == 200


class TestCrossUserIsolation:
    """Ensure users cannot access other users' resources."""

    def test_cannot_delete_other_users_pki_key(self, client, admin_user, regular_user):
        # Give admin a second key so the "only credential" check doesn't trigger
        r = client.post("/api/users/me/keys/challenge",
                        headers=admin_user["headers"])
        from conftest import PKIIdentity
        extra = PKIIdentity()
        challenge = r.json()["challenge"]
        client.post("/api/users/me/keys", json={
            "public_key": extra.public_key_b64url,
            "challenge": challenge,
            "signature": extra.sign(challenge),
        }, headers=admin_user["headers"])
        # Get regular user's key ID
        r = client.get("/api/users/me/keys", headers=regular_user["headers"])
        if r.json():
            key_id = r.json()[0]["id"]
            # Admin tries to delete regular user's key via /me endpoint
            # This should silently fail (scoped to admin's user_id) or return ok
            # but NOT actually remove regular user's key
            client.delete(f"/api/users/me/keys/{key_id}",
                          headers=admin_user["headers"])
            # Verify regular user's key still exists
            r = client.get("/api/users/me/keys", headers=regular_user["headers"])
            assert len(r.json()) >= 1


class TestSpaceRoleHierarchy:
    """Verify rank-based permission enforcement for space member role changes."""

    def _create_space_with_members(self, client, admin_user, regular_user,
                                    second_regular_user):
        """Helper: create a space with admin_user as owner, others as members."""
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=second_regular_user["headers"])
        return sp_id

    def test_space_owner_can_promote_to_admin(self, client, admin_user,
                                               regular_user, second_regular_user):
        sp_id = self._create_space_with_members(client, admin_user, regular_user,
                                                 second_regular_user)
        r = client.put(f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
                       json={"role": "admin"}, headers=admin_user["headers"])
        assert r.status_code == 200

    def test_space_admin_cannot_demote_another_admin(self, client, admin_user,
                                                      regular_user,
                                                      second_regular_user):
        sp_id = self._create_space_with_members(client, admin_user, regular_user,
                                                 second_regular_user)
        # Promote both to admin
        client.put(f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
                   json={"role": "admin"}, headers=admin_user["headers"])
        client.put(f"/api/spaces/{sp_id}/members/{second_regular_user['user']['id']}",
                   json={"role": "admin"}, headers=admin_user["headers"])
        # Admin tries to demote the other admin
        r = client.put(
            f"/api/spaces/{sp_id}/members/{second_regular_user['user']['id']}",
            json={"role": "write"}, headers=regular_user["headers"])
        assert r.status_code == 403

    def test_space_admin_cannot_promote_to_owner(self, client, admin_user,
                                                   regular_user,
                                                   second_regular_user):
        sp_id = self._create_space_with_members(client, admin_user, regular_user,
                                                 second_regular_user)
        # Promote regular to admin
        client.put(f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
                   json={"role": "admin"}, headers=admin_user["headers"])
        # Admin tries to promote second_regular to owner
        r = client.put(
            f"/api/spaces/{sp_id}/members/{second_regular_user['user']['id']}",
            json={"role": "owner"}, headers=regular_user["headers"])
        assert r.status_code == 403

    def test_space_admin_can_promote_write_to_admin(self, client, admin_user,
                                                      regular_user,
                                                      second_regular_user):
        sp_id = self._create_space_with_members(client, admin_user, regular_user,
                                                 second_regular_user)
        # Promote regular to admin
        client.put(f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
                   json={"role": "admin"}, headers=admin_user["headers"])
        # Admin promotes write member to admin (same rank, allowed)
        r = client.put(
            f"/api/spaces/{sp_id}/members/{second_regular_user['user']['id']}",
            json={"role": "admin"}, headers=regular_user["headers"])
        assert r.status_code == 200

    def test_space_admin_can_demote_write_to_read(self, client, admin_user,
                                                    regular_user,
                                                    second_regular_user):
        sp_id = self._create_space_with_members(client, admin_user, regular_user,
                                                 second_regular_user)
        # Promote regular to admin
        client.put(f"/api/spaces/{sp_id}/members/{regular_user['user']['id']}",
                   json={"role": "admin"}, headers=admin_user["headers"])
        # Admin can demote write member (lower rank) to read
        r = client.put(
            f"/api/spaces/{sp_id}/members/{second_regular_user['user']['id']}",
            json={"role": "read"}, headers=regular_user["headers"])
        assert r.status_code == 200


class TestInputValidation:
    """Test that the API properly validates and sanitizes input."""

    def test_nonexistent_channel_returns_error(self, client, admin_user, regular_user):
        fake_id = "00000000-0000-0000-0000-000000000000"
        # Use regular user (server admins bypass membership checks)
        r = client.get(f"/api/channels/{fake_id}/messages",
                       headers=regular_user["headers"])
        assert r.status_code in (403, 404)

    def test_nonexistent_space_returns_error(self, client, admin_user, regular_user):
        fake_id = "00000000-0000-0000-0000-000000000000"
        r = client.get(f"/api/spaces/{fake_id}",
                       headers=regular_user["headers"])
        assert r.status_code in (403, 404)

    def test_empty_channel_name_handling(self, client, admin_user):
        r = client.post("/api/channels", json={"name": ""},
                        headers=admin_user["headers"])
        # Should either reject or create with empty name
        # We're just checking it doesn't crash (500)
        assert r.status_code != 500

    def test_very_long_channel_name(self, client, admin_user):
        r = client.post("/api/channels", json={"name": "x" * 10000},
                        headers=admin_user["headers"])
        assert r.status_code != 500

    def test_special_chars_in_search(self, client, admin_user):
        r = client.get("/api/search", params={
            "q": "'; DROP TABLE users; --",
            "type": "messages",
        }, headers=admin_user["headers"])
        # Should not crash or return 500
        assert r.status_code != 500

    def test_sql_injection_in_channel_name(self, client, admin_user):
        r = client.post("/api/channels", json={
            "name": "'; DROP TABLE channels; --",
        }, headers=admin_user["headers"])
        assert r.status_code != 500
        # Verify channels still work
        r = client.get("/api/channels", headers=admin_user["headers"])
        assert r.status_code == 200
