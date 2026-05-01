#!/usr/bin/env bash
# Orchestrate the multi-instance Redis pub/sub integration test.
#
#   1. Bring up postgres + redis + sqitch + backend1 + backend2.
#   2. Wait for both backends to report healthy via /api/health.
#   3. Drive scenarios A/B/C from test_scenarios.py.
#   4. Tear everything down (always).
#
# Exit code reflects the test outcome.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPOSE="docker compose -f $SCRIPT_DIR/docker-compose.test.yml"
PROJECT="enclave-mi-test"
COMPOSE="$COMPOSE -p $PROJECT"

if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; BOLD='\033[1m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BLUE=''; BOLD=''; NC=''
fi

cleanup() {
    printf "\n${BLUE}[multi-instance] tearing down stack...${NC}\n"
    $COMPOSE down -v --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

# Sanity: ensure docker is available.
if ! command -v docker >/dev/null 2>&1; then
    printf "${RED}ERROR: docker not found in PATH.${NC}\n" >&2
    exit 2
fi

printf "${BLUE}${BOLD}[multi-instance] building images and starting stack...${NC}\n"
if ! $COMPOSE up -d --build; then
    printf "${RED}ERROR: failed to bring up the test stack.${NC}\n" >&2
    $COMPOSE logs --no-color --tail 200 >&2 || true
    exit 1
fi

# Wait up to 90s for both backends to be healthy.
wait_backend() {
    local port="$1" name="$2" deadline=$(( $(date +%s) + 90 ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        if curl -sf "http://127.0.0.1:${port}/api/health" >/dev/null 2>&1; then
            printf "${GREEN}[multi-instance] %s is up on :%s${NC}\n" "$name" "$port"
            return 0
        fi
        sleep 1
    done
    printf "${RED}ERROR: %s did not become ready on :%s within 90s.${NC}\n" "$name" "$port" >&2
    $COMPOSE logs --no-color --tail 200 "$name" >&2 || true
    return 1
}

wait_backend 9101 backend1 || exit 1
wait_backend 9102 backend2 || exit 1

# Set up Python venv and run scenarios.
VENV="$SCRIPT_DIR/.venv"
if [ ! -d "$VENV" ]; then
    printf "${BLUE}[multi-instance] creating Python venv...${NC}\n"
    python3 -m venv "$VENV" || { printf "${RED}python3 venv creation failed${NC}\n" >&2; exit 2; }
fi
"$VENV/bin/pip" install -q --disable-pip-version-check -r "$SCRIPT_DIR/requirements.txt" || {
    printf "${RED}pip install failed${NC}\n" >&2
    exit 2
}

printf "${BLUE}${BOLD}[multi-instance] running scenarios A/B/C...${NC}\n"
COMPOSE_PROJECT="$PROJECT" \
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.test.yml" \
"$VENV/bin/python" "$SCRIPT_DIR/test_scenarios.py"
RC=$?

if [ $RC -eq 0 ]; then
    printf "${GREEN}${BOLD}[multi-instance] PASS${NC}\n"
else
    printf "${RED}${BOLD}[multi-instance] FAIL (rc=%s)${NC}\n" "$RC"
    $COMPOSE logs --no-color --tail 100 backend1 backend2 redis >&2 || true
fi

exit $RC
