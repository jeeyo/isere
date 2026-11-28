#!/usr/bin/env bash
#
# Hardware-in-the-loop validation for isère on a board.
# Run tools/linux.sh first: it's the same test suite with a much faster loop.
#
#   build firmware -> flash over SWD -> wait for the board's network
#   -> wait for /__version to report this build -> node:test suite -> benchmark
#
# Boards:
#   frdm-k64f (default)  NXP FRDM-K64F through its OpenSDA USB port (SWD and UART in one
#                        cable), Ethernet to the LAN; the address comes from its UART log
#   pico2                Raspberry Pi Pico 2 through a Debug Probe, USB networking
#
# Usage: tools/hil.sh [options]
#   --board NAME          frdm-k64f (default) or pico2
#   --runtime NAME        jerryscript (default on frdm-k64f, where QuickJS does not fit)
#                         or quickjs (default on pico2)
#   --skip-build          flash the existing build in build-pico2-<runtime>/
#   --skip-flash          don't build or flash; test whatever is already running
#   --bench-duration SEC  wrk duration per concurrency level (default 30)
#   --bench-conns "N..."  concurrency levels (default "1 2 4")
#   --no-bench            skip the benchmark
#
# Environment overrides:
#   OPENOCD          openocd to use (pico2: a Raspberry Pi build with target/rp2350.cfg)
#   HIL_SERIAL       the board's UART device (default: the only /dev/cu.usbmodem*)
#   BOARD_HOST       board address (frdm-k64f: read from the UART, pico2: 192.168.7.1)
#   BOARD_PORT       board HTTP port (default 8080)
#   PICO_MAC         host-side MAC of the board's USB interface (default 02:02:84:6a:96:00,
#                    from tud_network_mac_address); empty skips the interface wait
#   HIL_RESULTS_DIR  where results go (default <repo>/hil-results)
#
# Exit codes: 0 passed, 1 tests failed, 2 build/flash/boot failed.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"

BOARD=frdm-k64f
RUNTIME=
SKIP_BUILD=0
SKIP_FLASH=0
BENCH=1
BENCH_DURATION=30
BENCH_CONNS="1 2 4"

BOARD_HOST="${BOARD_HOST:-${PICO_HOST:-}}"
BOARD_PORT="${BOARD_PORT:-${PICO_PORT:-8080}}"
PICO_MAC="${PICO_MAC-02:02:84:6a:96:00}"

FLASH_TIMEOUT=60
IFACE_TIMEOUT=30
ADDRESS_TIMEOUT=40
READY_TIMEOUT=30

while [ $# -gt 0 ]; do
  case "$1" in
    --board) BOARD="$2"; shift 2 ;;
    --runtime) RUNTIME="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --skip-flash) SKIP_FLASH=1; SKIP_BUILD=1; shift ;;
    --bench-duration) BENCH_DURATION="$2"; shift 2 ;;
    --bench-conns) BENCH_CONNS="$2"; shift 2 ;;
    --no-bench) BENCH=0; shift ;;
    -h|--help) sed -n '3,35p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$BOARD" in
  frdm-k64f) RUNTIME="${RUNTIME:-jerryscript}" ;;
  pico2) RUNTIME="${RUNTIME:-quickjs}"; BOARD_HOST="${BOARD_HOST:-192.168.7.1}" ;;
  *) echo "unknown board: $BOARD (frdm-k64f or pico2)" >&2; exit 2 ;;
esac

BASE_URL="http://${BOARD_HOST:-unknown}:${BOARD_PORT}"
RESULTS_ROOT="${HIL_RESULTS_DIR:-${REPO}/hil-results}"
LOG_TAG=hil
BUILD_DIR="${BUILD_DIR:-${REPO}/build-${BOARD}-${RUNTIME}}"

# shellcheck source=tools/common.sh
. "$REPO/tools/common.sh"

find_openocd() {
  if [ -n "${OPENOCD:-}" ]; then echo "$OPENOCD"; return 0; fi
  # Raspberry Pi's openocd as installed by the Pico VS Code extension
  local c
  # the FRDM-K64F needs only stock openocd (target/kx.cfg)
  [ "$BOARD" = pico2 ] || { command -v openocd 2>/dev/null; return 0; }
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
  [ $SKIP_BUILD -eq 1 ] || tools="$tools cmake make"
  # the K64F build also finds the toolchain in the repository's .toolchain/ folder
  if [ $SKIP_BUILD -eq 0 ] && ! ls -d "$REPO"/.toolchain/arm-gnu-toolchain-*/bin >/dev/null 2>&1; then
    tools="$tools arm-none-eabi-gcc"
  fi
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
    && cmake "$REPO" -DTARGET_PLATFORM="$BOARD" -DJS_RUNTIME="$RUNTIME" -DISERE_BUILD_ID="$BUILD_ID" \
       -DISERE_APP_DIRS="$REPO/examples/jwks/app;$REPO/tests/e2e/app" \
       -DISERE_BOARD_ENV="ISERE_HANDLER_TIMEOUT_MS=1000" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    && make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" ) > "$RESULTS/build.log" 2>&1
}

start_serial() {
  local dev
  dev="$(find_serial)"
  if [ -z "$dev" ]; then
    log "no unique UART found; set HIL_SERIAL to capture device logs"
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
  if [ "$BOARD" = frdm-k64f ]; then
    # a blank or crashed K64F may be reset-looping: halt it through the MDM-AP first
    run_with_timeout "$FLASH_TIMEOUT" "$OPENOCD_BIN" \
      -f interface/cmsis-dap.cfg -c "transport select swd" -f target/kx.cfg -c "adapter speed 4000" \
      -c init -c "kinetis mdm halt" -c "program $elf verify" -c "reset run" -c shutdown \
      > "$RESULTS/openocd.log" 2>&1 && grep -q "Verified OK" "$RESULTS/openocd.log"
    return
  fi
  # shellcheck disable=SC2086
  run_with_timeout "$FLASH_TIMEOUT" "$OPENOCD_BIN" $scripts_args \
    -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000" \
    -c "program $elf verify reset exit" > "$RESULTS/openocd.log" 2>&1
}

# the FRDM-K64F prints "isere: ip A.B.C.D" on its UART once DHCP has answered
do_wait_address() {
  local deadline=$(( $(date +%s) + ADDRESS_TIMEOUT )) ip
  while [ "$(date +%s)" -lt "$deadline" ]; do
    ip="$(tr -d '\r' < "$RESULTS/serial.log" 2>/dev/null | sed -n 's/^isere: ip \([0-9.]*\)$/\1/p' | tail -1)"
    if [ -n "$ip" ]; then
      BOARD_HOST="$ip"
      BASE_URL="http://${BOARD_HOST}:${BOARD_PORT}"
      log "board address $BOARD_HOST (DHCP)"
      return 0
    fi
    sleep 1
  done
  log "no \"isere: ip\" line on the UART after ${ADDRESS_TIMEOUT}s"
  return 1
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

log "board $BOARD, build $BUILD_ID, runtime $RUNTIME, results in $RESULTS"

step preflight do_preflight || fail 2 preflight "required tools are missing"

if [ $SKIP_FLASH -eq 1 ]; then
  [ -n "$BOARD_HOST" ] || fail 2 wait-ready "--skip-flash needs BOARD_HOST for $BOARD"
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
  if [ "$BOARD" = frdm-k64f ]; then
    if [ -z "$BOARD_HOST" ]; then
      step wait-address do_wait_address || fail 2 wait-address "board did not report a DHCP address, see serial.log"
    fi
  elif [ -n "$PICO_MAC" ]; then
    step wait-interface do_wait_iface || fail 2 wait-interface "board's USB network interface did not come up"
  fi
fi

step wait-ready wait_for_build "$READY_TIMEOUT" || fail 2 wait-ready "board did not serve this build's /__version"

step tests run_tests "ISERE_URL=$BASE_URL" || fail 1 tests "tests failed, see tests.log"

if [ $BENCH -eq 1 ]; then
  step bench run_bench "$BENCH_DURATION" "$BENCH_CONNS" || fail 1 bench "benchmark failed, see bench-*.txt and serial.log"
fi

pass
