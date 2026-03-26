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

if(EXISTS ${HANDLER_BIN})
    # handler.bin is already present (pre-built by CI or developer); use it.
    add_custom_target(handler_bytecode ALL)
else()
    # Not present: build using the host's native gcc, not CMAKE_C_COMPILER
    # (which would be arm-zephyr-eabi-gcc when cross-compiling).
    find_program(HOST_CC gcc REQUIRED)

    add_custom_command(
        OUTPUT ${COMPILE_BYTECODE}
        COMMAND ${HOST_CC} -m32 -O2
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
        COMMENT "Building host bytecode compiler (32-bit)"
    )

    add_custom_command(
        OUTPUT ${HANDLER_BIN}
        COMMAND ${COMPILE_BYTECODE} ${HANDLER_JS} ${HANDLER_BIN}
        DEPENDS ${COMPILE_BYTECODE} ${HANDLER_JS}
        COMMENT "Compiling handler.js to QuickJS bytecode"
    )

    add_custom_target(handler_bytecode ALL DEPENDS ${HANDLER_BIN})
endif()


# Zephyr API shims for Rust FFI (k_uptime_get and similar __syscall functions
# expand to static inline in Zephyr — these thin wrappers provide linkable symbols)
target_sources(app PRIVATE app_shim.c)

# Build the Rust application (depends on bytecode being ready)
rust_cargo_application()
