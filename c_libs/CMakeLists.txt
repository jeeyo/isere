# Build QuickJS as a static library linked into the Zephyr application.
#
# QuickJS source is in the quickjs/ subdirectory (git submodule from
# https://github.com/bellard/quickjs).
#
# Uses standard CMake (not zephyr_library) since this is built in
# Zephyr application mode, not kernel mode.

add_library(quickjs STATIC
    quickjs/quickjs.c
    quickjs/libregexp.c
    quickjs/libunicode.c
    quickjs/cutils.c
    quickjs/dtoa.c
    quickjs_shim.c
)

target_include_directories(quickjs PUBLIC
    quickjs
)

# QuickJS compile definitions for embedded use
# Undefine Zephyr's CONFIG_VERSION (integer from Kconfig) before defining
# QuickJS's CONFIG_VERSION (string) to avoid macro collision.
target_compile_options(quickjs PRIVATE -UCONFIG_VERSION)
target_compile_definitions(quickjs PRIVATE
    _GNU_SOURCE
    CONFIG_VERSION="2025-09-13"
    EMSCRIPTEN  # Disables OS-specific features in QuickJS
)

# Optimize for size on embedded targets
target_compile_options(quickjs PRIVATE
    -Os
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
)

# Link QuickJS into the Zephyr application and give it access to Zephyr headers
target_link_libraries(quickjs PUBLIC zephyr_interface)
zephyr_link_libraries(quickjs)
