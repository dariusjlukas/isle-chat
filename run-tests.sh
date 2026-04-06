#!/usr/bin/env bash
set -uo pipefail

CI="${CI:-}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
FRONTEND_DIR="$SCRIPT_DIR/frontend"
BUILD_DIR="$BACKEND_DIR/build"
API_TESTS_DIR="$SCRIPT_DIR/tests/api"
E2E_TESTS_DIR="$SCRIPT_DIR/tests/e2e"

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
MISSING_DEPS=()
FAILED=0

# Temporary directory for parallel job output
PARALLEL_TMPDIR=$(mktemp -d)
trap 'rm -rf "$PARALLEL_TMPDIR"' EXIT

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

# Run a check in the background, capturing output to a temp file.
# Usage: run_check_bg "Name" command args...
# Sets BGPID_<sanitized_name> and BGFILE_<sanitized_name>
run_check_bg() {
    local name="$1"
    shift
    local safe_name
    safe_name=$(echo "$name" | tr ' ' '_' | tr -cd 'A-Za-z0-9_')
    local outfile="$PARALLEL_TMPDIR/$safe_name.out"
    (
        if "$@" >"$outfile" 2>&1; then
            echo "PASS" > "$PARALLEL_TMPDIR/$safe_name.result"
        else
            echo "FAIL" > "$PARALLEL_TMPDIR/$safe_name.result"
        fi
    ) &
    eval "BGPID_${safe_name}=$!"
    eval "BGFILE_${safe_name}=$outfile"
    eval "BGNAME_${safe_name}='$name'"
}

# Wait for a background check and report its result.
# Usage: wait_check_bg "Name"
wait_check_bg() {
    local name="$1"
    local safe_name
    safe_name=$(echo "$name" | tr ' ' '_' | tr -cd 'A-Za-z0-9_')
    local pid_var="BGPID_${safe_name}"
    local file_var="BGFILE_${safe_name}"
    local result_file="$PARALLEL_TMPDIR/$safe_name.result"

    wait "${!pid_var}" 2>/dev/null

    printf "\n${BLUE}${BOLD}=== %s ===${NC}\n" "$name"
    cat "${!file_var}" 2>/dev/null

    if [ -f "$result_file" ] && [ "$(cat "$result_file")" = "PASS" ]; then
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
                 "Frontend Build" "Backend Build" "Backend Static Analysis"
                 "Backend Unit Tests" "Backend Integration Tests"
                 "API Tests" "E2E Tests" "Load Tests" "Load Tests (Release)"
                 "Docker Build")

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

    if [ "${#MISSING_DEPS[@]}" -gt 0 ]; then
        printf "\n${YELLOW}${BOLD}Some checks were skipped due to missing dependencies:${NC}\n"
        for dep in "${MISSING_DEPS[@]}"; do
            printf "  ${YELLOW}- %s${NC}\n" "$dep"
        done
        printf "\n"
    fi
}

usage() {
    cat <<EOF
Usage: ./run-tests.sh [OPTIONS]

Run all project tests and checks with a summary report.

Options:
  --all              Run everything (default if no flags given)
  --frontend         Run all frontend checks (lint, typecheck, format, build)
  --backend          Run all backend tests (build + unit + integration)
  --backend-unit     Run only backend unit tests (builds if needed)
  --backend-integ    Run only backend integration tests (builds if needed)
  --lint             Run frontend lint check only
  --typecheck        Run frontend type check only
  --format           Run frontend format check only
  --build            Run frontend production build only
  --api-tests        Run black-box API tests (needs backend build + PostgreSQL)
  --e2e              Run Playwright E2E tests (needs backend + frontend + PostgreSQL)
  --parallel N       Run API/E2E tests with N parallel workers (each gets own backend/DB)
  --static-analysis  Run C++ static analysis (clang-tidy)
  --load-tests       Run performance/load tests against debug build (C++)
  --load-tests-release  Run performance/load tests against optimized Release build
  --docker           Run Docker container builds
  --no-build         Skip the backend CMake build step
  --help             Show this help message

Examples:
  ./run-tests.sh                    # Run everything
  ./run-tests.sh --frontend         # Frontend checks only
  ./run-tests.sh --backend-unit     # Build and run backend unit tests
  ./run-tests.sh --lint --typecheck # Run specific frontend checks
  ./run-tests.sh --api-tests        # Black-box API endpoint tests
  ./run-tests.sh --e2e              # Playwright E2E tests
  ./run-tests.sh --e2e --parallel 3 # E2E tests with 3 parallel workers
  ./run-tests.sh --load-tests-release # Load tests against Release build
  ./run-tests.sh --docker           # Build Docker containers
EOF
}

# Parse arguments
RUN_LINT=false
RUN_TYPECHECK=false
RUN_FORMAT=false
RUN_FE_BUILD=false
RUN_BACKEND_UNIT=false
RUN_BACKEND_INTEG=false
RUN_API_TESTS=false
RUN_E2E=false
RUN_DOCKER=false
RUN_STATIC_ANALYSIS=false
RUN_LOAD_TESTS=false
RUN_LOAD_TESTS_RELEASE=false
SKIP_BUILD=false
E2E_WORKERS=16
API_WORKERS=64
ANY_FLAG=false
NEXT_IS_WORKERS=false

for arg in "$@"; do
    if [ "$NEXT_IS_WORKERS" = true ]; then
        E2E_WORKERS="$arg"
        API_WORKERS="$arg"
        NEXT_IS_WORKERS=false
        continue
    fi
    case "$arg" in
        --all)
            RUN_LINT=true; RUN_TYPECHECK=true; RUN_FORMAT=true; RUN_FE_BUILD=true
            RUN_BACKEND_UNIT=true; RUN_BACKEND_INTEG=true; RUN_API_TESTS=true; RUN_E2E=true; RUN_DOCKER=true; RUN_STATIC_ANALYSIS=true
            ANY_FLAG=true ;;
        --frontend)
            RUN_LINT=true; RUN_TYPECHECK=true; RUN_FORMAT=true; RUN_FE_BUILD=true
            ANY_FLAG=true ;;
        --backend)
            RUN_BACKEND_UNIT=true; RUN_BACKEND_INTEG=true; RUN_API_TESTS=true
            ANY_FLAG=true ;;
        --backend-unit)
            RUN_BACKEND_UNIT=true; ANY_FLAG=true ;;
        --backend-integ)
            RUN_BACKEND_INTEG=true; ANY_FLAG=true ;;
        --api-tests)
            RUN_API_TESTS=true; ANY_FLAG=true ;;
        --e2e)
            RUN_E2E=true; ANY_FLAG=true ;;
        --lint)
            RUN_LINT=true; ANY_FLAG=true ;;
        --typecheck)
            RUN_TYPECHECK=true; ANY_FLAG=true ;;
        --format)
            RUN_FORMAT=true; ANY_FLAG=true ;;
        --build)
            RUN_FE_BUILD=true; ANY_FLAG=true ;;
        --parallel)
            NEXT_IS_WORKERS=true ;;
        --static-analysis)
            RUN_STATIC_ANALYSIS=true; ANY_FLAG=true ;;
        --load-tests)
            RUN_LOAD_TESTS=true; ANY_FLAG=true ;;
        --load-tests-release)
            RUN_LOAD_TESTS_RELEASE=true; ANY_FLAG=true ;;
        --docker)
            RUN_DOCKER=true; ANY_FLAG=true ;;
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
    RUN_LINT=true; RUN_TYPECHECK=true; RUN_FORMAT=true; RUN_FE_BUILD=true
    RUN_BACKEND_UNIT=true; RUN_BACKEND_INTEG=true; RUN_API_TESTS=true; RUN_E2E=true; RUN_DOCKER=true; RUN_STATIC_ANALYSIS=true
fi

NEED_BACKEND=$( [ "$RUN_BACKEND_UNIT" = true ] || [ "$RUN_BACKEND_INTEG" = true ] || [ "$RUN_API_TESTS" = true ] || [ "$RUN_E2E" = true ] || [ "$RUN_LOAD_TESTS" = true ] && echo true || echo false )
NEED_PG=$( [ "$RUN_BACKEND_INTEG" = true ] || [ "$RUN_API_TESTS" = true ] || [ "$RUN_E2E" = true ] || [ "$RUN_LOAD_TESTS" = true ] || [ "$RUN_LOAD_TESTS_RELEASE" = true ] && echo true || echo false )

printf "${BOLD}Chat App Test Runner${NC}\n"

# =====================================================================
# Phase 1: Run frontend lint/typecheck/format AND backend build in parallel
# =====================================================================

FE_PARALLEL_CHECKS=()

# Run formatters before checking, so format:check only fails on unfixable issues
# Skip in CI (e.g. GitHub Actions) — CI should only check, not fix
if [ "$RUN_FORMAT" = true ] && [ "$CI" != "true" ]; then
    printf "\n${BLUE}${BOLD}=== Running Frontend Formatter ===${NC}\n"
    (cd "$FRONTEND_DIR" && npm run format) || true
fi

if [ "$NEED_BACKEND" = true ] && [ "$CI" != "true" ]; then
    if command -v clang-format &>/dev/null; then
        printf "\n${BLUE}${BOLD}=== Running Backend Formatter ===${NC}\n"
        find "$BACKEND_DIR/src" -type f \( -name '*.cpp' -o -name '*.h' \) -exec clang-format -i {} +
    else
        MISSING_DEPS+=("clang-format (backend auto-formatting)")
    fi
fi

if [ "$RUN_LINT" = true ]; then
    run_check_bg "Frontend Lint" bash -c "cd '$FRONTEND_DIR' && npm run lint"
    FE_PARALLEL_CHECKS+=("Frontend Lint")
fi

if [ "$RUN_TYPECHECK" = true ]; then
    run_check_bg "Frontend Type Check" bash -c "cd '$FRONTEND_DIR' && npm run typecheck"
    FE_PARALLEL_CHECKS+=("Frontend Type Check")
fi

if [ "$RUN_FORMAT" = true ]; then
    run_check_bg "Frontend Format Check" bash -c "cd '$FRONTEND_DIR' && npm run format:check"
    FE_PARALLEL_CHECKS+=("Frontend Format Check")
fi

# Start backend build in parallel with frontend checks
BUILD_OK=true
BACKEND_BUILD_BG=false
if [ "$NEED_BACKEND" = true ] && [ "$SKIP_BUILD" = false ]; then
    if [ ! -d "$BACKEND_DIR/libs/uWebSockets" ] || [ ! -d "$BACKEND_DIR/libs/json" ]; then
        printf "\n${RED}Error: Backend library dependencies not found.${NC}\n"
        printf "Run the following to initialize submodules:\n"
        printf "  git submodule update --init --recursive\n"
        FAILED=1
        RESULTS["Backend Build"]="FAIL"
        BUILD_OK=false
    else
        COVERAGE_FLAG=""
        SANITIZER_FLAG=""
        if [ "$RUN_BACKEND_UNIT" = true ]; then
            COVERAGE_FLAG="-DCODE_COVERAGE=ON"
            # Enable sanitizers only if the runtime library is available
            if gcc -fsanitize=address,undefined -x c -c /dev/null -o /dev/null &>/dev/null && \
               [ -e "$(gcc -print-file-name=libasan.so)" ]; then
                SANITIZER_FLAG="-DSANITIZERS=ON"
            else
                MISSING_DEPS+=("libasan + libubsan (runtime sanitizers for unit tests)")
            fi
        fi
        run_check_bg "Backend Build" bash -c "cd '$BACKEND_DIR' && cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON $COVERAGE_FLAG $SANITIZER_FLAG && cmake --build build -j\$(nproc)"
        BACKEND_BUILD_BG=true
    fi
fi

# Start static analysis build in parallel (separate build directory)
STATIC_ANALYSIS_BG=false
if [ "$RUN_STATIC_ANALYSIS" = true ] && [ "$SKIP_BUILD" = false ] && [ "$BUILD_OK" = true ]; then
    if command -v clang-tidy &>/dev/null; then
        run_check_bg "Backend Static Analysis" bash -c "cd '$BACKEND_DIR' && cmake -B build-analysis -DCMAKE_BUILD_TYPE=Debug -DSTATIC_ANALYSIS=ON && cmake --build build-analysis -j\$(nproc) 2>&1"
        STATIC_ANALYSIS_BG=true
    else
        RESULTS["Backend Static Analysis"]="SKIP"
        MISSING_DEPS+=("clang-tidy (backend static analysis)")
    fi
fi

# Also start PostgreSQL container early if we'll need it later
PG_CONTAINER_STARTED=false
if [ "$NEED_PG" = true ]; then
    TEST_PG_CONTAINER="chatapp-test-postgres-$$"
    TEST_PG_PORT=5433
    TEST_PG_USER=chatapp_test
    TEST_PG_PASS=testpassword
    TEST_PG_DB=chatapp_test

    # Clean up any stale test containers from previous runs on the same port
    for cid in $(docker ps -aq --filter "publish=$TEST_PG_PORT"); do
        docker rm -f "$cid" &>/dev/null
    done

    printf "\n${BLUE}Starting test PostgreSQL container on port %s...${NC}\n" "$TEST_PG_PORT"
    docker run -d --rm \
        --name "$TEST_PG_CONTAINER" \
        -e POSTGRES_USER="$TEST_PG_USER" \
        -e POSTGRES_PASSWORD="$TEST_PG_PASS" \
        -e POSTGRES_DB="$TEST_PG_DB" \
        -p "$TEST_PG_PORT:5432" \
        postgres:16-alpine \
        -c max_locks_per_transaction=512 \
        -c max_connections=200 >/dev/null

    PG_CONTAINER_STARTED=true
fi

# Wait for all parallel frontend checks
for name in "${FE_PARALLEL_CHECKS[@]}"; do
    wait_check_bg "$name"
done

# Wait for backend build
if [ "$BACKEND_BUILD_BG" = true ]; then
    wait_check_bg "Backend Build"
    if [ "${RESULTS["Backend Build"]}" = "FAIL" ]; then
        BUILD_OK=false
    fi
fi

# Wait for static analysis
if [ "$STATIC_ANALYSIS_BG" = true ]; then
    wait_check_bg "Backend Static Analysis"
fi

# =====================================================================
# Phase 2: Frontend build (depends on no specific phase 1 result, but
# runs after to avoid competing for CPU with backend build)
# =====================================================================

if [ "$RUN_FE_BUILD" = true ]; then
    run_check "Frontend Build" bash -c "cd '$FRONTEND_DIR' && npm run build"
fi

# =====================================================================
# Phase 3: Backend unit tests + wait for PostgreSQL
# =====================================================================

if [ "$BUILD_OK" = true ] && [ "$RUN_BACKEND_UNIT" = true ]; then
    # Clear stale gcov data only for files that were recompiled (checksum mismatch).
    # Compare gcno (compile-time) and gcda (runtime) timestamps — if gcno is newer,
    # the source was recompiled and the gcda is stale.
    while IFS= read -r gcno_file; do
        gcda_file="${gcno_file%.gcno}.gcda"
        if [ -f "$gcda_file" ] && [ "$gcno_file" -nt "$gcda_file" ]; then
            rm -f "$gcda_file"
        fi
    done < <(find "$BUILD_DIR" -name '*.gcno' 2>/dev/null)

    run_check "Backend Unit Tests" bash -c "cd '$BUILD_DIR' && ctest --output-on-failure -L unit --timeout 30 -j\$(nproc)"
fi

# Wait for PostgreSQL to be ready (it was started during phase 1)
PG_READY=false
if [ "$PG_CONTAINER_STARTED" = true ]; then
    printf "\n${BLUE}Waiting for PostgreSQL to be ready...${NC}\n"
    for i in $(seq 1 30); do
        if docker exec "$TEST_PG_CONTAINER" pg_isready -U "$TEST_PG_USER" &>/dev/null; then
            PG_READY=true
            break
        fi
        sleep 1
    done

    if [ "$PG_READY" = false ]; then
        printf "${RED}Error: Test PostgreSQL container failed to start within 30s.${NC}\n"
        FAILED=1
    fi
fi

# =====================================================================
# Phase 4: Integration tests, API tests, E2E tests (all share one PG)
# =====================================================================

if [ "$BUILD_OK" = true ] && [ "$RUN_BACKEND_INTEG" = true ] && [ "$PG_READY" = true ]; then
    run_check "Backend Integration Tests" bash -c "
        export POSTGRES_HOST=localhost
        export POSTGRES_PORT=$TEST_PG_PORT
        export POSTGRES_USER=$TEST_PG_USER
        export POSTGRES_PASSWORD=$TEST_PG_PASS
        export POSTGRES_DB=$TEST_PG_DB
        cd '$BUILD_DIR' && ctest --output-on-failure -L integration --timeout 60
    "
fi

# Report combined code coverage (unit + integration tests)
COVERAGE_RAN=false
if [ "$BUILD_OK" = true ] && command -v gcov &>/dev/null; then
    # Show report if either unit or integration tests ran successfully
    UNIT_PASSED=$( [ "${RESULTS["Backend Unit Tests"]:-SKIP}" = "PASS" ] && echo true || echo false )
    INTEG_PASSED=$( [ "${RESULTS["Backend Integration Tests"]:-SKIP}" = "PASS" ] && echo true || echo false )
    if [ "$UNIT_PASSED" = true ] || [ "$INTEG_PASSED" = true ]; then
        COVERAGE_LABEL="Code Coverage Report"
        if [ "$UNIT_PASSED" = true ] && [ "$INTEG_PASSED" = true ]; then
            COVERAGE_LABEL="Code Coverage Report (unit + integration tests)"
        elif [ "$INTEG_PASSED" = true ]; then
            COVERAGE_LABEL="Code Coverage Report (integration tests)"
        fi

        printf "\n${BLUE}${BOLD}=== %s ===${NC}\n" "$COVERAGE_LABEL"
        GCDA_DIR="$BUILD_DIR/CMakeFiles/chat-lib.dir/src"
        if [ -d "$GCDA_DIR" ]; then
            # Process all gcda files in one gcov invocation for reliable results
            GCOV_OUTPUT=$(cd "$GCDA_DIR" && find . -name '*.gcda' -exec gcov -n {} + 2>/dev/null)

            unset FILE_LINES FILE_COVERED 2>/dev/null
            declare -A FILE_LINES FILE_COVERED
            FILE_ORDER=()
            current_file=""
            while IFS= read -r line; do
                if [[ "$line" =~ ^File\ \'.*/backend/src/.*\.cpp ]]; then
                    current_file=$(echo "$line" | sed "s|.*src/|src/|; s|'$||")
                elif [[ "$line" =~ ^Lines\ executed: ]] && [ -n "$current_file" ]; then
                    pct=$(echo "$line" | sed -n 's/Lines executed:\([0-9.]*\)% of \([0-9]*\)/\1 \2/p')
                    if [ -n "$pct" ]; then
                        file_pct=$(echo "$pct" | cut -d' ' -f1)
                        file_lines=$(echo "$pct" | cut -d' ' -f2)
                        file_covered=$(echo "$file_pct $file_lines" | awk '{printf "%d", ($1/100)*$2}')
                        prev_covered="${FILE_COVERED[$current_file]:-0}"
                        if [ "$file_covered" -gt "$prev_covered" ] || [ -z "${FILE_LINES[$current_file]+x}" ]; then
                            if [ -z "${FILE_LINES[$current_file]+x}" ]; then
                                FILE_ORDER+=("$current_file")
                            fi
                            FILE_LINES[$current_file]=$file_lines
                            FILE_COVERED[$current_file]=$file_covered
                        fi
                    fi
                    current_file=""
                else
                    current_file=""
                fi
            done <<< "$GCOV_OUTPUT"

            TOTAL_LINES=0
            COVERED_LINES=0
            for f in "${FILE_ORDER[@]}"; do
                fl=${FILE_LINES[$f]}
                fc=${FILE_COVERED[$f]}
                TOTAL_LINES=$((TOTAL_LINES + fl))
                COVERED_LINES=$((COVERED_LINES + fc))
                file_pct=$(awk "BEGIN {printf \"%.2f\", ($fc/$fl)*100}")
                printf "  %-45s %s%% (%s lines)\n" "$f" "$file_pct" "$fl"
            done

            if [ "$TOTAL_LINES" -gt 0 ]; then
                COVERAGE_PCT=$(awk "BEGIN {printf \"%.1f\", ($COVERED_LINES/$TOTAL_LINES)*100}")
                printf "\n  ${BOLD}%-45s %s${NC}\n" "Overall coverage:" "$COVERAGE_PCT% ($COVERED_LINES/$TOTAL_LINES lines)"
            fi
        fi
        printf "${BLUE}${BOLD}=============================${NC}\n"
    fi
fi

if [ "$RUN_API_TESTS" = true ]; then
    if [ "$BUILD_OK" = false ] || [ "$PG_READY" = false ]; then
        RESULTS["API Tests"]="SKIP"
    else
        # Ensure Python venv with test dependencies
        API_VENV="$API_TESTS_DIR/.venv"
        if [ ! -d "$API_VENV" ]; then
            printf "\n${BLUE}Creating Python venv for API tests...${NC}\n"
            python3 -m venv "$API_VENV"
        fi
        "$API_VENV/bin/pip" install -q -r "$API_TESTS_DIR/requirements.txt" 2>/dev/null

        API_PYTEST_ARGS="-v --tb=short"
        if [ "$API_WORKERS" -gt 1 ]; then
            API_PYTEST_ARGS="-v --tb=short -n $API_WORKERS"
        fi

        if [ "$API_WORKERS" -gt 1 ]; then
            # Multi-worker mode: each xdist worker starts its own backend via fixture
            printf "\n${BLUE}Running API tests with %s parallel workers...${NC}\n" "$API_WORKERS"
            run_check "API Tests" bash -c "
                export TEST_BACKEND_BINARY='$BUILD_DIR/chat-server'
                export TEST_BUILD_DIR='$BUILD_DIR'
                export POSTGRES_HOST=localhost
                export POSTGRES_PORT=$TEST_PG_PORT
                export POSTGRES_USER=$TEST_PG_USER
                export POSTGRES_PASSWORD=$TEST_PG_PASS
                export POSTGRES_DB=$TEST_PG_DB
                cd '$API_TESTS_DIR' && '$API_VENV/bin/python' -m pytest $API_PYTEST_ARGS
            "
        else
            # Single-worker mode: start one backend server
            TEST_BACKEND_PORT=9099
            API_UPLOAD_DIR=$(mktemp -d)

            printf "\n${BLUE}Starting backend server on port %s for API tests...${NC}\n" "$TEST_BACKEND_PORT"
            BACKEND_PORT="$TEST_BACKEND_PORT" \
            POSTGRES_HOST=localhost \
            POSTGRES_PORT="$TEST_PG_PORT" \
            POSTGRES_USER="$TEST_PG_USER" \
            POSTGRES_PASSWORD="$TEST_PG_PASS" \
            POSTGRES_DB="$TEST_PG_DB" \
            UPLOAD_DIR="$API_UPLOAD_DIR" \
            "$BUILD_DIR/chat-server" &
            API_SERVER_PID=$!

            API_SERVER_READY=false
            for i in $(seq 1 15); do
                if curl -sf "http://127.0.0.1:$TEST_BACKEND_PORT/api/health" >/dev/null 2>&1; then
                    API_SERVER_READY=true
                    break
                fi
                sleep 1
            done

            if [ "$API_SERVER_READY" = false ]; then
                printf "${RED}Error: Backend server failed to start for API tests.${NC}\n"
                kill "$API_SERVER_PID" 2>/dev/null || true
                RESULTS["API Tests"]="FAIL"
                FAILED=1
            else
                run_check "API Tests" bash -c "
                    export TEST_SERVER_URL=http://127.0.0.1:$TEST_BACKEND_PORT
                    export TEST_BACKEND_PORT=$TEST_BACKEND_PORT
                    export POSTGRES_HOST=localhost
                    export POSTGRES_PORT=$TEST_PG_PORT
                    export POSTGRES_USER=$TEST_PG_USER
                    export POSTGRES_PASSWORD=$TEST_PG_PASS
                    export POSTGRES_DB=$TEST_PG_DB
                    cd '$API_TESTS_DIR' && '$API_VENV/bin/python' -m pytest $API_PYTEST_ARGS
                "
                kill "$API_SERVER_PID" 2>/dev/null || true
                wait "$API_SERVER_PID" 2>/dev/null || true
            fi

            rm -rf "$API_UPLOAD_DIR"
        fi
    fi
fi

if [ "$RUN_E2E" = true ]; then
    if [ "$BUILD_OK" = false ] || [ "$PG_READY" = false ]; then
        RESULTS["E2E Tests"]="SKIP"
    else
        # Ensure Node dependencies are installed
        if [ ! -d "$E2E_TESTS_DIR/node_modules" ]; then
            printf "\n${BLUE}Installing E2E test dependencies...${NC}\n"
            (cd "$E2E_TESTS_DIR" && npm install) || true
        fi

        if [ "$E2E_WORKERS" -gt 1 ]; then
            # Multi-worker mode: fixtures handle per-worker backend/Vite
            printf "\n${BLUE}Running E2E tests with %s parallel workers...${NC}\n" "$E2E_WORKERS"
            run_check "E2E Tests" bash -c "
                export TEST_WORKERS=$E2E_WORKERS
                export TEST_BUILD_DIR='$BUILD_DIR'
                export TEST_FRONTEND_DIR='$FRONTEND_DIR'
                export TEST_PG_CONTAINER=$TEST_PG_CONTAINER
                export POSTGRES_USER=$TEST_PG_USER
                export POSTGRES_PASSWORD=$TEST_PG_PASS
                export POSTGRES_PORT=$TEST_PG_PORT
                cd '$E2E_TESTS_DIR' && npx playwright test
            "
        else
            # Single-worker mode: start one backend + Vite server
            E2E_BACKEND_PORT=9098
            E2E_FRONTEND_PORT=5199
            E2E_UPLOAD_DIR=$(mktemp -d)

            printf "\n${BLUE}Starting backend server on port %s for E2E tests...${NC}\n" "$E2E_BACKEND_PORT"
            BACKEND_PORT="$E2E_BACKEND_PORT" \
            POSTGRES_HOST=localhost \
            POSTGRES_PORT="$TEST_PG_PORT" \
            POSTGRES_USER="$TEST_PG_USER" \
            POSTGRES_PASSWORD="$TEST_PG_PASS" \
            POSTGRES_DB="$TEST_PG_DB" \
            UPLOAD_DIR="$E2E_UPLOAD_DIR" \
            "$BUILD_DIR/chat-server" >/dev/null 2>&1 &
            E2E_SERVER_PID=$!

            E2E_SERVER_READY=false
            for i in $(seq 1 15); do
                if curl -sf "http://127.0.0.1:$E2E_BACKEND_PORT/api/health" >/dev/null 2>&1; then
                    E2E_SERVER_READY=true
                    break
                fi
                sleep 1
            done

            if [ "$E2E_SERVER_READY" = false ]; then
                printf "${RED}Error: Backend server failed to start for E2E tests.${NC}\n"
                kill "$E2E_SERVER_PID" 2>/dev/null || true
                RESULTS["E2E Tests"]="FAIL"
                FAILED=1
            else
                printf "${BLUE}Starting Vite dev server on port %s...${NC}\n" "$E2E_FRONTEND_PORT"
                (cd "$FRONTEND_DIR" && VITE_BACKEND_PORT="$E2E_BACKEND_PORT" npx vite --port "$E2E_FRONTEND_PORT" --strictPort) >/dev/null 2>&1 &
                E2E_VITE_PID=$!

                E2E_VITE_READY=false
                for i in $(seq 1 30); do
                    if curl -sf "http://localhost:$E2E_FRONTEND_PORT/" >/dev/null 2>&1; then
                        E2E_VITE_READY=true
                        break
                    fi
                    sleep 1
                done

                if [ "$E2E_VITE_READY" = false ]; then
                    printf "${RED}Error: Vite dev server failed to start for E2E tests.${NC}\n"
                    kill "$E2E_VITE_PID" 2>/dev/null || true
                    kill "$E2E_SERVER_PID" 2>/dev/null || true
                    RESULTS["E2E Tests"]="FAIL"
                    FAILED=1
                else
                    run_check "E2E Tests" bash -c "
                        export TEST_WORKERS=1
                        export TEST_BACKEND_PORT=$E2E_BACKEND_PORT
                        export TEST_FRONTEND_PORT=$E2E_FRONTEND_PORT
                        export TEST_PG_CONTAINER=$TEST_PG_CONTAINER
                        export POSTGRES_USER=$TEST_PG_USER
                        export POSTGRES_DB=$TEST_PG_DB
                        cd '$E2E_TESTS_DIR' && npx playwright test
                    "
                    kill "$E2E_VITE_PID" 2>/dev/null || true
                    wait "$E2E_VITE_PID" 2>/dev/null || true
                fi

                kill "$E2E_SERVER_PID" 2>/dev/null || true
                wait "$E2E_SERVER_PID" 2>/dev/null || true
            fi

            rm -rf "$E2E_UPLOAD_DIR"
        fi
    fi
fi

# =====================================================================
# Load tests (C++)
# =====================================================================

LOAD_TESTS_DIR="$SCRIPT_DIR/tests/load"
LOAD_BUILD_DIR="$LOAD_TESTS_DIR/build"
LOAD_BINARY="$LOAD_BUILD_DIR/loadtest"

if [ "$RUN_LOAD_TESTS" = true ]; then
    if [ "$BUILD_OK" = false ] || [ "$PG_READY" = false ]; then
        RESULTS["Load Tests"]="SKIP"
    else
        # Build C++ load tester if needed
        if [ ! -x "$LOAD_BINARY" ]; then
            printf "\n${BLUE}Building C++ load tester...${NC}\n"
            (cd "$LOAD_TESTS_DIR" && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)")
        fi

        # Start a backend server for load tests
        LOAD_BACKEND_PORT=9097
        LOAD_UPLOAD_DIR=$(mktemp -d)

        printf "\n${BLUE}Starting backend server on port %s for load tests...${NC}\n" "$LOAD_BACKEND_PORT"
        BACKEND_PORT="$LOAD_BACKEND_PORT" \
        POSTGRES_HOST=localhost \
        POSTGRES_PORT="$TEST_PG_PORT" \
        POSTGRES_USER="$TEST_PG_USER" \
        POSTGRES_PASSWORD="$TEST_PG_PASS" \
        POSTGRES_DB="$TEST_PG_DB" \
        UPLOAD_DIR="$LOAD_UPLOAD_DIR" \
        "$BUILD_DIR/chat-server" &
        LOAD_SERVER_PID=$!

        LOAD_SERVER_READY=false
        for i in $(seq 1 15); do
            if curl -sf "http://127.0.0.1:$LOAD_BACKEND_PORT/api/health" >/dev/null 2>&1; then
                LOAD_SERVER_READY=true
                break
            fi
            sleep 1
        done

        if [ "$LOAD_SERVER_READY" = false ]; then
            printf "${RED}Error: Backend server failed to start for load tests.${NC}\n"
            kill "$LOAD_SERVER_PID" 2>/dev/null || true
            RESULTS["Load Tests"]="FAIL"
            FAILED=1
        else
            run_check "Load Tests" bash -c "
                export POSTGRES_HOST=localhost
                export POSTGRES_PORT=$TEST_PG_PORT
                export POSTGRES_USER=$TEST_PG_USER
                export POSTGRES_PASSWORD=$TEST_PG_PASS
                export POSTGRES_DB=$TEST_PG_DB
                '$LOAD_BINARY' \
                    --host http://127.0.0.1:$LOAD_BACKEND_PORT \
                    --profile ci \
                    --csv-dir '$LOAD_TESTS_DIR/reports' \
                    --config-dir '$LOAD_TESTS_DIR/config'
            "
            kill "$LOAD_SERVER_PID" 2>/dev/null || true
            # Give the server up to 5s to exit, then force-kill
            if ! timeout 5 tail --pid="$LOAD_SERVER_PID" -f /dev/null 2>/dev/null; then
                kill -9 "$LOAD_SERVER_PID" 2>/dev/null || true
            fi
            wait "$LOAD_SERVER_PID" 2>/dev/null || true
        fi

        rm -rf "$LOAD_UPLOAD_DIR"
    fi
fi

# =====================================================================
# Load tests — Release build (C++)
# =====================================================================

RELEASE_BUILD_DIR="$BACKEND_DIR/build-release"

if [ "$RUN_LOAD_TESTS_RELEASE" = true ]; then
    if [ "$PG_READY" = false ]; then
        RESULTS["Load Tests (Release)"]="SKIP"
    elif [ ! -d "$BACKEND_DIR/libs/uWebSockets" ] || [ ! -d "$BACKEND_DIR/libs/json" ]; then
        printf "\n${RED}Error: Backend library dependencies not found.${NC}\n"
        RESULTS["Load Tests (Release)"]="FAIL"
        FAILED=1
    else
        # Build Release binary (separate build dir to avoid clobbering debug build)
        printf "\n${BLUE}${BOLD}=== Building backend (Release) for load tests ===${NC}\n"
        RELEASE_BUILD_OK=true
        if ! (cd "$BACKEND_DIR" && cmake -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release -j"$(nproc)"); then
            RESULTS["Load Tests (Release)"]="FAIL"
            FAILED=1
            RELEASE_BUILD_OK=false
        fi

        if [ "$RELEASE_BUILD_OK" = true ]; then
            # Build C++ load tester if needed
            if [ ! -x "$LOAD_BINARY" ]; then
                printf "\n${BLUE}Building C++ load tester...${NC}\n"
                (cd "$LOAD_TESTS_DIR" && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)")
            fi

            LOAD_BACKEND_PORT=9097
            LOAD_UPLOAD_DIR=$(mktemp -d)

            printf "\n${BLUE}Starting backend server (Release) on port %s for load tests...${NC}\n" "$LOAD_BACKEND_PORT"
            BACKEND_PORT="$LOAD_BACKEND_PORT" \
            POSTGRES_HOST=localhost \
            POSTGRES_PORT="$TEST_PG_PORT" \
            POSTGRES_USER="$TEST_PG_USER" \
            POSTGRES_PASSWORD="$TEST_PG_PASS" \
            POSTGRES_DB="$TEST_PG_DB" \
            UPLOAD_DIR="$LOAD_UPLOAD_DIR" \
            "$RELEASE_BUILD_DIR/chat-server" &
            LOAD_SERVER_PID=$!

            LOAD_SERVER_READY=false
            for i in $(seq 1 15); do
                if curl -sf "http://127.0.0.1:$LOAD_BACKEND_PORT/api/health" >/dev/null 2>&1; then
                    LOAD_SERVER_READY=true
                    break
                fi
                sleep 1
            done

            if [ "$LOAD_SERVER_READY" = false ]; then
                printf "${RED}Error: Backend server (Release) failed to start for load tests.${NC}\n"
                kill "$LOAD_SERVER_PID" 2>/dev/null || true
                RESULTS["Load Tests (Release)"]="FAIL"
                FAILED=1
            else
                run_check "Load Tests (Release)" bash -c "
                    export POSTGRES_HOST=localhost
                    export POSTGRES_PORT=$TEST_PG_PORT
                    export POSTGRES_USER=$TEST_PG_USER
                    export POSTGRES_PASSWORD=$TEST_PG_PASS
                    export POSTGRES_DB=$TEST_PG_DB
                    '$LOAD_BINARY' \
                        --host http://127.0.0.1:$LOAD_BACKEND_PORT \
                        --profile stress \
                        --csv-dir '$LOAD_TESTS_DIR/reports' \
                        --config-dir '$LOAD_TESTS_DIR/config'
                "
                kill "$LOAD_SERVER_PID" 2>/dev/null || true
                if ! timeout 5 tail --pid="$LOAD_SERVER_PID" -f /dev/null 2>/dev/null; then
                    kill -9 "$LOAD_SERVER_PID" 2>/dev/null || true
                fi
                wait "$LOAD_SERVER_PID" 2>/dev/null || true
            fi

            rm -rf "$LOAD_UPLOAD_DIR"
        fi
    fi
fi

# =====================================================================
# Phase 5: Docker build
# =====================================================================

if [ "$RUN_DOCKER" = true ]; then
    run_check "Docker Build" bash -c "cd '$SCRIPT_DIR' && docker compose build"
fi

# =====================================================================
# Cleanup
# =====================================================================

if [ "$PG_CONTAINER_STARTED" = true ]; then
    printf "\n${BLUE}Stopping test PostgreSQL container...${NC}\n"
    docker rm -f "$TEST_PG_CONTAINER" &>/dev/null
fi

# --- Summary ---
print_summary

exit "$FAILED"
