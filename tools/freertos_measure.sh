#!/usr/bin/env bash
#
# Measures the FreeRTOS settings PORTING.md (FreeRTOS configuration) classifies as effective,
# on the Linux build: each variant is built, then measured for idle CPU (10 s with
# no traffic), wrk throughput and p99 at 4 connections, and the time a request
# takes when it has to wait for a 1 ms timer.
# Usage: tools/freertos_measure.sh [runtime]   (default quickjs; needs wrk)
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME="${1:-quickjs}"
URL=http://127.0.0.1:8080

measure() {
  local name="$1" flags="$2" dir="$REPO/build-measure-$1"
  mkdir -p "$dir"
  ( cd "$dir" && cmake "$REPO" -DJS_RUNTIME="$RUNTIME" -DCMAKE_C_FLAGS="$flags" \
      -DISERE_APP_DIRS="$REPO/app;$REPO/tests/e2e/app" > /dev/null && make -j8 > /dev/null 2>&1 )
  ( cd "$dir" && ISERE_NDJSON=0 exec ./isere ) > /dev/null 2>&1 &
  local pid=$!
  sleep 1
  local t0 cpu0 cpu1 ticks
  ticks=$(getconf CLK_TCK)
  cpu0=$(awk '{print $14 + $15}' /proc/$pid/stat)
  sleep 10
  cpu1=$(awk '{print $14 + $15}' /proc/$pid/stat)
  local idle
  idle=$(awk -v a="$cpu0" -v b="$cpu1" -v t="$ticks" 'BEGIN { printf "%.1f", (b - a) / t / 10 * 100 }')
  local w
  w=$(wrk -t1 -c4 -d10s --latency "$URL/" | awk '/Requests\/sec/ {rps=$2} / 99%/ {p99=$2} END {print rps, p99}')
  local timer
  timer=$(curl -s -o /dev/null -w '%{time_total}' "$URL/__test/slow?ms=1")
  kill -INT $pid; wait $pid || true
  printf '| %s | %s%% | %s req/s, p99 %s | %.2f ms |\n' "$name" "$idle" "${w% *}" "${w#* }" "$(awk -v t="$timer" 'BEGIN{print t*1000}')"
}

echo "| variant | idle CPU (% of a core) | wrk -c4 hello | a request waiting on a 1 ms timer |"
echo "|---|---:|---|---:|"
measure tick1000 "-DconfigTICK_RATE_HZ=1000"
measure tick100 "-DconfigTICK_RATE_HZ=100"
measure tick10000 "-DconfigTICK_RATE_HZ=10000"
measure nopreempt "-DconfigUSE_PREEMPTION=0"
