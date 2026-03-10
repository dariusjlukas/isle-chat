"""Tests for multi-factor authentication: TOTP setup, MFA login flow, admin MFA settings."""

import pyotp
import pytest
from conftest import (auth_header, pki_register, pki_login,
                      password_register, password_login)


def setup_totp(client, headers):
    """Helper: initiate TOTP setup and verify it. Returns the pyotp TOTP object."""
    r = client.post("/api/users/me/totp/setup", json={}, headers=headers)
    assert r.status_code == 200
    data = r.json()
    secret = data["secret"]
    assert data["uri"].startswith("otpauth://totp/")
    totp = pyotp.TOTP(secret)
    code = totp.now()
    r = client.post("/api/users/me/totp/verify", json={"code": code}, headers=headers)
    assert r.status_code == 200
    return totp


class TestTotpSetup:
    def test_setup_and_verify(self, client, admin_user):
        totp = setup_totp(client, admin_user["headers"])
        # Status should now show enabled
        r = client.get("/api/users/me/totp/status", headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["enabled"] is True

    def test_status_disabled_by_default(self, client, admin_user):
        r = client.get("/api/users/me/totp/status", headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["enabled"] is False

    def test_verify_wrong_code(self, client, admin_user):
        r = client.post("/api/users/me/totp/setup", json={}, headers=admin_user["headers"])
        assert r.status_code == 200
        r = client.post("/api/users/me/totp/verify",
                        json={"code": "000000"}, headers=admin_user["headers"])
        assert r.status_code == 401

    def test_verify_without_setup(self, client, admin_user):
        r = client.post("/api/users/me/totp/verify",
                        json={"code": "123456"}, headers=admin_user["headers"])
        assert r.status_code == 400

    def test_has_totp_in_me(self, client, admin_user):
        r = client.get("/api/users/me", headers=admin_user["headers"])
        assert r.json()["has_totp"] is False
        setup_totp(client, admin_user["headers"])
        r = client.get("/api/users/me", headers=admin_user["headers"])
        assert r.json()["has_totp"] is True


class TestTotpRemoval:
    def test_remove_totp(self, client, admin_user):
        totp = setup_totp(client, admin_user["headers"])
        code = totp.now()
        r = client.request("DELETE", "/api/users/me/totp",
                           json={"code": code}, headers=admin_user["headers"])
        assert r.status_code == 200
        r = client.get("/api/users/me/totp/status", headers=admin_user["headers"])
        assert r.json()["enabled"] is False

    def test_remove_totp_wrong_code(self, client, admin_user):
        setup_totp(client, admin_user["headers"])
        r = client.request("DELETE", "/api/users/me/totp",
                           json={"code": "000000"}, headers=admin_user["headers"])
        assert r.status_code == 401

    def test_remove_totp_not_enabled(self, client, admin_user):
        r = client.request("DELETE", "/api/users/me/totp",
                           json={"code": "123456"}, headers=admin_user["headers"])
        assert r.status_code == 400


class TestMfaPasswordLogin:
    """Test MFA flow when user has TOTP enabled and logs in with password."""

    def test_password_login_with_totp(self, client, admin_user):
        """User with TOTP enabled gets mfa_required response, then verifies."""
        client.put("/api/admin/settings", json={
            "auth_methods": ["passkey", "pki", "password"],
        }, headers=admin_user["headers"])

        reg = password_register(client, "mfauser", "MFA User", password="TestPass123")
        headers = auth_header(reg["token"])
        totp = setup_totp(client, headers)

        # Now login with password — should get MFA challenge
        r = client.post("/api/auth/password/login", json={
            "username": "mfauser",
            "password": "TestPass123",
        })
        assert r.status_code == 200
        data = r.json()
        assert data["mfa_required"] is True
        assert "mfa_token" in data

        # Verify with TOTP code
        code = totp.now()
        r = client.post("/api/auth/mfa/verify", json={
            "mfa_token": data["mfa_token"],
            "totp_code": code,
        })
        assert r.status_code == 200
        result = r.json()
        assert result["token"]
        assert result["user"]["username"] == "mfauser"

    def test_mfa_wrong_totp_code(self, client, admin_user):
        client.put("/api/admin/settings", json={
            "auth_methods": ["passkey", "pki", "password"],
        }, headers=admin_user["headers"])

        reg = password_register(client, "mfauser2", "MFA User 2", password="TestPass123")
        headers = auth_header(reg["token"])
        setup_totp(client, headers)

        r = client.post("/api/auth/password/login", json={
            "username": "mfauser2",
            "password": "TestPass123",
        })
        data = r.json()

        r = client.post("/api/auth/mfa/verify", json={
            "mfa_token": data["mfa_token"],
            "totp_code": "000000",
        })
        assert r.status_code == 401

    def test_mfa_invalid_token(self, client):
        r = client.post("/api/auth/mfa/verify", json={
            "mfa_token": "bogus-token",
            "totp_code": "123456",
        })
        assert r.status_code in (400, 401)


class TestMfaPkiLogin:
    """Test MFA flow when user has TOTP enabled and logs in with PKI."""

    def test_pki_login_with_totp(self, client, admin_user):
        totp = setup_totp(client, admin_user["headers"])

        # PKI login should trigger MFA
        identity = admin_user["identity"]
        r = client.post("/api/auth/pki/challenge",
                        json={"public_key": identity.public_key_b64url})
        challenge = r.json()["challenge"]
        r = client.post("/api/auth/pki/login", json={
            "public_key": identity.public_key_b64url,
            "challenge": challenge,
            "signature": identity.sign(challenge),
        })
        assert r.status_code == 200
        data = r.json()
        assert data["mfa_required"] is True

        # Complete MFA
        r = client.post("/api/auth/mfa/verify", json={
            "mfa_token": data["mfa_token"],
            "totp_code": totp.now(),
        })
        assert r.status_code == 200
        assert r.json()["token"]


class TestAdminMfaSettings:
    def test_mfa_required_password(self, client, admin_user):
        """Admin can require MFA for password auth."""
        client.put("/api/admin/settings", json={
            "auth_methods": ["passkey", "pki", "password"],
            "mfa_required_password": True,
        }, headers=admin_user["headers"])
        r = client.get("/api/admin/settings", headers=admin_user["headers"])
        assert r.json()["mfa_required_password"] is True

    def test_mfa_required_pki(self, client, admin_user):
        client.put("/api/admin/settings", json={
            "mfa_required_pki": True,
        }, headers=admin_user["headers"])
        r = client.get("/api/admin/settings", headers=admin_user["headers"])
        assert r.json()["mfa_required_pki"] is True

    def test_mfa_required_passkey(self, client, admin_user):
        client.put("/api/admin/settings", json={
            "mfa_required_passkey": True,
        }, headers=admin_user["headers"])
        r = client.get("/api/admin/settings", headers=admin_user["headers"])
        assert r.json()["mfa_required_passkey"] is True

    def test_config_includes_mfa_settings(self, client, admin_user):
        client.put("/api/admin/settings", json={
            "mfa_required_password": True,
        }, headers=admin_user["headers"])
        r = client.get("/api/config")
        data = r.json()
        assert "mfa_required_password" in data
        assert data["mfa_required_password"] is True

    def test_mfa_required_but_no_totp_setup(self, client, admin_user):
        """When admin requires MFA but user hasn't set it up, user gets must_setup_totp flag."""
        client.put("/api/admin/settings", json={
            "auth_methods": ["passkey", "pki", "password"],
            "mfa_required_password": True,
        }, headers=admin_user["headers"])

        # Register a password user (who hasn't set up TOTP)
        reg = password_register(client, "nomfa", "No MFA", password="TestPass123")
        # User is logged in from registration, now log out and try logging in
        r = client.post("/api/auth/password/login", json={
            "username": "nomfa",
            "password": "TestPass123",
        })
        assert r.status_code == 200
        data = r.json()
        # Should get a session but with must_setup_totp flag
        assert data.get("must_setup_totp") is True
        assert data["token"]
        assert data["user"]["username"] == "nomfa"

    def test_mfa_not_required_no_totp_normal_login(self, client, admin_user):
        """Without MFA requirement and no TOTP, login is normal (no MFA challenge)."""
        client.put("/api/admin/settings", json={
            "auth_methods": ["passkey", "pki", "password"],
            "mfa_required_password": False,
        }, headers=admin_user["headers"])

        password_register(client, "normuser", "Normal User", password="TestPass123")
        r = client.post("/api/auth/password/login", json={
            "username": "normuser",
            "password": "TestPass123",
        })
        data = r.json()
        assert data["token"]
        assert data.get("mfa_required") is not True
        assert data.get("must_setup_totp") is not True
