#!/usr/bin/env bash
#
# Integration test for isere native_sim build.
#
# Sets up a TAP interface, runs the native_sim binary, sends HTTP requests,
# and verifies the responses match expected values.
#
# Usage: sudo ./scripts/integration_test.sh <path-to-zephyr-exe>
#
set -euo pipefail

BINARY="${1:?Usage: $0 <path-to-zephyr.exe>}"
TAP_IF="zeth"
SERVER_IP="192.0.2.1"
HOST_IP="192.0.2.2"
SERVER_PORT="8080"
SERVER_URL="http://${SERVER_IP}:${SERVER_PORT}"
LOG_FILE="$(mktemp)"
PASS=0
FAIL=0

cleanup() {
    # Kill the server if running
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    # Remove TAP interface
    ip link show "$TAP_IF" &>/dev/null && ip link delete "$TAP_IF" 2>/dev/null || true
    echo ""
    echo "=== Server logs ==="
    cat "$LOG_FILE"
    rm -f "$LOG_FILE"
}
trap cleanup EXIT

assert_contains() {
    local label="$1" haystack="$2" needle="$3"
    if echo "$haystack" | grep -qF "$needle"; then
        echo "  PASS: $label"
        ((PASS++))
    else
        echo "  FAIL: $label (expected '$needle')"
        echo "    got: $haystack"
        ((FAIL++))
    fi
}

assert_log_contains() {
    local label="$1" needle="$2"
    if grep -qF "$needle" "$LOG_FILE"; then
        echo "  PASS: $label"
        ((PASS++))
    else
        echo "  FAIL: $label (expected '$needle' in logs)"
        ((FAIL++))
    fi
}

# --- Setup ---

echo "=== Setting up TAP interface ($TAP_IF) ==="
ip tuntap add "$TAP_IF" mode tap
ip link set "$TAP_IF" up
ip addr add "${HOST_IP}/24" dev "$TAP_IF"

echo "=== Starting native_sim binary ==="
chmod +x "$BINARY"
"$BINARY" --eth-if="$TAP_IF" > "$LOG_FILE" 2>&1 &
SERVER_PID=$!

echo "=== Waiting for server to be ready ==="
READY=false
for i in $(seq 1 30); do
    if curl -sf --connect-timeout 1 "$SERVER_URL/" >/dev/null 2>&1; then
        READY=true
        break
    fi
    sleep 1
done

if ! $READY; then
    echo "FATAL: Server did not become ready within 30 seconds"
    echo "=== Server logs ==="
    cat "$LOG_FILE"
    exit 1
fi
echo "Server is ready (PID $SERVER_PID)"

# --- Tests ---

echo ""
echo "=== Test 1: GET / — basic response ==="
RESPONSE=$(curl -s -w "\n%{http_code}" "$SERVER_URL/")
HTTP_CODE=$(echo "$RESPONSE" | tail -1)
BODY=$(echo "$RESPONSE" | sed '$d')
assert_contains "status code is 200" "$HTTP_CODE" "200"
assert_contains "body contains handler output" "$BODY" '{"k":"v"}'

echo ""
echo "=== Test 2: GET / — response headers ==="
HEADERS=$(curl -sI "$SERVER_URL/")
assert_contains "Server header" "$HEADERS" "Server: isere"
assert_contains "Connection: close header" "$HEADERS" "Connection: close"
assert_contains "Content-Type header from handler" "$HEADERS" "Content-Type: text/plain"
assert_contains "Content-Length header present" "$HEADERS" "Content-Length:"

echo ""
echo "=== Test 3: GET /path?key=val — query and path ==="
curl -sf "$SERVER_URL/path?key=val" >/dev/null 2>&1
assert_log_contains "path logged in event" "/path"
assert_log_contains "query logged in event" "key=val"

echo ""
echo "=== Test 4: POST with body ==="
curl -sf -X POST -H "Content-Type: application/json" -d '{"hello":"world"}' "$SERVER_URL/post" >/dev/null 2>&1
assert_log_contains "POST body logged in event" "hello"

echo ""
echo "=== Test 5: Handler logs ==="
assert_log_contains "console.log from handler" "Test ESM"
assert_log_contains "module-level console.log" "ESM Outside"
assert_log_contains "async/await resolved" "a 555"

# --- Summary ---

echo ""
echo "==========================="
echo "Results: $PASS passed, $FAIL failed"
echo "==========================="

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
