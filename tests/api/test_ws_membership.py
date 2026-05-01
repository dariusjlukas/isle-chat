"""WS subscription audit + rate-limit tests (P1.6).

Goal A: When a user is removed from a channel (kick, leave, space-kick), they
should be unsubscribed from the channel topic immediately so they stop
receiving broadcasts.

Goal B: WS connections are rate-limited per-connection (token bucket;
burst 20, refill 10/s). When exceeded, the server replies with an error
event and drops the message.
"""

import asyncio
import json
import os

import pytest
import websockets

from conftest import auth_header


def _ws_url(base_url: str, token: str) -> str:
    # base_url is "http://127.0.0.1:<port>"; convert to ws://...
    host_port = base_url.replace("http://", "")
    return f"ws://{host_port}/ws?token={token}"


async def _drain(ws, timeout: float = 0.3):
    """Drain pending messages without waiting longer than `timeout`."""
    out = []
    try:
        while True:
            msg = await asyncio.wait_for(ws.recv(), timeout=timeout)
            out.append(json.loads(msg))
    except (asyncio.TimeoutError, websockets.ConnectionClosed):
        pass
    return out


async def _wait_for_type(ws, want_type: str, timeout: float = 2.0):
    """Wait up to `timeout` for a message with type=want_type. Returns the
    message dict, or None if it never arrives."""
    deadline = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < deadline:
        remaining = deadline - asyncio.get_event_loop().time()
        if remaining <= 0:
            break
        try:
            msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
        except (asyncio.TimeoutError, websockets.ConnectionClosed):
            return None
        try:
            j = json.loads(msg)
        except Exception:
            continue
        if j.get("type") == want_type:
            return j
    return None


# ---------------------------------------------------------------------------
# Goal A: kicked user does not receive subsequent channel broadcasts.
# ---------------------------------------------------------------------------
@pytest.mark.asyncio
async def test_kicked_user_does_not_receive_channel_broadcasts(
    client, admin_user, regular_user, base_url
):
    # admin creates a channel and adds regular user as member
    r = client.post(
        "/api/channels",
        json={"name": "kicktest", "is_public": True},
        headers=admin_user["headers"],
    )
    assert r.status_code == 200
    ch_id = r.json()["id"]
    r = client.post(
        f"/api/channels/{ch_id}/members",
        json={"user_id": regular_user["user"]["id"]},
        headers=admin_user["headers"],
    )
    assert r.status_code == 200

    # Both connect to WS
    admin_url = _ws_url(base_url, admin_user["token"])
    regular_url = _ws_url(base_url, regular_user["token"])
    async with websockets.connect(admin_url) as admin_ws, websockets.connect(
        regular_url
    ) as regular_ws:
        # Drain the initial state messages from both clients
        await _drain(admin_ws)
        await _drain(regular_ws)

        # First: confirm regular_ws receives a broadcast while still a member
        await admin_ws.send(
            json.dumps(
                {"type": "send_message", "channel_id": ch_id, "content": "hello-1"}
            )
        )
        # regular should see new_message
        msg = await _wait_for_type(regular_ws, "new_message", timeout=3.0)
        assert msg is not None, "regular user should receive broadcast pre-kick"
        assert msg["message"]["content"] == "hello-1"

        # Drain both before the kick
        await _drain(admin_ws)
        await _drain(regular_ws)

        # Now kick the regular user
        r = client.request(
            "DELETE",
            f"/api/channels/{ch_id}/members/{regular_user['user']['id']}",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200, r.text

        # Drain the kick notification(s) on the regular socket
        await _drain(regular_ws)

        # Send another message as admin
        await admin_ws.send(
            json.dumps(
                {"type": "send_message", "channel_id": ch_id, "content": "hello-2"}
            )
        )
        # admin should still see it (sender)
        msg = await _wait_for_type(admin_ws, "new_message", timeout=3.0)
        assert msg is not None, "admin (sender) should still see broadcast"

        # regular_ws should NOT receive new_message for hello-2
        leaked = await _wait_for_type(regular_ws, "new_message", timeout=1.5)
        assert leaked is None, (
            f"kicked user still received broadcast: {leaked!r}"
        )


# ---------------------------------------------------------------------------
# Goal A part 2: user kicked from a SPACE is also unsubscribed from
# channel topics within that space.
# ---------------------------------------------------------------------------
@pytest.mark.asyncio
async def test_space_kick_unsubscribes_from_space_channels(
    client, admin_user, regular_user, base_url
):
    # admin creates a space and adds regular user
    r = client.post(
        "/api/spaces", json={"name": "sk-space"}, headers=admin_user["headers"]
    )
    assert r.status_code == 200
    space_id = r.json()["id"]

    r = client.post(
        f"/api/spaces/{space_id}/members",
        json={"user_id": regular_user["user"]["id"]},
        headers=admin_user["headers"],
    )
    assert r.status_code == 200

    # Create a channel inside the space; default_join=true so members auto-join
    r = client.post(
        f"/api/spaces/{space_id}/channels",
        json={"name": "sk-chan", "default_join": True, "is_public": True},
        headers=admin_user["headers"],
    )
    assert r.status_code == 200
    ch_id = r.json()["id"]

    # Make sure regular is in the channel (default_join should have added)
    r = client.post(
        f"/api/channels/{ch_id}/members",
        json={"user_id": regular_user["user"]["id"]},
        headers=admin_user["headers"],
    )
    # 200 if added, or non-200 if already a member; either is ok
    # (the assert below doesn't rely on this specific call's outcome)

    admin_url = _ws_url(base_url, admin_user["token"])
    regular_url = _ws_url(base_url, regular_user["token"])
    async with websockets.connect(admin_url) as admin_ws, websockets.connect(
        regular_url
    ) as regular_ws:
        await _drain(admin_ws)
        await _drain(regular_ws)

        # Sanity: regular gets messages while still a space member
        await admin_ws.send(
            json.dumps(
                {"type": "send_message", "channel_id": ch_id, "content": "pre-kick"}
            )
        )
        msg = await _wait_for_type(regular_ws, "new_message", timeout=3.0)
        assert msg is not None, "regular should see channel broadcast pre-space-kick"

        await _drain(admin_ws)
        await _drain(regular_ws)

        # Kick regular from the space
        r = client.request(
            "DELETE",
            f"/api/spaces/{space_id}/members/{regular_user['user']['id']}",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200, r.text

        await _drain(regular_ws)

        # Send another channel message as admin — regular should no longer see it
        await admin_ws.send(
            json.dumps(
                {"type": "send_message", "channel_id": ch_id, "content": "post-kick"}
            )
        )
        msg = await _wait_for_type(admin_ws, "new_message", timeout=3.0)
        assert msg is not None, "admin (sender) should still see broadcast"

        leaked = await _wait_for_type(regular_ws, "new_message", timeout=1.5)
        assert leaked is None, (
            f"space-kicked user still received channel broadcast: {leaked!r}"
        )


# ---------------------------------------------------------------------------
# Goal B: per-connection rate limit (burst 20, refill 10/s) on send_message
# emits {"type":"error","reason":"rate_limit",...} once exceeded.
# ---------------------------------------------------------------------------
@pytest.mark.asyncio
async def test_rate_limit_sends_error_on_burst(
    client, admin_user, base_url
):
    # admin owns a channel of which they're the only member
    r = client.post(
        "/api/channels", json={"name": "rl-test"}, headers=admin_user["headers"]
    )
    assert r.status_code == 200
    ch_id = r.json()["id"]

    url = _ws_url(base_url, admin_user["token"])
    async with websockets.connect(url) as ws:
        await _drain(ws)

        # Burst more than the bucket size (20) without delay.
        for i in range(40):
            await ws.send(
                json.dumps(
                    {
                        "type": "send_message",
                        "channel_id": ch_id,
                        "content": f"burst-{i}",
                    }
                )
            )

        # Collect responses for ~1 second.
        end = asyncio.get_event_loop().time() + 1.0
        rate_limit_errors = []
        while asyncio.get_event_loop().time() < end:
            try:
                raw = await asyncio.wait_for(ws.recv(), timeout=0.2)
            except asyncio.TimeoutError:
                continue
            try:
                j = json.loads(raw)
            except Exception:
                continue
            if (
                j.get("type") == "error"
                and j.get("reason") == "rate_limit"
            ):
                rate_limit_errors.append(j)

        assert rate_limit_errors, (
            "expected at least one rate_limit error for burst of 40 messages"
        )
        # Sanity: the error payload includes a numeric retry hint.
        for e in rate_limit_errors:
            assert isinstance(e.get("retry_after_ms"), int)
            assert e["retry_after_ms"] >= 0
