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

    def test_regular_user_cannot_create_space(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={
            "name": "UserSpace",
        }, headers=regular_user["headers"])
        assert r.status_code == 403


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


class TestSpaceListPagination:
    def test_list_spaces_pagination(self, client, admin_user):
        # Create 5 spaces. Names chosen so they sort predictably.
        names = ["Pag-A", "Pag-B", "Pag-C", "Pag-D", "Pag-E"]
        for n in names:
            r = client.post("/api/spaces", json={"name": n},
                            headers=admin_user["headers"])
            assert r.status_code == 200

        # First page: limit=2
        r = client.get("/api/spaces?limit=2",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        first_page = r.json()
        assert len(first_page) == 2

        # Second page: offset=2
        r = client.get("/api/spaces?limit=2&offset=2",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        second_page = r.json()
        assert len(second_page) == 2

        first_ids = {sp["id"] for sp in first_page}
        second_ids = {sp["id"] for sp in second_page}
        assert first_ids.isdisjoint(second_ids)

    def test_list_spaces_invalid_offset(self, client, admin_user):
        r = client.get("/api/spaces?offset=-1",
                       headers=admin_user["headers"])
        assert r.status_code == 400, (
            f"Expected 400 for negative offset; got {r.status_code} "
            f"({r.text!r})"
        )

    def test_list_spaces_default_ordering(self, client, admin_user):
        creation_order = ["ZZZ-ord", "AAA-ord", "MMM-ord"]
        for n in creation_order:
            r = client.post("/api/spaces", json={"name": n},
                            headers=admin_user["headers"])
            assert r.status_code == 200

        r = client.get("/api/spaces?limit=500",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        body = r.json()
        seen = [sp["name"] for sp in body if sp["name"] in creation_order]
        assert seen == sorted(creation_order), (
            f"Expected alphabetic ordering, got {seen!r}"
        )


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


class TestSpaceAvatarFields:
    def test_space_response_has_avatar_fields(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        assert r.status_code == 200
        sp = r.json()
        assert "avatar_file_id" in sp
        assert "profile_color" in sp
        assert "icon" not in sp

    def test_space_list_has_avatar_fields(self, client, admin_user):
        client.post("/api/spaces", json={"name": "Team"},
                    headers=admin_user["headers"])
        r = client.get("/api/spaces", headers=admin_user["headers"])
        assert r.status_code == 200
        sp = r.json()[0]
        assert "avatar_file_id" in sp
        assert "profile_color" in sp
        assert "icon" not in sp

    def test_update_space_profile_color(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.put(f"/api/spaces/{sp_id}", json={
            "name": "Team",
            "profile_color": "#ff5500",
        }, headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["profile_color"] == "#ff5500"


class TestSpaceAvatar:
    def test_upload_space_avatar(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        # Create a minimal 1x1 PNG
        import struct, zlib
        def make_png():
            raw = b'\x00\x00\x00\x00'  # 1 pixel RGBA
            data = zlib.compress(raw)
            def chunk(ctype, body):
                c = ctype + body
                return struct.pack('>I', len(body)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
            ihdr = struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0)
            return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', data) + chunk(b'IEND', b'')
        png_data = make_png()
        r = client.post(f"/api/spaces/{sp_id}/avatar",
                        content=png_data,
                        headers={
                            **admin_user["headers"],
                            "Content-Type": "image/png",
                        })
        assert r.status_code == 200
        assert r.json()["avatar_file_id"] != ""

    def test_delete_space_avatar(self, client, admin_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        r = client.delete(f"/api/spaces/{sp_id}/avatar",
                          headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["avatar_file_id"] == ""

    def test_non_admin_cannot_upload_space_avatar(self, client, admin_user, regular_user):
        r = client.post("/api/spaces", json={"name": "Team"},
                        headers=admin_user["headers"])
        sp_id = r.json()["id"]
        client.post(f"/api/spaces/{sp_id}/join",
                    headers=regular_user["headers"])
        r = client.post(f"/api/spaces/{sp_id}/avatar",
                        content=b'\x89PNG fake',
                        headers={
                            **regular_user["headers"],
                            "Content-Type": "image/png",
                        })
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
