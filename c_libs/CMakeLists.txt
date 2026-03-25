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

# Link QuickJS into the Zephyr application.
# Don't use target_link_libraries with zephyr_interface — it would propagate
# Zephyr's CONFIG_VERSION (Kconfig integer) which conflicts with QuickJS's
# CONFIG_VERSION (string). QuickJS doesn't need Zephyr headers anyway.
zephyr_link_libraries(quickjs)
