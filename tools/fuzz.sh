#!/usr/bin/env bash
#
# Fuzzes the in-place HTTP parser with libFuzzer and AddressSanitizer.
# Usage: tools/fuzz.sh [seconds]   (default 60; needs clang)
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SECONDS_TO_RUN="${1:-60}"
OUT="${FUZZ_OUT:-$REPO/build-fuzz}"
mkdir -p "$OUT/corpus"
clang -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
  -I"$REPO/include" "$REPO/src/http_parser.c" "$REPO/tests/fuzz/fuzz_http_parser.c" \
  -o "$OUT/fuzz_http_parser"
"$OUT/fuzz_http_parser" -max_total_time="$SECONDS_TO_RUN" -max_len=4096 \
  -artifact_prefix="$OUT/" "$OUT/corpus" "$REPO/tests/fuzz/corpus"
