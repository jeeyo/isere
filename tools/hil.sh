#!/usr/bin/env bash
#
# Hardware-in-the-loop validation for isère on a Raspberry Pi Pico 2.
#
#   build firmware -> flash over SWD (Debug Probe) -> wait for USB network
#   -> wait for /__version to report this build -> smoke tests -> benchmark
#
# Usage: tools/hil.sh [options]
#   --runtime NAME        jerryscript (default) or quickjs
#   --skip-build          flash the existing build in build-pico2-<runtime>/
#   --skip-flash          don't build or flash; test whatever is already running
#   --bench-duration SEC  wrk duration per concurrency level (default 30)
#   --bench-conns "N..."  concurrency levels (default "1 2 4")
#   --no-bench            skip the benchmark
#
# Environment overrides:
#   OPENOCD          path to a Raspberry Pi openocd build (needs target/rp2350.cfg)
#   HIL_SERIAL       Debug Probe UART device (default: the only /dev/cu.usbmodem*)
#   PICO_HOST        board address (default 192.168.7.1)
#   PICO_PORT        board HTTP port (default 8080)
#   PICO_MAC         host-side MAC of the board's USB interface (default 02:02:84:6a:96:00,
#                    from tud_network_mac_address); empty skips the interface wait
#   HIL_TEST_CMD     extra end-to-end test command, run with ISERE_URL set
#   HIL_RESULTS_DIR  where results go (default <repo>/hil-results)
#
# Exit codes: 0 passed, 1 tests failed, 2 build/flash/boot failed.
#
# Written for the bash 3.2 that ships with macOS; also runs on Linux.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"

RUNTIME=jerryscript
SKIP_BUILD=0
SKIP_FLASH=0
BENCH=1
BENCH_DURATION=30
BENCH_CONNS="1 2 4"

PICO_HOST="${PICO_HOST:-192.168.7.1}"
PICO_PORT="${PICO_PORT:-8080}"
PICO_MAC="${PICO_MAC-02:02:84:6a:96:00}"

FLASH_TIMEOUT=60
IFACE_TIMEOUT=30
READY_TIMEOUT=30

while [ $# -gt 0 ]; do
  case "$1" in
    --runtime) RUNTIME="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --skip-flash) SKIP_FLASH=1; SKIP_BUILD=1; shift ;;
    --bench-duration) BENCH_DURATION="$2"; shift 2 ;;
    --bench-conns) BENCH_CONNS="$2"; shift 2 ;;
    --no-bench) BENCH=0; shift ;;
    -h|--help) sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

BASE_URL="http://${PICO_HOST}:${PICO_PORT}"
BUILD_DIR="${REPO}/build-pico2-${RUNTIME}"

# a build ID unique to this iteration, so a stale firmware can't pass the ready check
GIT_REV="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
if ! git -C "$REPO" diff --quiet HEAD 2>/dev/null; then
  GIT_REV="${GIT_REV}-dirty"
fi
BUILD_ID="${GIT_REV}-$(date +%Y%m%d-%H%M%S)"

RESULTS="${HIL_RESULTS_DIR:-${REPO}/hil-results}/${BUILD_ID}"
mkdir -p "$RESULTS"

STEPS=""          # "name:status:seconds" entries for the summary
FAILED_STEP=""
FAIL_REASON=""
SERIAL_PID=""

log() { printf '[hil %s] %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "$RESULTS/hil.log"; }

record() { STEPS="${STEPS}${STEPS:+ }$1:$2:$3"; }

json_escape() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | tr -d '\n'; }

write_summary() {
  local status="$1" first=1 entry name rest st secs
  {
    printf '{\n'
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
  if [ -n "$SERIAL_PID" ]; then
    kill "$SERIAL_PID" 2>/dev/null
    wait "$SERIAL_PID" 2>/dev/null
  fi
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

find_openocd() {
  if [ -n "${OPENOCD:-}" ]; then echo "$OPENOCD"; return 0; fi
  # Raspberry Pi's openocd as installed by the Pico VS Code extension
  local c
  for c in "$HOME"/.pico-sdk/openocd/*/openocd; do
    [ -x "$c" ] && { echo "$c"; return 0; }
  done
  command -v openocd 2>/dev/null
}

find_serial() {
  if [ -n "${HIL_SERIAL:-}" ]; then echo "$HIL_SERIAL"; return 0; fi
  local devs n
  devs=$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null)
  n=$(printf '%s\n' "$devs" | grep -c .)
  [ "$n" -eq 1 ] && echo "$devs"
}

# interface whose MAC matches PICO_MAC, from ifconfig on macOS or Linux
find_iface() {
  ifconfig 2>/dev/null | awk -v mac="$(echo "$PICO_MAC" | tr 'A-F' 'a-f')" '
    /^[^ \t]/ { iface = $1; sub(":$", "", iface) }
    tolower($0) ~ ("ether " mac) { print iface; exit }'
}

iface_ipv4() { ifconfig "$1" 2>/dev/null | awk '/inet / { sub("addr:", "", $2); print $2; exit }'; }

# --- steps -------------------------------------------------------------------

do_preflight() {
  local missing=""
  for t in curl awk; do command -v "$t" >/dev/null || missing="$missing $t"; done
  if [ $SKIP_BUILD -eq 0 ]; then
    for t in cmake make arm-none-eabi-gcc xxd; do command -v "$t" >/dev/null || missing="$missing $t"; done
  fi
  if [ $SKIP_FLASH -eq 0 ]; then
    OPENOCD_BIN="$(find_openocd)"
    [ -n "$OPENOCD_BIN" ] || missing="$missing openocd"
  fi
  if [ -n "$missing" ]; then
    log "missing tools:$missing (see tools/README.md)"
    return 1
  fi
  return 0
}

do_build() {
  mkdir -p "$BUILD_DIR"
  # the build dir must sit directly under the repo: CMakeLists.txt reads ../js/handler.js
  ( cd "$BUILD_DIR" \
    && cmake .. -DTARGET_PLATFORM=pico2 -DJS_RUNTIME="$RUNTIME" -DISERE_BUILD_ID="$BUILD_ID" \
    && make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" ) > "$RESULTS/build.log" 2>&1
}

start_serial() {
  local dev
  dev="$(find_serial)"
  if [ -z "$dev" ]; then
    log "no unique Debug Probe UART found; set HIL_SERIAL to capture device logs"
    return 0
  fi
  log "capturing $dev to serial.log"
  # configure and read on the same descriptor; macOS resets settings when the port closes
  ( stty 115200 raw -echo 2>/dev/null; exec cat ) < "$dev" > "$RESULTS/serial.log" 2>/dev/null &
  SERIAL_PID=$!
  return 0
}

do_flash() {
  local elf="$BUILD_DIR/isere.elf"
  [ -f "$elf" ] || { log "no firmware at $elf"; return 1; }
  local scripts_args=""
  # Raspberry Pi's openocd ships its scripts next to the binary
  [ -d "$(dirname "$OPENOCD_BIN")/scripts" ] && scripts_args="-s $(dirname "$OPENOCD_BIN")/scripts"
  # shellcheck disable=SC2086
  run_with_timeout "$FLASH_TIMEOUT" "$OPENOCD_BIN" $scripts_args \
    -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000" \
    -c "program $elf verify reset exit" > "$RESULTS/openocd.log" 2>&1
}

do_wait_iface() {
  local deadline=$(( $(date +%s) + IFACE_TIMEOUT )) iface ip
  while [ "$(date +%s)" -lt "$deadline" ]; do
    iface="$(find_iface)"
    if [ -n "$iface" ]; then
      ip="$(iface_ipv4 "$iface")"
      if [ -n "$ip" ]; then
        log "board network up on $iface ($ip)"
        return 0
      fi
    fi
    sleep 1
  done
  log "no interface with MAC $PICO_MAC and an IPv4 address after ${IFACE_TIMEOUT}s"
  return 1
}

do_wait_ready() {
  local deadline=$(( $(date +%s) + READY_TIMEOUT )) body=""
  while [ "$(date +%s)" -lt "$deadline" ]; do
    body="$(curl -s -m 2 "$BASE_URL/__version" 2>/dev/null)"
    if [ $SKIP_FLASH -eq 1 ] && [ -n "$body" ]; then
      log "running: $body"
      return 0
    fi
    case "$body" in
      *"\"build\":\"$BUILD_ID\""*) log "running: $body"; return 0 ;;
    esac
    sleep 1
  done
  log "/__version never reported build $BUILD_ID (last answer: ${body:-none})"
  return 1
}

# expect <name> <expected status> <path>
expect() {
  local code
  code="$(curl -s -o /dev/null -w '%{http_code}' -m 5 "$BASE_URL$3")"
  if [ "$code" = "$2" ]; then
    log "   pass  $1 ($code)"
    echo "pass $1" >> "$RESULTS/tests.txt"
    return 0
  fi
  log "   FAIL  $1: expected $2, got $code"
  echo "fail $1 expected=$2 got=$code" >> "$RESULTS/tests.txt"
  return 1
}

do_smoke() {
  local rc=0
  expect "version endpoint" 200 /__version || rc=1
  expect "handler at /" 200 / || rc=1
  expect "unknown path" 404 /does-not-exist || rc=1
  # the board must still answer after a burst of sequential requests
  local failures=0
  for _ in $(seq 1 50); do
    curl -s -o /dev/null -m 5 "$BASE_URL/" || failures=$((failures + 1))
  done
  if [ $failures -eq 0 ]; then
    log "   pass  50 sequential requests"
    echo "pass 50 sequential requests" >> "$RESULTS/tests.txt"
  else
    log "   FAIL  50 sequential requests: $failures failed"
    echo "fail 50 sequential requests failures=$failures" >> "$RESULTS/tests.txt"
    rc=1
  fi
  expect "alive after burst" 200 /__version || rc=1
  return $rc
}

do_e2e() {
  ISERE_URL="$BASE_URL" sh -c "$HIL_TEST_CMD" > "$RESULTS/e2e.log" 2>&1
}

do_bench() {
  if ! command -v wrk >/dev/null; then
    log "wrk not installed (brew install wrk); skipping benchmark"
    return 0
  fi
  local c
  for c in $BENCH_CONNS; do
    log "   wrk -t1 -c$c -d${BENCH_DURATION}s"
    wrk -t1 -c"$c" -d"${BENCH_DURATION}s" --latency "$BASE_URL/" > "$RESULTS/bench-c$c.txt" 2>&1 || return 1
    grep -E "Requests/sec|Socket errors|Non-2xx" "$RESULTS/bench-c$c.txt" | sed 's/^/      /' | tee -a "$RESULTS/hil.log"
  done
}

# --- main --------------------------------------------------------------------

log "build $BUILD_ID, runtime $RUNTIME, results in $RESULTS"

step preflight do_preflight || fail 2 preflight "required tools are missing"

if [ $SKIP_BUILD -eq 0 ]; then
  step build do_build || fail 2 build "firmware build failed, see build.log"
fi

if [ $SKIP_FLASH -eq 0 ]; then
  start_serial
  step flash do_flash || fail 2 flash "openocd could not program the board, see openocd.log"
  if [ -n "$PICO_MAC" ]; then
    step wait-interface do_wait_iface || fail 2 wait-interface "board's USB network interface did not come up"
  fi
fi

step wait-ready do_wait_ready || fail 2 wait-ready "board did not serve this build's /__version"

step smoke do_smoke || fail 1 smoke "smoke tests failed, see tests.txt"

if [ -n "${HIL_TEST_CMD:-}" ]; then
  step e2e do_e2e || fail 1 e2e "end-to-end tests failed, see e2e.log"
fi

if [ $BENCH -eq 1 ]; then
  step bench do_bench || fail 1 bench "benchmark failed, see bench-*.txt"
  # the board must survive the benchmark
  step post-bench-alive do_wait_ready || fail 1 post-bench-alive "board stopped answering after the benchmark"
fi

write_summary passed
log "PASSED, results: $RESULTS"
exit 0
