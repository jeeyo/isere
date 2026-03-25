# Build QuickJS as a Zephyr C library.
#
# QuickJS source files are expected in the quickjs/ subdirectory.
# Either copy them from 3rdparty/quickjs or add as a git submodule.
#
# This builds QuickJS with custom allocator hooks that bridge to
# Zephyr's k_malloc/k_free.

zephyr_library_named(quickjs)

# QuickJS core source files
zephyr_library_sources(
    quickjs/quickjs.c
    quickjs/libregexp.c
    quickjs/libunicode.c
    quickjs/cutils.c
    quickjs_shim.c
)

zephyr_library_include_directories(
    quickjs
)

# QuickJS compile definitions for embedded use
zephyr_library_compile_definitions(
    _GNU_SOURCE
    CONFIG_BIGNUM
    CONFIG_VERSION="0.1.0"
    EMSCRIPTEN  # Disables OS-specific features in QuickJS
)

# Optimize for size on embedded targets
zephyr_library_compile_options(
    -Os
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
)
