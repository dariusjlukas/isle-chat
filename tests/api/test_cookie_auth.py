"""Tests for P1.4 Release C cookie-only session auth.

The backend issues an HttpOnly `session` cookie + non-HttpOnly `csrf` cookie
on every login flow. Cookies are the sole auth mechanism — Bearer header and
?token= query param have been removed. State-changing methods require a
matching X-CSRF-Token header (double-submit pattern).
"""

import re

import httpx
import pytest
from conftest import (PKIIdentity, auth_header, password_login,
                      password_register, pki_login, pki_register)


def _parse_set_cookie_attrs(cookie_str: str) -> dict:
    """Parse a single Set-Cookie header value into {name, value, attrs:{...}}.

    Attribute names are lowercased; flag attributes (HttpOnly, Secure) map to
    True. Returns {} if the input is empty.
    """
    if not cookie_str:
        return {}
    parts = [p.strip() for p in cookie_str.split(";")]
    name_eq, *rest = parts
    name, _, value = name_eq.partition("=")
    attrs: dict = {}
    for p in rest:
        if "=" in p:
            k, _, v = p.partition("=")
            attrs[k.lower()] = v
        else:
            attrs[p.lower()] = True
    return {"name": name, "value": value, "attrs": attrs}


def _all_set_cookies(response: httpx.Response) -> list[str]:
    """Return the raw Set-Cookie header values (one per cookie)."""
    return response.headers.get_list("set-cookie")


def _find_cookie(response: httpx.Response, name: str) -> dict | None:
    for raw in _all_set_cookies(response):
        parsed = _parse_set_cookie_attrs(raw)
        if parsed.get("name") == name:
            return parsed
    return None


class TestLoginIssuesCookies:
    def test_pki_register_sets_session_and_csrf_cookies(self, client):
        # Use a fresh client so we get the raw Set-Cookie headers without
        # httpx silently merging them into the cookie jar between calls.
        identity = PKIIdentity()
        r = client.post("/api/auth/pki/challenge", json={})
        challenge = r.json()["challenge"]
        r = client.post(
            "/api/auth/pki/register",
            json={
                "username": "cookieuser",
                "display_name": "Cookie User",
                "public_key": identity.public_key_b64url,
                "challenge": challenge,
                "signature": identity.sign(challenge),
            },
        )
        assert r.status_code == 200

        session = _find_cookie(r, "session")
        csrf = _find_cookie(r, "csrf")
        assert session is not None, "expected `session` Set-Cookie on register"
        assert csrf is not None, "expected `csrf` Set-Cookie on register"

        # session cookie is HttpOnly, SameSite=Strict, Path=/, has a Max-Age.
        assert session["attrs"].get("httponly") is True
        assert session["attrs"].get("samesite") == "Strict"
        assert session["attrs"].get("path") == "/"
        assert "max-age" in session["attrs"]

        # csrf cookie is NOT HttpOnly (frontend needs to read it via JS for
        # the double-submit X-CSRF-Token header).
        assert csrf["attrs"].get("httponly") is not True
        assert csrf["attrs"].get("samesite") == "Strict"
        assert csrf["attrs"].get("path") == "/"
        assert "max-age" in csrf["attrs"]

        # P1.4 Release C: no `token` field in the response body anymore.
        assert "token" not in r.json()

    def test_pki_login_sets_cookies(self, client):
        reg = pki_register(client, "cookielogin", "Cookie Login")
        r = client.post(
            "/api/auth/pki/challenge",
            json={"public_key": reg["identity"].public_key_b64url},
        )
        challenge = r.json()["challenge"]
        r = client.post(
            "/api/auth/pki/login",
            json={
                "public_key": reg["identity"].public_key_b64url,
                "challenge": challenge,
                "signature": reg["identity"].sign(challenge),
            },
        )
        assert r.status_code == 200
        assert _find_cookie(r, "session") is not None
        assert _find_cookie(r, "csrf") is not None

    def test_password_login_sets_cookies(self, client, admin_user):
        client.put(
            "/api/admin/settings",
            json={"auth_methods": ["passkey", "pki", "password"]},
            headers=admin_user["headers"],
        )
        password_register(
            client, "pwcookie", "PW Cookie", password="MyPass123"
        )
        r = client.post(
            "/api/auth/password/login",
            json={"username": "pwcookie", "password": "MyPass123"},
        )
        assert r.status_code == 200
        assert _find_cookie(r, "session") is not None
        assert _find_cookie(r, "csrf") is not None


class TestCookieAuthenticatedRequests:
    def test_get_users_me_with_cookie_only(self, client, base_url):
        # Use a fresh client so we can control which auth credentials get sent.
        with httpx.Client(base_url=base_url, timeout=10.0) as fresh:
            data = pki_register(fresh, "ckonly", "Cookie Only")
            # The httpx client jar already has the `session` cookie from the
            # 200 response. Make a follow-up request WITHOUT an Authorization
            # header — it should succeed via the cookie alone.
            r = fresh.get("/api/users/me")
            assert r.status_code == 200
            assert r.json()["username"] == "ckonly"

    def test_state_changing_request_without_csrf_is_403(self, client, base_url):
        # Release C: state-changing requests require X-CSRF-Token matching
        # the csrf cookie. Without it, even with a valid session cookie,
        # the request is rejected with 403.
        with httpx.Client(base_url=base_url, timeout=10.0) as fresh:
            pki_register(fresh, "csrfneg", "CSRF Negative")
            # Fresh client has both cookies in the jar but we strip the
            # X-CSRF-Token header (httpx auto-attaches the csrf cookie via
            # the Cookie header but does NOT auto-attach X-CSRF-Token).
            r = fresh.put(
                "/api/users/me", json={"display_name": "No CSRF"}
            )
            assert r.status_code == 403

    def test_state_changing_request_with_csrf_succeeds(self, client, base_url):
        with httpx.Client(base_url=base_url, timeout=10.0) as fresh:
            pki_register(fresh, "csrfok", "CSRF OK")
            csrf = fresh.cookies.get("csrf", "")
            assert csrf, "expected csrf cookie after register"
            r = fresh.put(
                "/api/users/me",
                json={"display_name": "With CSRF"},
                headers={"X-CSRF-Token": csrf},
            )
            assert r.status_code == 200


class TestBearerNoLongerWorks:
    """Release C regression: Bearer header alone (no cookie) gets 401."""

    def test_get_users_me_with_bearer_only_is_401(self, client, base_url):
        # Fresh client without the cookie jar.
        with httpx.Client(base_url=base_url, timeout=10.0) as fresh:
            reg = pki_register(fresh, "beareronly", "Bearer Only")
            token = reg["token"]

        with httpx.Client(base_url=base_url, timeout=10.0) as bearer_only:
            r = bearer_only.get(
                "/api/users/me", headers={"Authorization": f"Bearer {token}"}
            )
            assert r.status_code == 401


class TestLogoutClearsCookies:
    def test_logout_emits_clearing_set_cookie(self, client, base_url):
        with httpx.Client(base_url=base_url, timeout=10.0) as fresh:
            pki_register(fresh, "logoutuser", "Logout User")
            r = fresh.post("/api/auth/logout")
            assert r.status_code == 200
            session = _find_cookie(r, "session")
            csrf = _find_cookie(r, "csrf")
            # Cookies are cleared by emitting an empty value with Max-Age=0.
            assert session is not None
            assert csrf is not None
            assert session["value"] == ""
            assert session["attrs"].get("max-age") == "0"
            assert csrf["value"] == ""
            assert csrf["attrs"].get("max-age") == "0"
