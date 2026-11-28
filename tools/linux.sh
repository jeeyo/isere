#!/usr/bin/env bash
#
# Fast validation loop on the Linux build. Run this until it's green, then tools/hil.sh.
#
#   build -> fuzz the HTTP parser -> node:test suite (incl. valgrind) -> benchmark
#
# Usage: tools/linux.sh [options]
#   --runtime NAME        quickjs (default) or jerryscript
#   --skip-build          test the existing build in build-linux-<runtime>/
#   --no-valgrind         skip the valgrind memory tests
#   --fuzz SEC            seconds of libFuzzer on the HTTP parser (default 30, 0 = skip; needs clang)
#   --bench-duration SEC  wrk duration per concurrency level (default 10)
#   --bench-conns "N..."  concurrency levels (default "1 2 4")
#   --no-bench            skip the benchmark
#
# In a container, from the repository root (this is how CI runs it):
#   docker build -t isere-linux -f tools/linux.Dockerfile tools
#   docker run --rm -v "$PWD":/app -w /app isere-linux tools/linux.sh
#
# Environment overrides:
#   ISERE_PORT       port to listen on (default 8080)
#   HIL_RESULTS_DIR  where results go (default <repo>/hil-results)
#
# Exit codes: 0 passed, 1 tests failed, 2 build/startup failed.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"

RUNTIME=quickjs
SKIP_BUILD=0
VALGRIND=1
BENCH=1
BENCH_DURATION=10
BENCH_CONNS="1 2 4"
FUZZ=30

while [ $# -gt 0 ]; do
  case "$1" in
    --runtime) RUNTIME="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --no-valgrind) VALGRIND=0; shift ;;
    --bench-duration) BENCH_DURATION="$2"; shift 2 ;;
    --bench-conns) BENCH_CONNS="$2"; shift 2 ;;
    --no-bench) BENCH=0; shift ;;
    --fuzz) FUZZ="$2"; shift 2 ;;
    -h|--help) sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

PORT="${ISERE_PORT:-8080}"
BASE_URL="http://127.0.0.1:${PORT}"
RESULTS_ROOT="${HIL_RESULTS_DIR:-${REPO}/hil-results}"
LOG_TAG=linux
BUILD_DIR="${BUILD_DIR:-${REPO}/build-linux-${RUNTIME}}"

# shellcheck source=tools/common.sh
. "$REPO/tools/common.sh"

do_build() {
  mkdir -p "$BUILD_DIR"
  ( cd "$BUILD_DIR" \
    && cmake "$REPO" -DTARGET_PLATFORM=linux -DJS_RUNTIME="$RUNTIME" -DISERE_BUILD_ID="$BUILD_ID" \
       -DISERE_APP_DIRS="$REPO/app;$REPO/tests/e2e/app" \
    && make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" ) > "$RESULTS/build.log" 2>&1
}

# the test suite starts and stops its own servers; the benchmark needs one of its own
start_server() {
  ( cd "$BUILD_DIR" && ISERE_NDJSON=0 exec ./isere ) > "$RESULTS/server.log" 2>&1 &
  SERVER_PID=$!
  CLEANUP_PIDS="$CLEANUP_PIDS $SERVER_PID"
  wait_for_build 10
}

log "build $BUILD_ID, runtime $RUNTIME, results in $RESULTS"

step preflight require_tools cmake make curl node || fail 2 preflight "required tools are missing"

if [ $SKIP_BUILD -eq 1 ]; then
  # reuse the existing binary, so check against whatever build ID it carries
  BUILD_ID="$(sed -n 's/^ISERE_BUILD_ID:STRING=//p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null)"
  [ -n "$BUILD_ID" ] || fail 2 build "no existing build in $BUILD_DIR"
else
  step build do_build || fail 2 build "build failed, see build.log"
fi

run_fuzz() {
  if ! command -v clang >/dev/null; then
    log "clang not installed; skipping the fuzzer"
    return 0
  fi
  FUZZ_OUT="$RESULTS/fuzz" "$REPO/tools/fuzz.sh" "$FUZZ" > "$RESULTS/fuzz.log" 2>&1
  local rc=$?
  grep -E "^Done [0-9]+ runs" "$RESULTS/fuzz.log" | sed 's/^/      /' | tee -a "$RESULTS/run.log"
  return $rc
}

if [ "$FUZZ" -gt 0 ]; then
  step fuzz run_fuzz || fail 1 fuzz "the HTTP parser fuzzer found a problem, see fuzz.log"
fi

TEST_ENV="ISERE_BUILD_DIR=$BUILD_DIR ISERE_PORT=$PORT"
[ $VALGRIND -eq 1 ] || TEST_ENV="$TEST_ENV ISERE_VALGRIND=0"
# shellcheck disable=SC2086
step tests run_tests $TEST_ENV || fail 1 tests "tests failed, see tests.log"

if [ $BENCH -eq 1 ]; then
  step start-server start_server || fail 2 start-server "isère did not start, see server.log"
  step bench run_bench "$BENCH_DURATION" "$BENCH_CONNS" || fail 1 bench "benchmark failed, see bench-*.txt and server.log"
fi

pass
