#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
FRONTEND_DIR="$SCRIPT_DIR/frontend"
BUILD_DIR="$BACKEND_DIR/build"

# Colors (disabled if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' BLUE='' BOLD='' NC=''
fi

# Results tracking
declare -A RESULTS
FAILED=0

run_check() {
    local name="$1"
    shift
    printf "\n${BLUE}${BOLD}=== %s ===${NC}\n" "$name"
    if "$@"; then
        RESULTS["$name"]="PASS"
        printf -- "${GREEN}--- %s: PASSED ---${NC}\n" "$name"
    else
        RESULTS["$name"]="FAIL"
        FAILED=1
        printf -- "${RED}--- %s: FAILED ---${NC}\n" "$name"
    fi
}

print_summary() {
    local order=("Frontend Lint" "Frontend Type Check" "Frontend Format Check"
                 "Frontend E2E Tests"
                 "Backend Build" "Backend Unit Tests" "Backend Integration Tests")

    printf "\n${BOLD}========================================${NC}\n"
    printf "${BOLD}  TEST RESULTS SUMMARY${NC}\n"
    printf "${BOLD}========================================${NC}\n"

    for name in "${order[@]}"; do
        local status="${RESULTS[$name]:-SKIP}"
        local color="$YELLOW"
        if [ "$status" = "PASS" ]; then color="$GREEN"; fi
        if [ "$status" = "FAIL" ]; then color="$RED"; fi
        printf "  %-30s ${color}%s${NC}\n" "$name" "$status"
    done

    printf "${BOLD}========================================${NC}\n"
    if [ "$FAILED" -eq 0 ]; then
        printf "  ${GREEN}${BOLD}Result: ALL PASSED${NC}\n"
    else
        printf "  ${RED}${BOLD}Result: FAILED${NC}\n"
    fi
    printf "${BOLD}========================================${NC}\n"
}

usage() {
    cat <<EOF
Usage: ./run-tests.sh [OPTIONS]

Run all project tests and checks with a summary report.

Options:
  --all              Run everything (default if no flags given)
  --frontend         Run all frontend checks (lint, typecheck, format)
  --backend          Run all backend tests (build + unit + integration)
  --backend-unit     Run only backend unit tests (builds if needed)
  --backend-integ    Run only backend integration tests (builds if needed)
  --lint             Run frontend lint check only
  --typecheck        Run frontend type check only
  --format           Run frontend format check only
  --e2e              Run Playwright UI tests only
  --no-build         Skip the backend CMake build step
  --help             Show this help message

Examples:
  ./run-tests.sh                    # Run everything
  ./run-tests.sh --frontend         # Frontend checks only (lint, typecheck, format, e2e)
  ./run-tests.sh --e2e              # Playwright UI tests only
  ./run-tests.sh --backend-unit     # Build and run backend unit tests
  ./run-tests.sh --lint --typecheck # Run specific frontend checks
EOF
}

# Parse arguments
RUN_LINT=false
RUN_TYPECHECK=false
RUN_FORMAT=false
RUN_E2E=false
RUN_BACKEND_UNIT=false
RUN_BACKEND_INTEG=false
SKIP_BUILD=false
ANY_FLAG=false

for arg in "$@"; do
    case "$arg" in
        --all)
            RUN_LINT=true; RUN_TYPECHECK=true; RUN_FORMAT=true; RUN_E2E=true
            RUN_BACKEND_UNIT=true; RUN_BACKEND_INTEG=true
            ANY_FLAG=true ;;
        --frontend)
            RUN_LINT=true; RUN_TYPECHECK=true; RUN_FORMAT=true; RUN_E2E=true
            ANY_FLAG=true ;;
        --backend)
            RUN_BACKEND_UNIT=true; RUN_BACKEND_INTEG=true
            ANY_FLAG=true ;;
        --backend-unit)
            RUN_BACKEND_UNIT=true; ANY_FLAG=true ;;
        --backend-integ)
            RUN_BACKEND_INTEG=true; ANY_FLAG=true ;;
        --lint)
            RUN_LINT=true; ANY_FLAG=true ;;
        --typecheck)
            RUN_TYPECHECK=true; ANY_FLAG=true ;;
        --format)
            RUN_FORMAT=true; ANY_FLAG=true ;;
        --e2e)
            RUN_E2E=true; ANY_FLAG=true ;;
        --no-build)
            SKIP_BUILD=true ;;
        --help)
            usage; exit 0 ;;
        *)
            printf "${RED}Unknown option: %s${NC}\n" "$arg"
            usage; exit 1 ;;
    esac
done

# Default: run everything
if [ "$ANY_FLAG" = false ]; then
    RUN_LINT=true; RUN_TYPECHECK=true; RUN_FORMAT=true; RUN_E2E=true
    RUN_BACKEND_UNIT=true; RUN_BACKEND_INTEG=true
fi

NEED_BACKEND=$( [ "$RUN_BACKEND_UNIT" = true ] || [ "$RUN_BACKEND_INTEG" = true ] && echo true || echo false )

printf "${BOLD}Chat App Test Runner${NC}\n"

# --- Frontend checks ---

if [ "$RUN_LINT" = true ]; then
    run_check "Frontend Lint" bash -c "cd '$FRONTEND_DIR' && npm run lint"
fi

if [ "$RUN_TYPECHECK" = true ]; then
    run_check "Frontend Type Check" bash -c "cd '$FRONTEND_DIR' && npm run typecheck"
fi

if [ "$RUN_FORMAT" = true ]; then
    run_check "Frontend Format Check" bash -c "cd '$FRONTEND_DIR' && npm run format:check"
fi

if [ "$RUN_E2E" = true ]; then
    run_check "Frontend E2E Tests" bash -c "cd '$FRONTEND_DIR' && npx playwright test"
fi

# --- Backend tests ---

if [ "$NEED_BACKEND" = true ]; then
    # Check for library dependencies
    if [ ! -d "$BACKEND_DIR/libs/uWebSockets" ] || [ ! -d "$BACKEND_DIR/libs/json" ]; then
        printf "\n${RED}Error: Backend library dependencies not found.${NC}\n"
        printf "Run the following to set up:\n"
        printf "  cd backend && mkdir -p libs\n"
        printf "  git clone --depth 1 --recurse-submodules https://github.com/uNetworking/uWebSockets.git libs/uWebSockets\n"
        printf "  git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git libs/json\n"
        FAILED=1
        RESULTS["Backend Build"]="FAIL"
        if [ "$RUN_BACKEND_UNIT" = true ]; then RESULTS["Backend Unit Tests"]="SKIP"; fi
        if [ "$RUN_BACKEND_INTEG" = true ]; then RESULTS["Backend Integration Tests"]="SKIP"; fi
    else
        # Build
        BUILD_OK=true
        if [ "$SKIP_BUILD" = false ]; then
            run_check "Backend Build" bash -c "cd '$BACKEND_DIR' && cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON && cmake --build build -j\$(nproc)"
            if [ "${RESULTS["Backend Build"]}" = "FAIL" ]; then
                BUILD_OK=false
            fi
        fi

        if [ "$BUILD_OK" = true ]; then
            if [ "$RUN_BACKEND_UNIT" = true ]; then
                run_check "Backend Unit Tests" bash -c "cd '$BUILD_DIR' && ctest --output-on-failure -L unit --timeout 30"
            fi

            if [ "$RUN_BACKEND_INTEG" = true ]; then
                # Spin up an isolated PostgreSQL container for integration tests
                TEST_PG_CONTAINER="chatapp-test-postgres-$$"
                TEST_PG_PORT=5433
                TEST_PG_USER=chatapp_test
                TEST_PG_PASS=testpassword
                TEST_PG_DB=chatapp_test

                cleanup_test_pg() {
                    if docker inspect "$TEST_PG_CONTAINER" &>/dev/null; then
                        printf "\n${BLUE}Stopping test PostgreSQL container...${NC}\n"
                        docker rm -f "$TEST_PG_CONTAINER" &>/dev/null
                    fi
                }
                trap cleanup_test_pg EXIT

                printf "\n${BLUE}Starting test PostgreSQL container on port %s...${NC}\n" "$TEST_PG_PORT"
                docker run -d --rm \
                    --name "$TEST_PG_CONTAINER" \
                    -e POSTGRES_USER="$TEST_PG_USER" \
                    -e POSTGRES_PASSWORD="$TEST_PG_PASS" \
                    -e POSTGRES_DB="$TEST_PG_DB" \
                    -p "$TEST_PG_PORT:5432" \
                    postgres:16-alpine >/dev/null

                # Wait for PostgreSQL to be ready
                printf "${BLUE}Waiting for PostgreSQL to be ready...${NC}\n"
                PG_READY=false
                for i in $(seq 1 30); do
                    if docker exec "$TEST_PG_CONTAINER" pg_isready -U "$TEST_PG_USER" &>/dev/null; then
                        PG_READY=true
                        break
                    fi
                    sleep 1
                done

                if [ "$PG_READY" = false ]; then
                    printf "${RED}Error: Test PostgreSQL container failed to start within 30s.${NC}\n"
                    RESULTS["Backend Integration Tests"]="FAIL"
                    FAILED=1
                else
                    run_check "Backend Integration Tests" bash -c "
                        export POSTGRES_HOST=localhost
                        export POSTGRES_PORT=$TEST_PG_PORT
                        export POSTGRES_USER=$TEST_PG_USER
                        export POSTGRES_PASSWORD=$TEST_PG_PASS
                        export POSTGRES_DB=$TEST_PG_DB
                        cd '$BUILD_DIR' && ctest --output-on-failure -L integration --timeout 60
                    "
                fi

                cleanup_test_pg
                trap - EXIT
            fi
        else
            if [ "$RUN_BACKEND_UNIT" = true ]; then RESULTS["Backend Unit Tests"]="SKIP"; fi
            if [ "$RUN_BACKEND_INTEG" = true ]; then RESULTS["Backend Integration Tests"]="SKIP"; fi
        fi
    fi
fi

# --- Summary ---
print_summary

exit "$FAILED"
