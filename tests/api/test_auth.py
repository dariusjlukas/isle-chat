"""Tests for authentication endpoints: PKI register/login, sessions, logout."""

import httpx
import pytest
from conftest import PKIIdentity, auth_header, pki_login, pki_register


class TestPKIRegistration:
    def test_first_user_becomes_owner(self, client):
        data = pki_register(client, "first", "First User")
        assert data["token"]
        assert data["user"]["username"] == "first"
        assert data["user"]["role"] in ("admin", "owner")
        assert "recovery_keys" in data
        assert len(data["recovery_keys"]) == 8

    def test_second_user_is_regular(self, client):
        admin = pki_register(client, "admin", "Admin")
        # Open registration so second user can register
        client.put("/api/admin/settings",
                   json={"registration_mode": "open"},
                   headers=auth_header(admin["token"]))
        data = pki_register(client, "user2", "User Two")
        assert data["user"]["role"] == "user"

    def test_duplicate_username_rejected(self, client):
        admin = pki_register(client, "taken", "First")
        client.put("/api/admin/settings",
                   json={"registration_mode": "open"},
                   headers=auth_header(admin["token"]))
        identity = PKIIdentity()
        r = client.post("/api/auth/pki/challenge", json={})
        challenge = r.json()["challenge"]
        r = client.post("/api/auth/pki/register", json={
            "username": "taken",
            "display_name": "Second",
            "public_key": identity.public_key_b64url,
            "challenge": challenge,
            "signature": identity.sign(challenge),
        })
        assert r.status_code == 409

    def test_invalid_signature_rejected(self, client):
        r = client.post("/api/auth/pki/challenge", json={})
        challenge = r.json()["challenge"]
        identity = PKIIdentity()
        # Sign with one key, register with another
        other = PKIIdentity()
        r = client.post("/api/auth/pki/register", json={
            "username": "attacker",
            "display_name": "Attacker",
            "public_key": identity.public_key_b64url,
            "challenge": challenge,
            "signature": other.sign(challenge),
        })
        assert r.status_code == 401

    def test_expired_challenge_rejected(self, client):
        identity = PKIIdentity()
        r = client.post("/api/auth/pki/challenge", json={})
        challenge = r.json()["challenge"]
        sig = identity.sign(challenge)
        # Use a garbage challenge value
        r = client.post("/api/auth/pki/register", json={
            "username": "test",
            "display_name": "Test",
            "public_key": identity.public_key_b64url,
            "challenge": "bogus_challenge_value",
            "signature": sig,
        })
        assert r.status_code in (400, 401)

    def test_missing_fields_rejected(self, client):
        r = client.post("/api/auth/pki/register", json={
            "username": "test",
        })
        assert r.status_code == 400


class TestPKILogin:
    def test_login_success(self, client):
        reg = pki_register(client, "loginuser", "Login User")
        login_data = pki_login(client, reg["identity"])
        assert login_data["token"]
        assert login_data["user"]["username"] == "loginuser"

    def test_login_wrong_key(self, client):
        pki_register(client, "user1", "User One")
        unknown = PKIIdentity()
        r = client.post("/api/auth/pki/challenge",
                        json={"public_key": unknown.public_key_b64url})
        challenge = r.json()["challenge"]
        r = client.post("/api/auth/pki/login", json={
            "public_key": unknown.public_key_b64url,
            "challenge": challenge,
            "signature": unknown.sign(challenge),
        })
        assert r.status_code == 401

    def test_login_invalid_signature(self, client):
        reg = pki_register(client, "user1", "User One")
        identity = reg["identity"]
        r = client.post("/api/auth/pki/challenge",
                        json={"public_key": identity.public_key_b64url})
        challenge = r.json()["challenge"]
        other = PKIIdentity()
        r = client.post("/api/auth/pki/login", json={
            "public_key": identity.public_key_b64url,
            "challenge": challenge,
            "signature": other.sign(challenge),
        })
        assert r.status_code == 401


class TestRecoveryKeyLogin:
    def test_recovery_key_login(self, client):
        reg = pki_register(client, "recoverme", "Recover Me")
        key = reg["recovery_keys"][0]
        r = client.post("/api/auth/recovery", json={"recovery_key": key})
        assert r.status_code == 200
        data = r.json()
        assert data["token"]
        assert data["user"]["username"] == "recoverme"

    def test_recovery_key_consumed(self, client):
        reg = pki_register(client, "recoverme", "Recover Me")
        key = reg["recovery_keys"][0]
        r = client.post("/api/auth/recovery", json={"recovery_key": key})
        assert r.status_code == 200
        # Same key again should fail
        r = client.post("/api/auth/recovery", json={"recovery_key": key})
        assert r.status_code == 401

    def test_invalid_recovery_key(self, client):
        pki_register(client, "user1", "User One")
        r = client.post("/api/auth/recovery",
                        json={"recovery_key": "totally-fake-key"})
        assert r.status_code == 401


class TestSession:
    def test_authenticated_request(self, client, admin_user):
        r = client.get("/api/users/me", headers=admin_user["headers"])
        assert r.status_code == 200
        assert r.json()["username"] == "admin"

    def test_unauthenticated_request_rejected(self, client):
        r = client.get("/api/users/me")
        assert r.status_code == 401

    def test_invalid_token_rejected(self, client):
        r = client.get("/api/users/me",
                       headers=auth_header("invalid-token-value"))
        assert r.status_code == 401

    def test_logout_invalidates_session(self, client, admin_user):
        r = client.post("/api/auth/logout", headers=admin_user["headers"])
        assert r.status_code == 200
        # Token should no longer work
        r = client.get("/api/users/me", headers=admin_user["headers"])
        assert r.status_code == 401


class TestPublicEndpoints:
    def test_health_check(self, client):
        r = client.get("/api/health")
        assert r.status_code == 200
        assert r.json()["status"] == "ok"

    def test_config_endpoint(self, client):
        r = client.get("/api/config")
        assert r.status_code == 200
        data = r.json()
        assert "auth_methods" in data
        assert "registration_mode" in data
