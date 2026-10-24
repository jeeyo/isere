# Shared by tools/linux.sh and tools/hil.sh. Source it after setting:
#   REPO, RUNTIME, BASE_URL, RESULTS_ROOT, LOG_TAG
# Written for the bash 3.2 that ships with macOS.

# a build ID unique to this iteration, so a stale binary or firmware can't pass the ready check
GIT_REV="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
if ! git -C "$REPO" diff --quiet HEAD 2>/dev/null; then
  GIT_REV="${GIT_REV}-dirty"
fi
BUILD_ID="${GIT_REV}-$(date +%Y%m%d-%H%M%S)"

RESULTS="${RESULTS_ROOT}/${BUILD_ID}"
mkdir -p "$RESULTS"

STEPS=""          # "name:status:seconds" entries for the summary
FAILED_STEP=""
FAIL_REASON=""
CLEANUP_PIDS=""   # background processes to stop on exit

log() { printf '[%s %s] %s\n' "$LOG_TAG" "$(date +%H:%M:%S)" "$*" | tee -a "$RESULTS/run.log"; }

record() { STEPS="${STEPS}${STEPS:+ }$1:$2:$3"; }

json_escape() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | tr -d '\n'; }

write_summary() {
  local status="$1" first=1 entry name rest st secs
  {
    printf '{\n'
    printf '  "target": "%s",\n' "$LOG_TAG"
    printf '  "build_id": "%s",\n' "$BUILD_ID"
    printf '  "runtime": "%s",\n' "$RUNTIME"
    printf '  "url": "%s",\n' "$BASE_URL"
    printf '  "status": "%s",\n' "$status"
    printf '  "failed_step": "%s",\n' "$FAILED_STEP"
    printf '  "reason": "%s",\n' "$(json_escape "$FAIL_REASON")"
    printf '  "steps": ['
    for entry in $STEPS; do
      name="${entry%%:*}"; rest="${entry#*:}"; st="${rest%%:*}"; secs="${rest#*:}"
      [ $first -eq 1 ] || printf ','
      printf '\n    {"name": "%s", "status": "%s", "seconds": %s}' "$name" "$st" "$secs"
      first=0
    done
    printf '\n  ]\n}\n'
  } > "$RESULTS/summary.json"
}

cleanup() {
  local pid
  for pid in $CLEANUP_PIDS; do
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
  done
}
trap cleanup EXIT

# fail <exit code> <step> <reason>
fail() {
  FAILED_STEP="$2"; FAIL_REASON="$3"
  log "FAILED at $2: $3"
  log "results: $RESULTS"
  if [ "$1" -eq 1 ]; then write_summary failed; else write_summary error; fi
  exit "$1"
}

pass() {
  write_summary passed
  log "PASSED, results: $RESULTS"
  exit 0
}

# run_with_timeout <seconds> <command...>  (macOS has no coreutils timeout)
run_with_timeout() {
  local secs="$1"; shift
  "$@" &
  local pid=$!
  ( sleep "$secs"; kill -TERM "$pid" 2>/dev/null ) &
  local watcher=$!
  wait "$pid"
  local rc=$?
  kill "$watcher" 2>/dev/null
  wait "$watcher" 2>/dev/null
  return $rc
}

# step <name> <command...>: times a step and records its outcome
step() {
  local name="$1"; shift
  local start
  start=$(date +%s)
  log "-> $name"
  "$@"
  local rc=$?
  local secs=$(( $(date +%s) - start ))
  if [ $rc -eq 0 ]; then record "$name" ok "$secs"; else record "$name" failed "$secs"; fi
  return $rc
}

# require_tools <tool...>
require_tools() {
  local missing="" t
  for t in "$@"; do command -v "$t" >/dev/null || missing="$missing $t"; done
  if [ -n "$missing" ]; then
    log "missing tools:$missing (see tools/README.md)"
    return 1
  fi
}

# wait_for_build <timeout seconds>: /__version must report this iteration's BUILD_ID
wait_for_build() {
  local deadline=$(( $(date +%s) + $1 )) body=""
  while [ "$(date +%s)" -lt "$deadline" ]; do
    body="$(curl -s -m 2 "$BASE_URL/__version" 2>/dev/null)"
    case "$body" in
      *"\"build\":\"$BUILD_ID\""*) log "running: $body"; return 0 ;;
    esac
    sleep 1
  done
  log "/__version never reported build $BUILD_ID (last answer: ${body:-none})"
  return 1
}

# run_tests [extra env assignments...]: the node:test suite in tests/e2e
run_tests() {
  ( cd "$REPO" && env "$@" node --test --test-concurrency=1 \
      --test-reporter=spec --test-reporter-destination=stdout \
      --test-reporter=junit --test-reporter-destination="$RESULTS/tests.xml" \
      'tests/e2e/*.test.mjs' ) > "$RESULTS/tests.log" 2>&1
  local rc=$?
  grep -E '^ℹ (tests|pass|fail|skipped|todo) ' "$RESULTS/tests.log" | sed 's/^/      /' | tee -a "$RESULTS/run.log"
  [ $rc -eq 0 ] || grep -E '^ *✖' "$RESULTS/tests.log" | head -20 | sed 's/^/      /' | tee -a "$RESULTS/run.log"
  return $rc
}

# run_bench <duration seconds> <connection levels>: wrk against /, then the server must still answer
run_bench() {
  if ! command -v wrk >/dev/null; then
    log "wrk not installed (brew install wrk); skipping benchmark"
    return 0
  fi
  local c
  for c in $2; do
    log "   wrk -t1 -c$c -d${1}s"
    wrk -t1 -c"$c" -d"${1}s" --latency "$BASE_URL/" > "$RESULTS/bench-c$c.txt" 2>&1 || return 1
    grep -E "Requests/sec|Socket errors|Non-2xx" "$RESULTS/bench-c$c.txt" | sed 's/^/      /' | tee -a "$RESULTS/run.log"
  done
  if ! curl -s -o /dev/null -m 5 "$BASE_URL/__version"; then
    log "server stopped answering after the benchmark"
    return 1
  fi
}
