# Multi-instance Redis pub/sub integration test

Brings up postgres + redis + sqitch + two backend instances pointed at the
same Redis, then drives three scenarios end-to-end:

- **Scenario A** — cross-instance broadcast: a message posted via backend1 is
  delivered to a WebSocket connected to backend2 within 200ms.
- **Scenario B** — degraded mode: with Redis paused, a backend's local
  fan-out continues to work; cross-instance delivery stops. After unpausing
  Redis, cross-instance delivery resumes.
- **Scenario C** — self-echo filter: the sender's own WebSocket receives its
  message exactly once (no duplicate from the Redis round-trip).

## Prerequisites

- Docker + docker compose
- Python 3 with `pip` (the runner creates a `.venv` automatically)

## Run

```
./run.sh
```

The orchestrator builds images, brings the stack up, runs the scenarios, and
tears everything down regardless of pass/fail. The exit code reflects the
test result.

This is also wired into the top-level test runner:

```
./run-tests.sh --redis-multi-instance
```

## Files

- `docker-compose.test.yml` — self-contained stack
- `run.sh` — orchestration wrapper (build, wait, drive, teardown)
- `test_scenarios.py` — Python test driver
- `requirements.txt` — pinned Python deps
