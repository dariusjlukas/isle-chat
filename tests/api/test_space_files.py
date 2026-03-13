"""Tests for space file endpoints: CRUD, permissions, versions, admin storage."""

import io
from conftest import pki_register, auth_header


def _create_space(client, headers, name="TestSpace"):
    """Helper to create a space and enable the files tool."""
    r = client.post("/api/spaces", json={"name": name}, headers=headers)
    assert r.status_code == 200
    sp = r.json()
    # Enable the files tool
    client.put(f"/api/spaces/{sp['id']}/tools",
               json={"tool": "files", "enabled": True}, headers=headers)
    return sp


def _upload_file(client, space_id, headers, filename="test.txt",
                 content=b"hello world", content_type="text/plain",
                 parent_id=""):
    """Helper to upload a file via raw body POST."""
    params = {"filename": filename, "content_type": content_type}
    if parent_id:
        params["parent_id"] = parent_id
    r = client.post(
        f"/api/spaces/{space_id}/files/upload",
        params=params,
        content=content,
        headers={**headers, "Content-Type": content_type},
    )
    return r


class TestSpaceFilesCRUD:
    def test_create_folder(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = client.post(f"/api/spaces/{sp['id']}/files/folder",
                        json={"name": "Documents", "parent_id": ""},
                        headers=admin_user["headers"])
        assert r.status_code == 200
        folder = r.json()
        assert folder["name"] == "Documents"
        assert folder["is_folder"] is True

    def test_upload_file(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"])
        assert r.status_code == 200
        f = r.json()
        assert f["name"] == "test.txt"
        assert f["is_folder"] is False
        assert f["file_size"] == 11  # len("hello world")

    def test_list_files(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _upload_file(client, sp["id"], admin_user["headers"], "a.txt")
        _upload_file(client, sp["id"], admin_user["headers"], "b.txt")

        r = client.get(f"/api/spaces/{sp['id']}/files",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert len(data["files"]) == 2
        assert "my_permission" in data

    def test_list_files_in_subfolder(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = client.post(f"/api/spaces/{sp['id']}/files/folder",
                        json={"name": "Sub", "parent_id": ""},
                        headers=admin_user["headers"])
        folder_id = r.json()["id"]

        _upload_file(client, sp["id"], admin_user["headers"],
                     "inner.txt", parent_id=folder_id)

        r = client.get(f"/api/spaces/{sp['id']}/files",
                       params={"parent_id": folder_id},
                       headers=admin_user["headers"])
        assert r.status_code == 200
        assert len(r.json()["files"]) == 1
        assert r.json()["files"][0]["name"] == "inner.txt"

    def test_rename_file(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"], "old.txt")
        file_id = r.json()["id"]

        r = client.put(f"/api/spaces/{sp['id']}/files/{file_id}",
                       json={"name": "new.txt"},
                       headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["name"] == "new.txt"

    def test_delete_file(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        r = client.delete(f"/api/spaces/{sp['id']}/files/{file_id}",
                          headers=admin_user["headers"])
        assert r.status_code == 200

        # File should not appear in listing
        r = client.get(f"/api/spaces/{sp['id']}/files",
                       headers=admin_user["headers"])
        assert len(r.json()["files"]) == 0

    def test_download_file(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"],
                         content=b"download me")
        file_id = r.json()["id"]

        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}/download",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.content == b"download me"

    def test_get_file_details(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert data["name"] == "test.txt"
        assert "my_permission" in data
        assert "path" in data

    def test_duplicate_name_rejected(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _upload_file(client, sp["id"], admin_user["headers"], "dup.txt")
        r = _upload_file(client, sp["id"], admin_user["headers"], "dup.txt")
        assert r.status_code == 409

    def test_non_member_cannot_access(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"], "Private")
        # regular_user is not a member
        r = client.get(f"/api/spaces/{sp['id']}/files",
                       headers=regular_user["headers"])
        assert r.status_code == 403


class TestSpaceFilePermissions:
    def test_owner_permission_on_list(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = client.get(f"/api/spaces/{sp['id']}/files",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        # Space owner gets "owner" effective permission
        assert r.json()["my_permission"] == "owner"

    def test_write_member_gets_edit(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        # Join as default "write" role
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = client.get(f"/api/spaces/{sp['id']}/files",
                       headers=regular_user["headers"])
        assert r.status_code == 200
        assert r.json()["my_permission"] == "edit"

    def test_get_file_permissions(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}/permissions",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert "permissions" in data
        assert "my_permission" in data
        # Creator gets auto-granted owner
        assert len(data["permissions"]) >= 1

    def test_set_permission(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        r = client.put(
            f"/api/spaces/{sp['id']}/files/{file_id}/permissions",
            json={
                "user_id": regular_user["user"]["id"],
                "permission": "edit",
            },
            headers=admin_user["headers"],
        )
        assert r.status_code == 200

        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}/permissions",
                       headers=admin_user["headers"])
        perms = r.json()["permissions"]
        user_perm = next(p for p in perms if p["user_id"] == regular_user["user"]["id"])
        assert user_perm["permission"] == "edit"

    def test_remove_permission(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        # Grant then remove
        client.put(
            f"/api/spaces/{sp['id']}/files/{file_id}/permissions",
            json={"user_id": regular_user["user"]["id"], "permission": "edit"},
            headers=admin_user["headers"],
        )
        r = client.delete(
            f"/api/spaces/{sp['id']}/files/{file_id}/permissions/{regular_user['user']['id']}",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200

    def test_invalid_permission_level(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        r = client.put(
            f"/api/spaces/{sp['id']}/files/{file_id}/permissions",
            json={"user_id": regular_user["user"]["id"], "permission": "superadmin"},
            headers=admin_user["headers"],
        )
        assert r.status_code == 400

    def test_non_owner_cannot_set_permissions(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        # regular_user has "edit" on root, not "owner" on this file
        r = client.put(
            f"/api/spaces/{sp['id']}/files/{file_id}/permissions",
            json={"user_id": admin_user["user"]["id"], "permission": "view"},
            headers=regular_user["headers"],
        )
        assert r.status_code == 403

    def test_view_user_cannot_create_folder(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        # Add regular_user with read role
        client.post(f"/api/spaces/{sp['id']}/members",
                    json={"user_id": regular_user["user"]["id"], "role": "read"},
                    headers=admin_user["headers"])

        r = client.post(f"/api/spaces/{sp['id']}/files/folder",
                        json={"name": "Blocked", "parent_id": ""},
                        headers=regular_user["headers"])
        assert r.status_code == 403

    def test_view_user_cannot_upload(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/members",
                    json={"user_id": regular_user["user"]["id"], "role": "read"},
                    headers=admin_user["headers"])

        r = _upload_file(client, sp["id"], regular_user["headers"])
        assert r.status_code == 403

    def test_edit_user_cannot_delete(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        client.post(f"/api/spaces/{sp['id']}/join",
                    headers=regular_user["headers"])

        # Admin uploads file
        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        # Regular user (edit level) cannot delete (requires owner)
        r = client.delete(f"/api/spaces/{sp['id']}/files/{file_id}",
                          headers=regular_user["headers"])
        assert r.status_code == 403


class TestSpaceFileVersions:
    def test_list_versions(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}/versions",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        versions = r.json()["versions"]
        assert len(versions) == 1
        assert versions[0]["version_number"] == 1

    def test_upload_new_version(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"],
                         content=b"version 1")
        file_id = r.json()["id"]

        # Upload version 2
        r = client.post(
            f"/api/spaces/{sp['id']}/files/{file_id}/versions",
            content=b"version 2",
            headers={**admin_user["headers"], "Content-Type": "text/plain"},
        )
        assert r.status_code == 200
        v2 = r.json()
        assert v2["version_number"] == 2
        assert v2["file_size"] == 9  # len("version 2")

    def test_download_specific_version(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"],
                         content=b"original")
        file_id = r.json()["id"]

        # Upload v2
        client.post(
            f"/api/spaces/{sp['id']}/files/{file_id}/versions",
            content=b"updated",
            headers={**admin_user["headers"], "Content-Type": "text/plain"},
        )

        # Get v1 id
        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}/versions",
                       headers=admin_user["headers"])
        versions = r.json()["versions"]
        v1_id = versions[-1]["id"]  # last = oldest = v1

        # Download v1
        r = client.get(
            f"/api/spaces/{sp['id']}/files/{file_id}/versions/{v1_id}/download",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200
        assert r.content == b"original"

    def test_revert_to_version(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = _upload_file(client, sp["id"], admin_user["headers"],
                         content=b"version 1")
        file_id = r.json()["id"]

        client.post(
            f"/api/spaces/{sp['id']}/files/{file_id}/versions",
            content=b"version 2",
            headers={**admin_user["headers"], "Content-Type": "text/plain"},
        )

        # Get v1 id
        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}/versions",
                       headers=admin_user["headers"])
        v1_id = r.json()["versions"][-1]["id"]

        # Revert to v1
        r = client.post(
            f"/api/spaces/{sp['id']}/files/{file_id}/versions/{v1_id}/revert",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200
        v3 = r.json()
        assert v3["version_number"] == 3
        assert v3["file_size"] == 9  # same as v1 "version 1"

        # Download current should give v1 content
        r = client.get(f"/api/spaces/{sp['id']}/files/{file_id}/download",
                       headers=admin_user["headers"])
        assert r.content == b"version 1"

    def test_versions_on_folder_returns_404(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        r = client.post(f"/api/spaces/{sp['id']}/files/folder",
                        json={"name": "Docs", "parent_id": ""},
                        headers=admin_user["headers"])
        folder_id = r.json()["id"]

        r = client.get(f"/api/spaces/{sp['id']}/files/{folder_id}/versions",
                       headers=admin_user["headers"])
        assert r.status_code == 404

    def test_view_user_cannot_upload_version(self, client, admin_user, regular_user):
        sp = _create_space(client, admin_user["headers"])
        # Add regular as read
        client.post(f"/api/spaces/{sp['id']}/members",
                    json={"user_id": regular_user["user"]["id"], "role": "read"},
                    headers=admin_user["headers"])

        r = _upload_file(client, sp["id"], admin_user["headers"])
        file_id = r.json()["id"]

        r = client.post(
            f"/api/spaces/{sp['id']}/files/{file_id}/versions",
            content=b"blocked",
            headers={**regular_user["headers"], "Content-Type": "text/plain"},
        )
        assert r.status_code == 403


class TestSpaceFileStorage:
    def test_get_storage_used(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _upload_file(client, sp["id"], admin_user["headers"],
                     content=b"A" * 1000)

        r = client.get(f"/api/spaces/{sp['id']}/storage",
                       headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["used"] == 1000


class TestAdminStorage:
    def test_admin_storage_endpoint(self, client, admin_user):
        sp = _create_space(client, admin_user["headers"])
        _upload_file(client, sp["id"], admin_user["headers"],
                     content=b"data" * 100)

        r = client.get("/api/admin/storage", headers=admin_user["headers"])
        assert r.status_code == 200
        data = r.json()
        assert "spaces" in data
        assert "total_used" in data
        assert data["total_used"] >= 400

    def test_non_admin_cannot_access_storage(self, client, admin_user, regular_user):
        r = client.get("/api/admin/storage", headers=regular_user["headers"])
        assert r.status_code == 403
