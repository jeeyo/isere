cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(isere LANGUAGES C)

# QuickJS C library (submodule at c_libs/quickjs/)
add_subdirectory(c_libs)

# --- Compile handler.js to QuickJS bytecode at build time ---
#
# handler.bin must be pre-compiled for 32-bit (to match RP2350's pointer size)
# using scripts/compile_bytecode.sh before the firmware build.
# In CI, the bytecode job does this and passes handler.bin as an artifact.
#
# For local native_sim builds where handler.bin is absent, we fall back to
# building it here using the host's gcc (never CMAKE_C_COMPILER, which is
# the cross-compiler when targeting ARM).

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/c_libs/quickjs)
set(HANDLER_JS ${CMAKE_CURRENT_SOURCE_DIR}/js/handler.js)
set(HANDLER_BIN ${CMAKE_CURRENT_SOURCE_DIR}/js/handler.bin)
set(COMPILE_BYTECODE ${CMAKE_CURRENT_BINARY_DIR}/compile_bytecode)

# native_sim runs on the host (64-bit on modern systems) — bytecode must match.
# RP2350 (Cortex-M33) is 32-bit — requires -m32 on the host compiler.
# macOS arm64 does not support -m32 at all, so native_sim is the only usable
# build target on Apple Silicon without a Linux cross-build environment.
if(BOARD MATCHES "native_sim")
    set(BYTECODE_ARCH_FLAGS "")
else()
    set(BYTECODE_ARCH_FLAGS "-m32")
endif()

find_program(HOST_CC gcc HINTS /usr/bin /usr/local/bin)
if(NOT HOST_CC)
    find_program(HOST_CC cc REQUIRED)
endif()

add_custom_command(
    OUTPUT ${COMPILE_BYTECODE}
    COMMAND ${HOST_CC} ${BYTECODE_ARCH_FLAGS} -O2
        -o ${COMPILE_BYTECODE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_bytecode.c
        ${QUICKJS_DIR}/quickjs.c
        ${QUICKJS_DIR}/libregexp.c
        ${QUICKJS_DIR}/libunicode.c
        ${QUICKJS_DIR}/cutils.c
        ${QUICKJS_DIR}/dtoa.c
        -I${QUICKJS_DIR}
        -DCONFIG_VERSION='"2025-09-13"'
        -D_GNU_SOURCE
        -lm -lpthread
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_bytecode.c
        ${QUICKJS_DIR}/quickjs.c
        ${QUICKJS_DIR}/quickjs.h
    COMMENT "Building host bytecode compiler (${BOARD})"
)

add_custom_command(
    OUTPUT ${HANDLER_BIN}
    COMMAND ${COMPILE_BYTECODE} ${HANDLER_JS} ${HANDLER_BIN}
    DEPENDS ${COMPILE_BYTECODE} ${HANDLER_JS}
    COMMENT "Compiling handler.js to QuickJS bytecode"
)

add_custom_target(handler_bytecode ALL DEPENDS ${HANDLER_BIN})


# Zephyr API shims for Rust FFI (k_uptime_get and similar __syscall functions
# expand to static inline in Zephyr — these thin wrappers provide linkable symbols)
target_sources(app PRIVATE app_shim.c)

# Build the Rust application (depends on bytecode being ready)
rust_cargo_application()
