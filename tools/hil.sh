#!/usr/bin/env bash
#
# Hardware-in-the-loop validation for isère on a Raspberry Pi Pico 2.
# Run tools/linux.sh first: it's the same test suite with a much faster loop.
#
#   build firmware -> flash over SWD (Debug Probe) -> wait for USB network
#   -> wait for /__version to report this build -> node:test suite -> benchmark
#
# Usage: tools/hil.sh [options]
#   --runtime NAME        quickjs (default) or jerryscript
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
#   HIL_RESULTS_DIR  where results go (default <repo>/hil-results)
#
# Exit codes: 0 passed, 1 tests failed, 2 build/flash/boot failed.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"

RUNTIME=quickjs
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
    -h|--help) sed -n '3,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

BASE_URL="http://${PICO_HOST}:${PICO_PORT}"
RESULTS_ROOT="${HIL_RESULTS_DIR:-${REPO}/hil-results}"
LOG_TAG=hil
BUILD_DIR="${BUILD_DIR:-${REPO}/build-pico2-${RUNTIME}}"

# shellcheck source=tools/common.sh
. "$REPO/tools/common.sh"

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

do_preflight() {
  local tools="curl awk node"
  [ $SKIP_BUILD -eq 1 ] || tools="$tools cmake make arm-none-eabi-gcc"
  # shellcheck disable=SC2086
  require_tools $tools || return 1
  if [ $SKIP_FLASH -eq 0 ]; then
    OPENOCD_BIN="$(find_openocd)"
    [ -n "$OPENOCD_BIN" ] || { log "missing tools: openocd (see tools/README.md)"; return 1; }
  fi
}

do_build() {
  mkdir -p "$BUILD_DIR"
  ( cd "$BUILD_DIR" \
    && cmake "$REPO" -DTARGET_PLATFORM=pico2 -DJS_RUNTIME="$RUNTIME" -DISERE_BUILD_ID="$BUILD_ID" \
       -DISERE_APP_DIRS="$REPO/app;$REPO/tests/e2e/app" \
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
  CLEANUP_PIDS="$CLEANUP_PIDS $!"
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

log "build $BUILD_ID, runtime $RUNTIME, results in $RESULTS"

step preflight do_preflight || fail 2 preflight "required tools are missing"

if [ $SKIP_FLASH -eq 1 ]; then
  # test whatever is running, so take its build ID as the expected one
  BUILD_ID="$(curl -s -m 5 "$BASE_URL/__version" | sed -n 's/.*"build":"\([^"]*\)".*/\1/p')"
  [ -n "$BUILD_ID" ] || fail 2 wait-ready "nothing answered $BASE_URL/__version"
else
  if [ $SKIP_BUILD -eq 0 ]; then
    step build do_build || fail 2 build "firmware build failed, see build.log"
  else
    BUILD_ID="$(sed -n 's/^ISERE_BUILD_ID:STRING=//p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null)"
    [ -n "$BUILD_ID" ] || fail 2 build "no existing build in $BUILD_DIR"
  fi
  start_serial
  step flash do_flash || fail 2 flash "openocd could not program the board, see openocd.log"
  if [ -n "$PICO_MAC" ]; then
    step wait-interface do_wait_iface || fail 2 wait-interface "board's USB network interface did not come up"
  fi
fi

step wait-ready wait_for_build "$READY_TIMEOUT" || fail 2 wait-ready "board did not serve this build's /__version"

step tests run_tests "ISERE_URL=$BASE_URL" || fail 1 tests "tests failed, see tests.log"

if [ $BENCH -eq 1 ]; then
  step bench run_bench "$BENCH_DURATION" "$BENCH_CONNS" || fail 1 bench "benchmark failed, see bench-*.txt and serial.log"
fi

pass
