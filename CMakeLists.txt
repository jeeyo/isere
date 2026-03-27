cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(isere LANGUAGES C)

# QuickJS C library (submodule at c_libs/quickjs/)
add_subdirectory(c_libs)

# --- Compile handler.js to QuickJS bytecode at build time ---
#
# QuickJS bytecode is NOT portable across pointer sizes. The bytecode
# compiler must be built to match the target:
#   - 32-bit targets (RP2350): compile with -m32
#   - 64-bit targets (native_sim/native/64): compile native (no -m32)
#
# In CI, a pre-compiled handler.bin can be provided as an artifact,
# but it MUST match the target's pointer size.

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/c_libs/quickjs)
set(HANDLER_JS ${CMAKE_CURRENT_SOURCE_DIR}/js/handler.js)
set(HANDLER_BIN ${CMAKE_CURRENT_SOURCE_DIR}/js/handler.bin)
set(COMPILE_BYTECODE ${CMAKE_CURRENT_BINARY_DIR}/compile_bytecode)

# Detect target pointer size to choose correct compiler flags
if(CONFIG_64BIT)
    set(BYTECODE_M_FLAG "")
    set(BYTECODE_COMMENT "Building host bytecode compiler (64-bit)")
else()
    set(BYTECODE_M_FLAG "-m32")
    set(BYTECODE_COMMENT "Building host bytecode compiler (32-bit)")
endif()

if(EXISTS ${HANDLER_BIN})
    # handler.bin is already present (pre-built by CI or developer); use it.
    add_custom_target(handler_bytecode ALL)
else()
    # Not present: build using the host's native gcc, not CMAKE_C_COMPILER
    # (which would be arm-zephyr-eabi-gcc when cross-compiling).
    find_program(HOST_CC gcc REQUIRED)

    add_custom_command(
        OUTPUT ${COMPILE_BYTECODE}
        COMMAND ${HOST_CC} ${BYTECODE_M_FLAG} -O2
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
        COMMENT "${BYTECODE_COMMENT}"
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
