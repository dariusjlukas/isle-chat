"""Shared fixtures for black-box API tests.

The test suite expects a running backend server and PostgreSQL instance.
Configure via environment variables (see SERVER_URL / DB vars below).
"""

import base64
import os
import subprocess
import signal
import socket
import time
import uuid

import httpx
import psycopg2
import pytest
from cryptography.hazmat.primitives.asymmetric.ec import ECDSA, EllipticCurvePrivateKey, SECP256R1, generate_private_key
from cryptography.hazmat.primitives.hashes import SHA256
from cryptography.hazmat.primitives.serialization import (
    Encoding,
    NoEncryption,
    PublicFormat,
)
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

# ---------------------------------------------------------------------------
# Configuration from environment
# ---------------------------------------------------------------------------
BACKEND_PORT = int(os.environ.get("TEST_BACKEND_PORT", "9099"))
SERVER_URL = os.environ.get("TEST_SERVER_URL", f"http://127.0.0.1:{BACKEND_PORT}")
BACKEND_BINARY = os.environ.get("TEST_BACKEND_BINARY", "")

PG_HOST = os.environ.get("POSTGRES_HOST", "localhost")
PG_PORT = os.environ.get("POSTGRES_PORT", "5433")
PG_USER = os.environ.get("POSTGRES_USER", "chatapp_test")
PG_PASS = os.environ.get("POSTGRES_PASSWORD", "testpassword")
PG_DB = os.environ.get("POSTGRES_DB", "chatapp_test")


# ---------------------------------------------------------------------------
# Helpers: P-256 ECDSA PKI auth (matches frontend Web Crypto API)
# ---------------------------------------------------------------------------
def _base64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


class PKIIdentity:
    """A P-256 ECDSA key pair that can sign challenges for PKI auth.

    Matches the frontend's Web Crypto API format:
    - Public key: SPKI DER bytes, base64url-encoded
    - Signature: raw r||s (IEEE P1363), base64url-encoded
    """

    def __init__(self):
        self._private_key = generate_private_key(SECP256R1())
        spki_der = self._private_key.public_key().public_bytes(
            Encoding.DER, PublicFormat.SubjectPublicKeyInfo
        )
        self.public_key_b64url = _base64url_encode(spki_der)

    def sign(self, message: str) -> str:
        """Sign message with ECDSA P-256 SHA-256, return base64url raw r||s."""
        der_sig = self._private_key.sign(message.encode(), ECDSA(SHA256()))
        # Convert DER signature to raw r||s (IEEE P1363) format
        r, s = decode_dss_signature(der_sig)
        raw_sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")
        return _base64url_encode(raw_sig)


def pki_register(client: httpx.Client, username: str, display_name: str,
                 identity: PKIIdentity | None = None,
                 token: str | None = None) -> dict:
    """Register a new user via PKI and return {token, user, recovery_keys, identity}."""
    if identity is None:
        identity = PKIIdentity()

    # Step 1: get a challenge (must send JSON body, even if empty)
    r = client.post("/api/auth/pki/challenge", json={})
    r.raise_for_status()
    challenge = r.json()["challenge"]

    # Step 2: sign and register
    sig = identity.sign(challenge)
    body: dict = {
        "username": username,
        "display_name": display_name,
        "public_key": identity.public_key_b64url,
        "challenge": challenge,
        "signature": sig,
    }
    if token is not None:
        body["token"] = token

    r = client.post("/api/auth/pki/register", json=body)
    r.raise_for_status()
    data = r.json()
    data["identity"] = identity
    return data


def pki_login(client: httpx.Client, identity: PKIIdentity) -> dict:
    """Login via PKI and return {token, user}."""
    r = client.post("/api/auth/pki/challenge",
                    json={"public_key": identity.public_key_b64url})
    r.raise_for_status()
    challenge = r.json()["challenge"]

    sig = identity.sign(challenge)
    r = client.post("/api/auth/pki/login", json={
        "public_key": identity.public_key_b64url,
        "challenge": challenge,
        "signature": sig,
    })
    r.raise_for_status()
    return r.json()


def auth_header(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


# ---------------------------------------------------------------------------
# Database helpers
# ---------------------------------------------------------------------------
def get_pg_dsn() -> str:
    return f"host={PG_HOST} port={PG_PORT} dbname={PG_DB} user={PG_USER} password={PG_PASS}"


def reset_database():
    """Drop all user-created data between test modules."""
    conn = psycopg2.connect(get_pg_dsn())
    conn.autocommit = True
    cur = conn.cursor()
    # Truncate all application tables (preserve schema).
    cur.execute("""
        DO $$
        DECLARE r RECORD;
        BEGIN
            FOR r IN
                SELECT tablename FROM pg_tables
                WHERE schemaname = 'public'
                  AND tablename NOT IN ('schema_migrations')
            LOOP
                EXECUTE 'TRUNCATE TABLE ' || quote_ident(r.tablename) || ' CASCADE';
            END LOOP;
        END $$;
    """)
    cur.close()
    conn.close()


# ---------------------------------------------------------------------------
# Server lifecycle
# ---------------------------------------------------------------------------
def _wait_for_port(port: int, host: str = "127.0.0.1", timeout: float = 15.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return
        except OSError:
            time.sleep(0.2)
    raise TimeoutError(f"Server not reachable on {host}:{port} after {timeout}s")


@pytest.fixture(scope="session")
def server_process():
    """Start the backend binary if TEST_BACKEND_BINARY is set, otherwise assume it's already running."""
    proc = None
    if BACKEND_BINARY:
        env = {
            **os.environ,
            "BACKEND_PORT": str(BACKEND_PORT),
            "POSTGRES_HOST": PG_HOST,
            "POSTGRES_PORT": PG_PORT,
            "POSTGRES_USER": PG_USER,
            "POSTGRES_PASSWORD": PG_PASS,
            "POSTGRES_DB": PG_DB,
            "SESSION_EXPIRY_HOURS": "168",
        }
        proc = subprocess.Popen(
            [BACKEND_BINARY],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        try:
            _wait_for_port(BACKEND_PORT)
        except TimeoutError:
            proc.kill()
            stdout = proc.stdout.read().decode() if proc.stdout else ""
            raise RuntimeError(f"Backend failed to start:\n{stdout}")
    else:
        _wait_for_port(BACKEND_PORT)

    yield proc

    if proc is not None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


# ---------------------------------------------------------------------------
# HTTP client fixtures
# ---------------------------------------------------------------------------
@pytest.fixture(scope="session")
def base_url():
    return SERVER_URL


@pytest.fixture(scope="session")
def client(server_process, base_url):
    """A shared httpx client for the test session."""
    with httpx.Client(base_url=base_url, timeout=10.0) as c:
        yield c


@pytest.fixture(autouse=True)
def _clean_db():
    """Reset database before each test function."""
    reset_database()


# ---------------------------------------------------------------------------
# Convenience fixtures for common user setups
# ---------------------------------------------------------------------------
@pytest.fixture()
def admin_user(client):
    """Register the first user and promote to owner via DB. Return auth info."""
    data = pki_register(client, "admin", "Admin User")
    headers = auth_header(data["token"])
    # The first PKI user gets role "admin", promote to "owner" via DB
    # so tests can exercise owner-only endpoints.
    conn = psycopg2.connect(get_pg_dsn())
    conn.autocommit = True
    cur = conn.cursor()
    cur.execute("UPDATE users SET role = 'owner' WHERE id = %s",
                (data["user"]["id"],))
    cur.close()
    conn.close()
    data["user"]["role"] = "owner"
    # Set registration mode to open so subsequent users can register freely
    client.put("/api/admin/settings",
               json={"registration_mode": "open"}, headers=headers)
    return {
        "token": data["token"],
        "user": data["user"],
        "identity": data["identity"],
        "headers": headers,
    }


@pytest.fixture()
def regular_user(client, admin_user):
    """Register a second (non-admin) user. Requires admin_user to exist first."""
    data = pki_register(client, "regular", "Regular User")
    return {
        "token": data["token"],
        "user": data["user"],
        "identity": data["identity"],
        "headers": auth_header(data["token"]),
    }


@pytest.fixture()
def second_regular_user(client, admin_user):
    """Register a third user for multi-user tests."""
    data = pki_register(client, "regular2", "Regular User 2")
    return {
        "token": data["token"],
        "user": data["user"],
        "identity": data["identity"],
        "headers": auth_header(data["token"]),
    }
