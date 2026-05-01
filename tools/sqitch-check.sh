#!/usr/bin/env bash
# Smoke-tests the sqitch deploy script against a fresh Postgres 16 container.
#
# Pre-Phase-2 this script compared sqitch's output to the embedded
# Database::run_migrations() output as a load-bearing equivalence check.
# Since P1.1 PR-B (sqitch cutover) the embedded DDL is a no-op stub, so
# there is nothing to compare against. Instead, this script verifies:
#
#   1. The deploy SQL applies cleanly to an empty PG.
#   2. All expected sentinel tables exist after deploy.
#   3. The verify SQL passes.
#
# Usage: ./tools/sqitch-check.sh
# Requires: docker (in PATH), sqitch directory at sqitch/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

PG_PORT=5441
TMPDIR="$(mktemp -d)"
PG_NAME=sqitch-check-pg

cleanup() {
  set +e
  docker rm -f "$PG_NAME" >/dev/null 2>&1
  rm -rf "$TMPDIR"
}
trap cleanup EXIT

if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: docker not found in PATH." >&2
  exit 1
fi

echo "[sqitch-check] Starting fresh postgres container..."
docker run --rm -d --name "$PG_NAME" \
  -e POSTGRES_PASSWORD=check -e POSTGRES_DB=enclave -e POSTGRES_USER=postgres \
  -p "$PG_PORT:5432" postgres:16-alpine >/dev/null

echo "[sqitch-check] Waiting for postgres to be ready..."
until docker exec "$PG_NAME" pg_isready -U postgres -d enclave >/dev/null 2>&1; do
  sleep 0.3
done

echo "[sqitch-check] Applying sqitch deploy script..."
docker exec -i "$PG_NAME" psql -U postgres -d enclave -v ON_ERROR_STOP=1 \
  < sqitch/deploy/0001-initial-schema.sql > "$TMPDIR/deploy.log" 2>&1

echo "[sqitch-check] Running verify checks..."
docker exec -i "$PG_NAME" psql -U postgres -d enclave -v ON_ERROR_STOP=1 \
  < sqitch/verify/0001-initial-schema.sql > "$TMPDIR/verify.log" 2>&1

# Confirm a sample of the expected 54 application tables exist.
EXPECTED_TABLES=(users channels messages sessions spaces webauthn_credentials \
                  password_credentials totp_credentials user_keys server_settings)
MISSING=()
for t in "${EXPECTED_TABLES[@]}"; do
  if ! docker exec "$PG_NAME" psql -U postgres -d enclave -tAc \
        "SELECT 1 FROM pg_tables WHERE schemaname='public' AND tablename='$t'" \
        | grep -q '^1$'; then
    MISSING+=("$t")
  fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
  echo "[sqitch-check] FAIL — missing tables after deploy: ${MISSING[*]}" >&2
  exit 1
fi

echo "[sqitch-check] OK — sqitch deploy applied cleanly, all sentinel tables exist."
exit 0
