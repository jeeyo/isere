# Build QuickJS as a static library linked into the Zephyr application.
#
# QuickJS source is in the quickjs/ subdirectory (git submodule from
# https://github.com/bellard/quickjs).
#
# Uses standard CMake (not zephyr_library) since this is built in
# Zephyr application mode, not kernel mode.

# Apply minimal compatibility patch for Zephyr/picolibc targets:
#   - routes getTimezoneOffset through the portable _WIN32 path so it
#     avoids tm_gmtoff (a GNU/BSD extension absent in picolibc)
# -N makes patch skip silently if already applied.
execute_process(
    COMMAND patch -N -p1 -i ${CMAKE_CURRENT_SOURCE_DIR}/quickjs_zephyr.patch
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/quickjs
    OUTPUT_QUIET
    ERROR_QUIET
)

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

# Inherit only the -m* machine flags from Zephyr (e.g. -mcpu=cortex-m33,
# -mfpu=fpv5-sp-d16, -mfloat-abi=hard, -mthumb) so QuickJS is compiled
# for the correct CPU/ABI. We deliberately exclude all other options
# (e.g. -nostdinc, -isystem) to keep QuickJS using its own standard
# toolchain headers — pulling in Zephyr's include paths would shadow
# <alloca.h> and other standard headers QuickJS depends on.
get_target_property(_zephyr_compile_opts zephyr_interface INTERFACE_COMPILE_OPTIONS)
if(_zephyr_compile_opts)
    foreach(_opt ${_zephyr_compile_opts})
        if(_opt MATCHES "^-m")
            target_compile_options(quickjs PRIVATE ${_opt})
        endif()
    endforeach()
endif()

# Optimize for size on embedded targets
target_compile_options(quickjs PRIVATE
    -Os
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    # On ARM, int32_t is 'long' not 'int'; QuickJS uses them interchangeably
    # since they're the same width on 32-bit targets.
    -Wno-incompatible-pointer-types
    -Wno-double-promotion
)

# Link QuickJS into the Zephyr application.
# Don't use target_link_libraries with zephyr_interface — it would propagate
# Zephyr's CONFIG_VERSION (Kconfig integer) which conflicts with QuickJS's
# CONFIG_VERSION (string). QuickJS doesn't need Zephyr headers anyway.
zephyr_link_libraries(quickjs)
