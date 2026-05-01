# Redis pub/sub for horizontal WebSocket scaling

## Overview

When `REDIS_URL` is unset, the backend operates as a single instance with
local-only WebSocket fan-out. Setting `REDIS_URL` to a Redis 7 endpoint enables
cross-instance broadcast: every WS broadcast is published to the
`enclave:broadcast` channel, and every backend instance subscribes and
fans out to its locally-connected sockets.

## Configuration

- `REDIS_URL` — `redis://host[:port][/db]`. Empty disables Redis.
- `INSTANCE_ID` — stable identifier; defaults to a random UUID v4 at boot.
  Used to filter self-echoes. For Kubernetes deployments, set to the pod
  name.

## Topology

- Single Redis channel: `enclave:broadcast`.
- Envelope JSON: `{instance_id, topic, payload}`.
- Topics: `channel:<id>`, `space:<id>`, `presence`, `wiki:<id>`.

## Degraded mode

If Redis becomes unreachable, the publisher logs and increments
`enclave_redis_health_check_failures_total`; subscribers reconnect with
exponential backoff (1s, 2s, 4s, 8s, 30s cap). Local-only fan-out continues
unaffected. No backfill on reconnect — messages published while Redis was
down are lost.

## Metrics

Exposed at `/metrics`:

- `enclave_redis_publish_total{topic_kind}`
- `enclave_redis_subscribe_received_total{topic_kind}`
- `enclave_redis_self_echo_dropped_total`
- `enclave_redis_health_check_failures_total`
- `enclave_redis_reconnect_total`
- `enclave_redis_ok` (gauge: 1 if healthy, 0 if down)

## Rollout

1. Deploy backend with `REDIS_URL=""` first. No behavior change vs. pre-P2.1.
2. Add Redis service to compose with `--profile redis`.
3. Set `REDIS_URL=redis://redis:6379` on a single instance. Verify metrics.
4. Scale horizontally: add additional backend instances behind the same
   Redis. Verify cross-instance delivery via `enclave_redis_subscribe_received_total`.

## Rollback

Unset `REDIS_URL` and restart the affected instance. Cross-instance broadcast
stops; the local-only path is the same code as the Redis-down degraded mode.

## Verification

The multi-instance integration test under
[`tests/integration/multi_instance/`](../../tests/integration/multi_instance/)
exercises the full pub/sub path end-to-end with two backend instances and a
Redis broker. Run via `./run-tests.sh --redis-multi-instance`.
