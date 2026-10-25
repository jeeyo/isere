#!/bin/sh
# Compile handler.js into QuickJS bytecode for the RP2350 (32-bit).
#
# Usage:
#   ./scripts/compile_bytecode.sh [input.js] [output.bin]
#
# Defaults:
#   input:  js/handler.js
#   output: js/handler.bin
#
# Prerequisites:
#   - 32-bit libc dev package (e.g., apt install gcc-multilib)
#   - QuickJS submodule initialized (git submodule update --init)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

INPUT="${1:-$ROOT_DIR/js/handler.js}"
OUTPUT="${2:-$ROOT_DIR/js/handler.bin}"

QUICKJS_DIR="$ROOT_DIR/c_libs/quickjs"
BUILD_DIR="$ROOT_DIR/build/bytecode_compiler"

mkdir -p "$BUILD_DIR"
mkdir -p "$(dirname "$OUTPUT")"

# Build the compiler for 32-bit to match RP2350's pointer size.
# QuickJS bytecode encodes pointer-sized values, so host and target
# pointer sizes must match.
echo "Building 32-bit bytecode compiler..."
cc -m32 -O2 -o "$BUILD_DIR/compile_bytecode" \
    "$ROOT_DIR/scripts/compile_bytecode.c" \
    "$QUICKJS_DIR/quickjs.c" \
    "$QUICKJS_DIR/libregexp.c" \
    "$QUICKJS_DIR/libunicode.c" \
    "$QUICKJS_DIR/cutils.c" \
    "$QUICKJS_DIR/dtoa.c" \
    -I"$QUICKJS_DIR" \
    -DCONFIG_VERSION=\"2025-09-13\" \
    -D_GNU_SOURCE \
    -lm -lpthread

echo "Compiling $INPUT -> $OUTPUT ..."
"$BUILD_DIR/compile_bytecode" "$INPUT" "$OUTPUT"

echo "Done. Enable bytecode loading with: cargo build --features bytecode"
