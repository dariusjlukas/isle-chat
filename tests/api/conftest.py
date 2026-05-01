"""Shared fixtures for black-box API tests.

The test suite expects a running backend server and PostgreSQL instance.
Configure via environment variables (see SERVER_URL / DB vars below).

When running with pytest-xdist (-n N), each worker gets its own database
and backend server for full isolation.
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

# Base port for xdist workers (worker 0 gets BASE+0, worker 1 gets BASE+1, etc.)
XDIST_PORT_BASE = int(os.environ.get("TEST_XDIST_PORT_BASE", "9200"))


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


# P1.4 Release C: cookie-only auth.
#
# Each register/login call captures the session and CSRF cookies the server
# set. Tests then construct an explicit Cookie + X-CSRF-Token header pair via
# `auth_header(...)` for whichever user they want to act as. The shared client
# jar is cleared after each capture so cookies from one user don't bleed into
# another user's subsequent registration.
def _capture_session(client: httpx.Client) -> tuple[str, str]:
    session = client.cookies.get("session", "")
    csrf = client.cookies.get("csrf", "")
    # Clear so the next register/login on the shared client starts fresh.
    client.cookies.clear()
    return session, csrf


def pki_register(client: httpx.Client, username: str, display_name: str,
                 identity: PKIIdentity | None = None,
                 token: str | None = None) -> dict:
    """Register a new user via PKI and return {token, csrf, user, recovery_keys, identity}."""
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
    session, csrf = _capture_session(client)
    data["token"] = session
    data["csrf"] = csrf
    return data


def pki_login(client: httpx.Client, identity: PKIIdentity) -> dict:
    """Login via PKI and return {token, csrf, user}."""
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
    data = r.json()
    session, csrf = _capture_session(client)
    data["token"] = session
    data["csrf"] = csrf
    return data


def password_register(client: httpx.Client, username: str, display_name: str,
                      password: str = "TestPass123!",
                      token: str | None = None) -> dict:
    """Register a new user via password and return {token, csrf, user}."""
    body: dict = {
        "username": username,
        "display_name": display_name,
        "password": password,
    }
    if token is not None:
        body["token"] = token

    r = client.post("/api/auth/password/register", json=body)
    r.raise_for_status()
    data = r.json()
    session, csrf = _capture_session(client)
    data["token"] = session
    data["csrf"] = csrf
    return data


def password_login(client: httpx.Client, username: str,
                   password: str = "TestPass123!") -> dict:
    """Login via password and return {token, csrf, user}."""
    r = client.post("/api/auth/password/login", json={
        "username": username,
        "password": password,
    })
    r.raise_for_status()
    data = r.json()
    session, csrf = _capture_session(client)
    data["token"] = session
    data["csrf"] = csrf
    return data


def auth_header(token: str, csrf: str = "") -> dict:
    """Build an explicit Cookie + X-CSRF-Token header pair for a given user.

    Pass the captured session token; csrf defaults to a stable synthetic value
    that the backend's csrf_ok helper will accept (it just checks header ==
    cookie). Tests don't typically need to vary CSRF, so the default is fine.
    """
    csrf_value = csrf or "test-csrf"
    return {
        "Cookie": f"session={token}; csrf={csrf_value}",
        "X-CSRF-Token": csrf_value,
    }


# ---------------------------------------------------------------------------
# Database helpers
# ---------------------------------------------------------------------------
def _get_pg_dsn(dbname: str = PG_DB) -> str:
    return f"host={PG_HOST} port={PG_PORT} dbname={dbname} user={PG_USER} password={PG_PASS}"


def _reset_database(dbname: str = PG_DB):
    """Drop all user-created data between test modules."""
    conn = psycopg2.connect(_get_pg_dsn(dbname))
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


# Keep old names for backwards compatibility with test files that import them
def get_pg_dsn() -> str:
    return _get_pg_dsn()


def reset_database():
    _reset_database()


# ---------------------------------------------------------------------------
# xdist worker helpers
# ---------------------------------------------------------------------------
def _worker_index(worker_id: str) -> int | None:
    """Extract numeric index from xdist worker_id like 'gw0', 'gw1', etc.
    Returns None when not running under xdist ('master')."""
    if worker_id == "master":
        return None
    return int(worker_id.replace("gw", ""))


SQITCH_DEPLOY_SQL = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "sqitch",
    "deploy",
    "0001-initial-schema.sql",
)


def _apply_sqitch_deploy(dbname: str):
    """Apply sqitch's initial schema script to a freshly created worker DB.

    The deploy script has explicit BEGIN/COMMIT, but psycopg2 with autocommit=True
    runs each statement independently — the BEGIN/COMMIT pair becomes a no-op
    transaction wrapper, which is fine. SET statements are session-scoped.
    """
    with open(SQITCH_DEPLOY_SQL, "r") as f:
        sql = f.read()
    conn = psycopg2.connect(_get_pg_dsn(dbname))
    conn.autocommit = True
    cur = conn.cursor()
    cur.execute(sql)
    cur.close()
    conn.close()


def _create_worker_db(dbname: str):
    """Create a fresh database for this xdist worker and apply the sqitch schema.

    P1.1 PR-B (sqitch cutover): the backend now defaults to ENABLE_SQITCH_ONLY=1
    and skips its embedded run_migrations(). Tests apply sqitch's initial schema
    here so per-worker DBs are usable as soon as the backend connects.
    """
    conn = psycopg2.connect(_get_pg_dsn("postgres"))
    conn.autocommit = True
    cur = conn.cursor()
    cur.execute(f"DROP DATABASE IF EXISTS {dbname}")
    cur.execute(f"CREATE DATABASE {dbname} OWNER {PG_USER}")
    cur.close()
    conn.close()
    _apply_sqitch_deploy(dbname)


def _drop_worker_db(dbname: str):
    """Drop the worker's database."""
    conn = psycopg2.connect(_get_pg_dsn("postgres"))
    conn.autocommit = True
    cur = conn.cursor()
    cur.execute(f"DROP DATABASE IF EXISTS {dbname}")
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
def worker_db(worker_id):
    """Per-worker database. Under xdist each worker gets an isolated DB;
    without xdist falls back to the default PG_DB."""
    idx = _worker_index(worker_id)
    if idx is None:
        yield PG_DB
        return

    dbname = f"chatapp_apitest_w{idx}"
    _create_worker_db(dbname)
    yield dbname
    _drop_worker_db(dbname)


@pytest.fixture(scope="session")
def worker_port(worker_id):
    """Per-worker backend port."""
    idx = _worker_index(worker_id)
    if idx is None:
        return BACKEND_PORT
    return XDIST_PORT_BASE + idx


@pytest.fixture(scope="session")
def server_process(worker_db, worker_port):
    """Start a backend server for this worker (or use the externally-started one)."""
    proc = None
    if BACKEND_BINARY or _worker_index(os.environ.get("PYTEST_XDIST_WORKER", "master")) is not None:
        binary = BACKEND_BINARY or os.environ.get("TEST_BUILD_DIR", "") + "/chat-server"
        if not binary or not os.path.isfile(binary):
            # No binary available — assume externally managed server
            _wait_for_port(worker_port)
            yield None
            return

        import tempfile
        upload_dir = tempfile.mkdtemp()
        env = {
            **os.environ,
            "BACKEND_PORT": str(worker_port),
            "POSTGRES_HOST": PG_HOST,
            "POSTGRES_PORT": PG_PORT,
            "POSTGRES_USER": PG_USER,
            "POSTGRES_PASSWORD": PG_PASS,
            "POSTGRES_DB": worker_db,
            "UPLOAD_DIR": upload_dir,
            "DB_POOL_SIZE": "2",
            "SESSION_EXPIRY_HOURS": "168",
            # P1.1 PR-B: sqitch is now the canonical schema source. Conftest
            # applies the deploy script in _create_worker_db; the backend
            # asserts the schema is initialized at boot.
            "ENABLE_SQITCH_ONLY": "1",
        }
        proc = subprocess.Popen(
            [binary],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        try:
            _wait_for_port(worker_port)
        except TimeoutError:
            proc.kill()
            stdout = proc.stdout.read().decode() if proc.stdout else ""
            raise RuntimeError(f"Backend failed to start on port {worker_port}:\n{stdout}")
    else:
        _wait_for_port(worker_port)

    yield proc

    if proc is not None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        import shutil
        shutil.rmtree(upload_dir, ignore_errors=True)


# ---------------------------------------------------------------------------
# HTTP client fixtures
# ---------------------------------------------------------------------------
@pytest.fixture(scope="session")
def base_url(worker_port):
    return f"http://127.0.0.1:{worker_port}"


@pytest.fixture(scope="session")
def client(server_process, base_url):
    """A shared httpx client for the test session."""
    with httpx.Client(base_url=base_url, timeout=10.0) as c:
        yield c


@pytest.fixture(autouse=True)
def _clean_db(worker_db):
    """Reset database before each test function."""
    _reset_database(worker_db)


# ---------------------------------------------------------------------------
# Convenience fixtures for common user setups
# ---------------------------------------------------------------------------
@pytest.fixture()
def admin_user(client, worker_db):
    """Register the first user and promote to owner via DB. Return auth info."""
    data = pki_register(client, "admin", "Admin User")
    headers = auth_header(data["token"], data.get("csrf", ""))
    # The first user should automatically get the "owner" role
    assert data["user"]["role"] == "owner"
    # Set registration mode to open so subsequent users can register freely
    client.put("/api/admin/settings",
               json={"registration_mode": "open"}, headers=headers)
    return {
        "token": data["token"],
        "csrf": data.get("csrf", ""),
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
        "csrf": data.get("csrf", ""),
        "user": data["user"],
        "identity": data["identity"],
        "headers": auth_header(data["token"], data.get("csrf", "")),
    }


@pytest.fixture()
def second_regular_user(client, admin_user):
    """Register a third user for multi-user tests."""
    data = pki_register(client, "regular2", "Regular User 2")
    return {
        "token": data["token"],
        "csrf": data.get("csrf", ""),
        "user": data["user"],
        "identity": data["identity"],
        "headers": auth_header(data["token"], data.get("csrf", "")),
    }
