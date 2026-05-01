"""Multi-instance Redis pub/sub integration scenarios.

Drives backend1 (http://localhost:9101) and backend2 (http://localhost:9102)
which share a Postgres + Redis. The two backends run with distinct
INSTANCE_IDs so the self-echo filter has something to filter.

Scenarios:
  A. Cross-instance broadcast: user_a connects via backend1, user_b connects
     via backend2; user_a posts a message; user_b receives it within 200ms.
  B. Redis-down -> local-only fallback: pause redis, send another message;
     user_a still receives it locally; user_b does NOT (Redis is down).
     Unpause redis; cross-instance delivery resumes within ~30s.
  C. Self-echo filter: user_a's own WS receives its own message exactly once.

All assertions are explicit; the script prints a clear PASS/FAIL summary and
exits 0 on success, non-zero otherwise.
"""

from __future__ import annotations

import asyncio
import base64
import json
import os
import subprocess
import sys
import uuid
from dataclasses import dataclass, field

import httpx
import websockets
from cryptography.hazmat.primitives.asymmetric.ec import (
    ECDSA,
    SECP256R1,
    generate_private_key,
)
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
from cryptography.hazmat.primitives.hashes import SHA256
from cryptography.hazmat.primitives.serialization import (
    Encoding,
    PublicFormat,
)

BACKEND1_URL = os.environ.get("BACKEND1_URL", "http://127.0.0.1:9101")
BACKEND2_URL = os.environ.get("BACKEND2_URL", "http://127.0.0.1:9102")
COMPOSE_FILE = os.environ.get("COMPOSE_FILE", "docker-compose.test.yml")
COMPOSE_PROJECT = os.environ.get("COMPOSE_PROJECT", "enclave-mi-test")

CROSS_INSTANCE_TIMEOUT_S = 0.5  # generous; expectation is <200ms but tolerate jitter
LOCAL_ONLY_NEGATIVE_TIMEOUT_S = 1.5
RECONNECT_TIMEOUT_S = 30.0


# ---------------------------------------------------------------------------
# PKI helpers (mirror tests/api/conftest.py)
# ---------------------------------------------------------------------------
def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


class PKIIdentity:
    def __init__(self):
        self._priv = generate_private_key(SECP256R1())
        self.public_key_b64url = b64url(
            self._priv.public_key().public_bytes(
                Encoding.DER, PublicFormat.SubjectPublicKeyInfo
            )
        )

    def sign(self, message: str) -> str:
        der = self._priv.sign(message.encode(), ECDSA(SHA256()))
        r, s = decode_dss_signature(der)
        return b64url(r.to_bytes(32, "big") + s.to_bytes(32, "big"))


@dataclass
class Session:
    base_url: str
    user_id: str
    cookie_session: str
    cookie_csrf: str
    identity: PKIIdentity
    user: dict = field(default_factory=dict)

    @property
    def cookie_header(self) -> str:
        return f"session={self.cookie_session}; csrf={self.cookie_csrf}"

    @property
    def auth_headers(self) -> dict:
        return {
            "Cookie": self.cookie_header,
            "X-CSRF-Token": self.cookie_csrf,
        }


def pki_register(client: httpx.Client, base_url: str, username: str) -> Session:
    identity = PKIIdentity()
    r = client.post(f"{base_url}/api/auth/pki/challenge", json={})
    r.raise_for_status()
    challenge = r.json()["challenge"]
    sig = identity.sign(challenge)
    r = client.post(
        f"{base_url}/api/auth/pki/register",
        json={
            "username": username,
            "display_name": username,
            "public_key": identity.public_key_b64url,
            "challenge": challenge,
            "signature": sig,
        },
    )
    r.raise_for_status()
    body = r.json()
    cookies = client.cookies
    session = cookies.get("session", "")
    csrf = cookies.get("csrf", "")
    if not session:
        raise RuntimeError(
            f"register did not set session cookie (body={body!r}, cookies={dict(cookies)})"
        )
    client.cookies.clear()
    return Session(
        base_url=base_url,
        user_id=body.get("user", {}).get("id", ""),
        cookie_session=session,
        cookie_csrf=csrf,
        identity=identity,
        user=body.get("user", {}),
    )


# ---------------------------------------------------------------------------
# WS helpers
# ---------------------------------------------------------------------------
def ws_url(base_url: str) -> str:
    host_port = base_url.replace("http://", "").replace("https://", "")
    return f"ws://{host_port}/ws"


async def ws_connect(session: Session):
    """Open a WS connection authenticated by the user's cookie."""
    url = ws_url(session.base_url)
    return await websockets.connect(
        url,
        additional_headers={"Cookie": session.cookie_header},
    )


async def drain(ws, timeout: float = 0.3):
    """Drain pending messages without blocking longer than `timeout`."""
    out = []
    try:
        while True:
            msg = await asyncio.wait_for(ws.recv(), timeout=timeout)
            try:
                out.append(json.loads(msg))
            except Exception:
                pass
    except (asyncio.TimeoutError, websockets.ConnectionClosed):
        pass
    return out


async def wait_for_new_message(ws, content: str, timeout: float):
    """Wait up to `timeout` seconds for a `new_message` event with matching content."""
    deadline = asyncio.get_event_loop().time() + timeout
    while True:
        remaining = deadline - asyncio.get_event_loop().time()
        if remaining <= 0:
            return None
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=remaining)
        except (asyncio.TimeoutError, websockets.ConnectionClosed):
            return None
        try:
            msg = json.loads(raw)
        except Exception:
            continue
        if msg.get("type") == "new_message" and msg.get("message", {}).get("content") == content:
            return msg


async def count_new_messages(ws, content: str, window_s: float) -> int:
    """Count `new_message` events with matching content over a fixed window."""
    deadline = asyncio.get_event_loop().time() + window_s
    n = 0
    while True:
        remaining = deadline - asyncio.get_event_loop().time()
        if remaining <= 0:
            return n
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=remaining)
        except (asyncio.TimeoutError, websockets.ConnectionClosed):
            return n
        try:
            msg = json.loads(raw)
        except Exception:
            continue
        if msg.get("type") == "new_message" and msg.get("message", {}).get("content") == content:
            n += 1


async def send_message(ws, channel_id: str, content: str):
    await ws.send(
        json.dumps({"type": "send_message", "channel_id": channel_id, "content": content})
    )


# ---------------------------------------------------------------------------
# Compose helpers
# ---------------------------------------------------------------------------
def compose(*args: str, check: bool = True) -> subprocess.CompletedProcess:
    cmd = ["docker", "compose", "-p", COMPOSE_PROJECT, "-f", COMPOSE_FILE, *args]
    return subprocess.run(cmd, check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def pause_redis() -> None:
    compose("pause", "redis")


def unpause_redis() -> None:
    compose("unpause", "redis")


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------
async def run_all() -> int:
    failures: list[str] = []
    log = lambda msg: print(f"[multi-instance] {msg}", flush=True)  # noqa: E731

    suffix = uuid.uuid4().hex[:8]
    user_a_name = f"user_a_{suffix}"
    user_b_name = f"user_b_{suffix}"

    # Same client object talks to both backends; we just pass full base_urls.
    with httpx.Client(timeout=15.0) as client:
        log(f"registering {user_a_name} on backend1 (becomes owner — first user)")
        a = pki_register(client, BACKEND1_URL, user_a_name)
        if a.user.get("role") != "owner":
            print(
                f"[multi-instance] WARN: first user role={a.user.get('role')!r} (expected 'owner')",
                flush=True,
            )

        # Open registration so user_b can register on backend2.
        log("user_a setting registration_mode=open")
        r = client.put(
            f"{BACKEND1_URL}/api/admin/settings",
            json={"registration_mode": "open"},
            headers=a.auth_headers,
        )
        r.raise_for_status()

        log(f"registering {user_b_name} on backend2")
        b = pki_register(client, BACKEND2_URL, user_b_name)

        log("user_a creating channel via backend1")
        r = client.post(
            f"{BACKEND1_URL}/api/channels",
            json={"name": f"mi-{suffix}", "is_public": True},
            headers=a.auth_headers,
        )
        r.raise_for_status()
        ch_id = r.json()["id"]

        log(f"user_a adding user_b ({b.user_id}) to channel {ch_id}")
        r = client.post(
            f"{BACKEND1_URL}/api/channels/{ch_id}/members",
            json={"user_id": b.user_id},
            headers=a.auth_headers,
        )
        r.raise_for_status()

    # ---- Open WS connections ----
    log("user_a -> ws backend1")
    ws_a = await ws_connect(a)
    log("user_b -> ws backend2")
    ws_b = await ws_connect(b)

    try:
        # Drain initial state.
        await drain(ws_a)
        await drain(ws_b)

        # ---------------- Scenario A ----------------
        log("Scenario A: cross-instance broadcast")
        content_a = f"hello-cross-{suffix}"
        await send_message(ws_a, ch_id, content_a)

        got_b = await wait_for_new_message(ws_b, content_a, CROSS_INSTANCE_TIMEOUT_S)
        if got_b is None:
            failures.append(
                f"Scenario A: user_b on backend2 did not receive '{content_a}' "
                f"within {CROSS_INSTANCE_TIMEOUT_S}s"
            )
            log("  FAIL: user_b did not receive cross-instance message")
        else:
            log("  PASS: user_b received cross-instance message")

        # ---------------- Scenario C ----------------
        # We folded scenario C in here because we already have ws_a connected
        # and a message in flight. Verify user_a saw the message exactly once.
        log("Scenario C: self-echo filter (user_a sees own message exactly once)")
        # We already drained before sending, so any new_message events in the
        # window must be the just-sent one. Count them over a short window.
        n_a = await count_new_messages(ws_a, content_a, window_s=0.6)
        if n_a == 1:
            log("  PASS: user_a saw own message exactly once")
        elif n_a == 0:
            failures.append(
                "Scenario C: user_a did not receive own message (local fan-out broken?)"
            )
            log("  FAIL: user_a did not receive own message")
        else:
            failures.append(
                f"Scenario C: user_a received own message {n_a} times "
                "(self-echo filter not working)"
            )
            log(f"  FAIL: user_a saw own message {n_a} times")

        # Drain everything so scenario B starts clean.
        await drain(ws_a)
        await drain(ws_b)

        # ---------------- Scenario B ----------------
        log("Scenario B: pausing redis, expect local-only delivery")
        pause_redis()
        # Give the subscribers a moment to notice the disconnect (not strictly
        # required for the test logic, but reduces log noise).
        await asyncio.sleep(0.5)

        content_b = f"redis-down-{suffix}"
        await send_message(ws_a, ch_id, content_b)

        # user_a must still receive its own broadcast (local fan-out).
        got_a = await wait_for_new_message(ws_a, content_b, timeout=2.0)
        if got_a is None:
            failures.append(
                "Scenario B: user_a did not receive own message during Redis-down "
                "(local fan-out broken)"
            )
            log("  FAIL: user_a did not receive own message during Redis-down")
        else:
            log("  PASS: user_a received own message during Redis-down (local fan-out)")

        # user_b must NOT receive it (Redis is down, no cross-instance path).
        leaked = await wait_for_new_message(ws_b, content_b, LOCAL_ONLY_NEGATIVE_TIMEOUT_S)
        if leaked is not None:
            failures.append(
                "Scenario B: user_b received cross-instance message while Redis was paused "
                "(no degraded mode?)"
            )
            log("  FAIL: user_b received cross-instance message during Redis-down")
        else:
            log("  PASS: user_b did NOT receive cross-instance message during Redis-down")

        log("Scenario B: unpausing redis, waiting for pub/sub to recover")
        unpause_redis()

        # Poll: send messages every 2s up to RECONNECT_TIMEOUT_S until user_b
        # receives one. The first few may be lost while subscribers reconnect;
        # any successful delivery within the window is a pass.
        recovered = False
        attempts = 0
        deadline = asyncio.get_event_loop().time() + RECONNECT_TIMEOUT_S
        while asyncio.get_event_loop().time() < deadline:
            attempts += 1
            content_c = f"recovered-{suffix}-{attempts}"
            await send_message(ws_a, ch_id, content_c)
            got = await wait_for_new_message(ws_b, content_c, timeout=2.0)
            if got is not None:
                log(f"  PASS: pub/sub recovered after {attempts} attempt(s)")
                recovered = True
                break
            # Drain user_a's own echo so we don't pile up.
            await drain(ws_a, timeout=0.1)

        if not recovered:
            failures.append(
                f"Scenario B: pub/sub did not recover within {RECONNECT_TIMEOUT_S}s"
            )
            log("  FAIL: pub/sub did not recover after unpause")

    finally:
        try:
            await ws_a.close()
        except Exception:
            pass
        try:
            await ws_b.close()
        except Exception:
            pass

    # ---- Summary ----
    print("", flush=True)
    print("[multi-instance] ===== summary =====", flush=True)
    if failures:
        print(f"[multi-instance] FAIL ({len(failures)} failure(s)):", flush=True)
        for f in failures:
            print(f"  - {f}", flush=True)
        return 1
    print("[multi-instance] PASS — all scenarios green", flush=True)
    return 0


def main() -> int:
    try:
        return asyncio.run(run_all())
    except Exception as e:
        # Best-effort: try to unpause redis in case scenario B left it paused.
        try:
            unpause_redis()
        except Exception:
            pass
        print(f"[multi-instance] FAIL — driver crashed: {e!r}", file=sys.stderr, flush=True)
        return 2


if __name__ == "__main__":
    sys.exit(main())
