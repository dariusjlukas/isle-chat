#!/usr/bin/env bash
# Self-contained load test runner.
#
# Starts its own PostgreSQL container and backend server, runs the C++ load
# tester, then cleans everything up. Does NOT touch any existing running
# services or databases.
#
# Usage:
#   ./run-load-test.sh [--profile NAME] [--scenario NAME] [--release]
#   ./run-load-test.sh --profile stress --scenario auth_load
#   ./run-load-test.sh --release          # Test against Release backend build
#   ./run-load-test.sh                    # defaults to 'ci' profile, all scenarios

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
LOAD_DIR="$SCRIPT_DIR/tests/load"
LOAD_BUILD_DIR="$LOAD_DIR/build"
LOAD_BINARY="$LOAD_BUILD_DIR/loadtest"

# Colors
if [ -t 1 ]; then
  RED='\033[0;31m'; GREEN='\033[0;32m'; BLUE='\033[0;34m'; BOLD='\033[1m'; NC='\033[0m'
else
  RED='' GREEN='' BLUE='' BOLD='' NC=''
fi

# Defaults
PROFILE="ci"
SCENARIO=""
CSV_DIR="$LOAD_DIR/reports"
USE_RELEASE=true
REBUILD_LOAD=false

# Isolated test infrastructure
PG_CONTAINER="loadtest-postgres-$$"
PG_PORT=5434
PG_USER=loadtest
PG_PASS=loadtestpass
PG_DB=loadtest
BACKEND_PORT=9098
UPLOAD_DIR=""
BACKEND_PID=""

cleanup() {
  printf "\n${BLUE}Cleaning up...${NC}\n"
  # Stop backend
  if [ -n "$BACKEND_PID" ]; then
    kill "$BACKEND_PID" 2>/dev/null || true
    wait "$BACKEND_PID" 2>/dev/null || true
  fi
  # Remove upload dir
  if [ -n "$UPLOAD_DIR" ] && [ -d "$UPLOAD_DIR" ]; then
    rm -rf "$UPLOAD_DIR"
  fi
  # Stop and remove PostgreSQL container
  docker rm -f "$PG_CONTAINER" &>/dev/null || true
}
trap cleanup EXIT

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile|-p) PROFILE="$2"; shift 2 ;;
    --scenario|-s) SCENARIO="$2"; shift 2 ;;
    --csv-dir) CSV_DIR="$2"; shift 2 ;;
    --debug) USE_RELEASE=false; shift ;;
    --rebuild) REBUILD_LOAD=true; shift ;;
    --help)
      echo "Usage: $0 [options]"
      echo ""
      echo "  --profile NAME    Load profile (default: ci)"
      echo "  --scenario NAME   Run only this scenario (default: all)"
      echo "  --csv-dir DIR     CSV output directory (default: tests/load/reports)"
      echo "  --debug           Build and test against Debug backend (default: Release)"
      echo "  --rebuild         Force rebuild of load tester"
      echo ""
      echo "Scenarios: auth_load, messaging, rest_api_mix, file_upload, search, realistic"
      echo "Profiles: baseline, moderate, stress, spike, ci, max_throughput"
      exit 0
      ;;
    *) echo "Unknown argument: $1"; exit 1 ;;
  esac
done

FAILED=0

# =====================================================================
# Step 1: Build backend
# =====================================================================

if [ "$USE_RELEASE" = true ]; then
  BACKEND_BUILD_DIR="$BACKEND_DIR/build-release"
  BUILD_TYPE="Release"
else
  BACKEND_BUILD_DIR="$BACKEND_DIR/build"
  BUILD_TYPE="Debug"
fi
BACKEND_BINARY="$BACKEND_BUILD_DIR/chat-server"

if [ ! -x "$BACKEND_BINARY" ]; then
  printf "${BLUE}${BOLD}Building backend (%s)...${NC}\n" "$BUILD_TYPE"
  (cd "$BACKEND_DIR" && cmake -B "$(basename "$BACKEND_BUILD_DIR")" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" && \
   cmake --build "$(basename "$BACKEND_BUILD_DIR")" -j"$(nproc)")
  if [ ! -x "$BACKEND_BINARY" ]; then
    printf "${RED}Error: Backend build failed.${NC}\n"
    exit 1
  fi
fi

# =====================================================================
# Step 2: Build load tester
# =====================================================================

if [ "$REBUILD_LOAD" = true ]; then
  rm -rf "$LOAD_BUILD_DIR"
fi

if [ ! -x "$LOAD_BINARY" ]; then
  printf "${BLUE}Building C++ load tester...${NC}\n"
  cmake -B "$LOAD_BUILD_DIR" -S "$LOAD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$LOAD_BUILD_DIR" -j"$(nproc)"
fi

# =====================================================================
# Step 3: Start PostgreSQL container
# =====================================================================

printf "\n${BLUE}Starting isolated PostgreSQL container on port %s...${NC}\n" "$PG_PORT"
docker run -d --rm \
  --name "$PG_CONTAINER" \
  -e POSTGRES_USER="$PG_USER" \
  -e POSTGRES_PASSWORD="$PG_PASS" \
  -e POSTGRES_DB="$PG_DB" \
  -p "$PG_PORT:5432" \
  postgres:16-alpine \
  -c max_locks_per_transaction=512 \
  -c max_connections=200 >/dev/null

# Wait for PostgreSQL to be ready
printf "${BLUE}Waiting for PostgreSQL...${NC}"
PG_READY=false
for i in $(seq 1 30); do
  if docker exec "$PG_CONTAINER" pg_isready -U "$PG_USER" &>/dev/null; then
    PG_READY=true
    break
  fi
  printf "."
  sleep 1
done
echo ""

if [ "$PG_READY" = false ]; then
  printf "${RED}Error: PostgreSQL failed to start within 30s.${NC}\n"
  exit 1
fi

# =====================================================================
# Step 4: Start backend server
# =====================================================================

UPLOAD_DIR=$(mktemp -d)

printf "${BLUE}Starting backend server (%s) on port %s...${NC}\n" "$BUILD_TYPE" "$BACKEND_PORT"
env -i \
  HOME="$HOME" \
  PATH="$PATH" \
  BACKEND_PORT="$BACKEND_PORT" \
  POSTGRES_HOST=localhost \
  POSTGRES_PORT="$PG_PORT" \
  POSTGRES_USER="$PG_USER" \
  POSTGRES_PASSWORD="$PG_PASS" \
  POSTGRES_DB="$PG_DB" \
  UPLOAD_DIR="$UPLOAD_DIR" \
  "$BACKEND_BINARY" &
BACKEND_PID=$!

# Wait for backend to be ready (use /api/config which requires DB access,
# not /api/health which is a static response)
printf "${BLUE}Waiting for backend...${NC}"
BACKEND_READY=false
for i in $(seq 1 20); do
  if curl -sf "http://127.0.0.1:$BACKEND_PORT/api/config" >/dev/null 2>&1; then
    BACKEND_READY=true
    break
  fi
  printf "."
  sleep 1
done
echo ""

if [ "$BACKEND_READY" = false ]; then
  printf "${RED}Error: Backend server failed to start within 20s.${NC}\n"
  exit 1
fi

printf "${GREEN}Infrastructure ready.${NC}\n"

# =====================================================================
# Step 5: Run load tests
# =====================================================================

mkdir -p "$CSV_DIR"

HOST="http://127.0.0.1:$BACKEND_PORT"
CMD="$LOAD_BINARY --host $HOST --profile $PROFILE --csv-dir $CSV_DIR --config-dir $LOAD_DIR/config"
if [[ -n "$SCENARIO" ]]; then
  CMD="$CMD --scenario $SCENARIO"
fi

echo ""
export POSTGRES_HOST=localhost
export POSTGRES_PORT=$PG_PORT
export POSTGRES_USER=$PG_USER
export POSTGRES_PASSWORD=$PG_PASS
export POSTGRES_DB=$PG_DB

$CMD
LOAD_EXIT=$?

# =====================================================================
# Step 6: Run validation
# =====================================================================

if command -v python3 &>/dev/null && [[ -f "$LOAD_DIR/validate.py" ]]; then
  echo ""
  printf "${BLUE}Running validation...${NC}\n"
  cd "$LOAD_DIR"

  python3 validate.py --reports-dir "$CSV_DIR" --host "$HOST" || true
fi

# Cleanup happens via trap
exit $LOAD_EXIT
