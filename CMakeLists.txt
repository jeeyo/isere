cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(isere LANGUAGES C)

# QuickJS C library (submodule at c_libs/quickjs/)
add_subdirectory(c_libs)

# --- Compile handler.js to QuickJS bytecode at build time ---
#
# 1. Build a host-native QuickJS bytecode compiler (compile_bytecode)
# 2. Run it to convert js/handler.js → js/handler.bin
# 3. Rust embeds the .bin via include_bytes!() in src/platform/loader.rs
#
# The compiler is built for 32-bit (-m32) to match the RP2350's pointer size,
# since QuickJS bytecode encodes pointer-sized values.

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/c_libs/quickjs)
set(HANDLER_JS ${CMAKE_CURRENT_SOURCE_DIR}/js/handler.js)
set(HANDLER_BIN ${CMAKE_CURRENT_SOURCE_DIR}/js/handler.bin)
set(COMPILE_BYTECODE ${CMAKE_CURRENT_BINARY_DIR}/compile_bytecode)

# Build the host bytecode compiler
add_custom_command(
    OUTPUT ${COMPILE_BYTECODE}
    COMMAND ${CMAKE_C_COMPILER} -m32 -O2
        -o ${COMPILE_BYTECODE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_bytecode.c
        ${QUICKJS_DIR}/quickjs.c
        ${QUICKJS_DIR}/libregexp.c
        ${QUICKJS_DIR}/libunicode.c
        ${QUICKJS_DIR}/cutils.c
        ${QUICKJS_DIR}/dtoa.c
        -I${QUICKJS_DIR}
        "-DCONFIG_VERSION=\"2025-09-13\""
        -D_GNU_SOURCE
        -lm -lpthread
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_bytecode.c
        ${QUICKJS_DIR}/quickjs.c
        ${QUICKJS_DIR}/quickjs.h
    COMMENT "Building host bytecode compiler (32-bit)"
)

# Compile handler.js → handler.bin
add_custom_command(
    OUTPUT ${HANDLER_BIN}
    COMMAND ${COMPILE_BYTECODE} ${HANDLER_JS} ${HANDLER_BIN}
    DEPENDS ${COMPILE_BYTECODE} ${HANDLER_JS}
    COMMENT "Compiling handler.js to QuickJS bytecode"
)

# Ensure bytecode is compiled before Rust build
add_custom_target(handler_bytecode ALL DEPENDS ${HANDLER_BIN})

# Build the Rust application (depends on bytecode being ready)
rust_cargo_application()
