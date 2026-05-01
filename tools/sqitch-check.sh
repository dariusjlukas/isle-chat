#!/usr/bin/env bash
# Validates that sqitch/deploy/0001-initial-schema.sql produces a schema
# byte-identical to what the backend's run_migrations() produces against
# the same fresh Postgres. This is the load-bearing check that PR-B
# (cutover to ENABLE_SQITCH_ONLY=1) will not regress the schema.
#
# Spins up two ephemeral postgres-16 containers in parallel, applies sqitch
# to one and runs the backend against the other, then pg_dumps both and
# diffs the schemas. Emits a diff to stderr and exits non-zero if the
# schemas differ.
#
# Usage: ./tools/sqitch-check.sh
# Requires: docker (in PATH), backend binary at backend/build/chat-server,
# sqitch directory at sqitch/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

# Pick non-default ports to avoid collisions
PG_SQITCH_PORT=5441
PG_INIT_PORT=5442
BACKEND_PORT=9098
TMPDIR="$(mktemp -d)"
SQITCH_PG=sqitch-check-pg-sqitch
INIT_PG=sqitch-check-pg-init

cleanup() {
  set +e
  if [ -n "${BACKEND_PID:-}" ]; then kill "$BACKEND_PID" 2>/dev/null; fi
  docker rm -f "$SQITCH_PG" "$INIT_PG" >/dev/null 2>&1
  rm -rf "$TMPDIR"
}
trap cleanup EXIT

if [ ! -x backend/build/chat-server ]; then
  echo "ERROR: backend/build/chat-server not found. Build the backend first." >&2
  exit 1
fi
if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: docker not found in PATH." >&2
  exit 1
fi

echo "[sqitch-check] Starting two postgres containers..."
docker run --rm -d --name "$SQITCH_PG" \
  -e POSTGRES_PASSWORD=check -e POSTGRES_DB=enclave -e POSTGRES_USER=postgres \
  -p "$PG_SQITCH_PORT:5432" postgres:16-alpine >/dev/null
docker run --rm -d --name "$INIT_PG" \
  -e POSTGRES_PASSWORD=check -e POSTGRES_DB=enclave -e POSTGRES_USER=postgres \
  -p "$PG_INIT_PORT:5432" postgres:16-alpine >/dev/null

echo "[sqitch-check] Waiting for containers to be ready..."
for c in "$SQITCH_PG" "$INIT_PG"; do
  until docker exec "$c" pg_isready -U postgres -d enclave >/dev/null 2>&1; do
    sleep 0.3
  done
done

echo "[sqitch-check] Applying sqitch deploy to container 1..."
docker exec -i "$SQITCH_PG" psql -U postgres -d enclave -v ON_ERROR_STOP=1 \
  < sqitch/deploy/0001-initial-schema.sql > "$TMPDIR/sqitch-apply.log" 2>&1

echo "[sqitch-check] Booting backend against container 2 (run_migrations)..."
mkdir -p "$TMPDIR/uploads"
POSTGRES_USER=postgres POSTGRES_PASSWORD=check POSTGRES_DB=enclave \
POSTGRES_HOST=localhost POSTGRES_PORT="$PG_INIT_PORT" \
BACKEND_PORT="$BACKEND_PORT" UPLOAD_DIR="$TMPDIR/uploads" \
ASAN_OPTIONS=detect_leaks=0 \
backend/build/chat-server > "$TMPDIR/backend.log" 2>&1 &
BACKEND_PID=$!

# Wait for /api/health
for _ in $(seq 1 30); do
  if curl -fs "http://localhost:$BACKEND_PORT/api/health" >/dev/null 2>&1; then
    break
  fi
  sleep 0.5
done
kill "$BACKEND_PID" 2>/dev/null
wait "$BACKEND_PID" 2>/dev/null || true
BACKEND_PID=""

echo "[sqitch-check] Dumping both schemas..."
docker exec "$SQITCH_PG" pg_dump -U postgres -d enclave \
  --schema-only --no-owner --no-privileges --no-comments \
  --quote-all-identifiers > "$TMPDIR/sqitch.sql"
docker exec "$INIT_PG" pg_dump -U postgres -d enclave \
  --schema-only --no-owner --no-privileges --no-comments \
  --quote-all-identifiers > "$TMPDIR/init.sql"

# Strip dump-version metadata so the diff is meaningful
strip_meta() {
  sed -e '/^\\restrict/d' \
      -e '/^\\unrestrict/d' \
      -e '/^-- Dumped from/d' \
      -e '/^-- Dumped by/d' "$1"
}
strip_meta "$TMPDIR/sqitch.sql" > "$TMPDIR/sqitch.clean"
strip_meta "$TMPDIR/init.sql" > "$TMPDIR/init.clean"

if diff -u "$TMPDIR/init.clean" "$TMPDIR/sqitch.clean" > "$TMPDIR/schema.diff"; then
  echo "[sqitch-check] OK — sqitch and run_migrations() produce identical schemas."
  exit 0
fi

echo "[sqitch-check] FAIL — schemas differ. Diff (init -> sqitch):" >&2
cat "$TMPDIR/schema.diff" >&2
exit 1
