# isère's settings, read before the app's project(): the board decides the toolchain,
# which CMake fixes at project(). Included by isere_import.cmake; apps don't include it.
#
#   ISERE_BOARD (or TARGET_PLATFORM)   linux (default), frdm-k64f, pico2
#   JS_RUNTIME                         quickjs (default) or jerryscript
#   ISERE_WITH_OTEL, ISERE_OTEL_HOST, ISERE_OTEL_PORT, DEBUG, ISERE_BUILD_ID, ISERE_BOARD_ENV
#
# Afterwards: project(), then isere_init(), then isere_add_app() (cmake/isere.cmake).

cmake_minimum_required(VERSION 3.19)
include_guard(GLOBAL)

get_filename_component(_isere_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ISERE_DIR "${_isere_dir}" CACHE INTERNAL "isère's source directory")

# these options carry the ISERE_ prefix now. An old name set to something other than its
# old default would be ignored silently, so stop; a default left in an old build
# directory's cache is just dropped.
set(_isere_old_defaults "WITH_OTEL=OFF" "OTEL_HOST=127.0.0.1" "OTEL_PORT=4318")
foreach(_old_default IN LISTS _isere_old_defaults)
  string(REPLACE "=" ";" _pair "${_old_default}")
  list(GET _pair 0 _old)
  list(GET _pair 1 _default)
  if(DEFINED ${_old})
    if(NOT "${${_old}}" STREQUAL "${_default}")
      message(FATAL_ERROR "${_old} was renamed to ISERE_${_old}; set that instead "
        "(in an existing build directory: cmake -U ${_old} -DISERE_${_old}=${${_old}})")
    endif()
    unset(${_old} CACHE)
    unset(${_old})
  endif()
endforeach()

# optimised with debug info unless asked otherwise; this applies to the engines and mbedTLS too
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# the board: ISERE_BOARD, or TARGET_PLATFORM as isère's own scripts pass it
set(ISERE_BOARD "" CACHE STRING "Board to build for (alias of TARGET_PLATFORM)")
if(ISERE_BOARD AND DEFINED TARGET_PLATFORM AND NOT TARGET_PLATFORM STREQUAL ISERE_BOARD)
  message(FATAL_ERROR "ISERE_BOARD=${ISERE_BOARD} and TARGET_PLATFORM=${TARGET_PLATFORM} disagree; set one of them")
endif()
if(ISERE_BOARD)
  set(TARGET_PLATFORM "${ISERE_BOARD}" CACHE STRING "Platform to compile isère for")
endif()
set(TARGET_PLATFORM linux CACHE STRING "Platform to compile isère for")
if(NOT EXISTS "${ISERE_DIR}/portable/${TARGET_PLATFORM}/platform_features.cmake")
  file(GLOB _boards RELATIVE "${ISERE_DIR}/portable" "${ISERE_DIR}/portable/*/platform_features.cmake")
  string(REPLACE "/platform_features.cmake" "" _boards "${_boards}")
  message(FATAL_ERROR "unknown board '${TARGET_PLATFORM}'; isère has: ${_boards}")
endif()
# the toolchain is cached with the build directory, so a board can't change in one
if(DEFINED ISERE_CONFIGURED_BOARD AND NOT ISERE_CONFIGURED_BOARD STREQUAL TARGET_PLATFORM)
  message(FATAL_ERROR "this build directory is for ${ISERE_CONFIGURED_BOARD}; use a new one for ${TARGET_PLATFORM}")
endif()
set(ISERE_CONFIGURED_BOARD "${TARGET_PLATFORM}" CACHE INTERNAL "")

set(JS_RUNTIME quickjs CACHE STRING "JavaScript runtime: quickjs or jerryscript")
if(NOT JS_RUNTIME MATCHES "^(quickjs|jerryscript)$")
  message(FATAL_ERROR "JS_RUNTIME must be quickjs or jerryscript, not '${JS_RUNTIME}'")
endif()

option(DEBUG "Debug mode" OFF)
option(ISERE_WITH_OTEL "OpenTelemetry exporter" OFF)
set(ISERE_OTEL_HOST "127.0.0.1" CACHE STRING "OpenTelemetry Collector OTLP/HTTP host")
set(ISERE_OTEL_PORT 4318 CACHE STRING "OpenTelemetry Collector OTLP/HTTP port")
# empty: computed per app from the app's and isère's git commits (isere_add_app)
set(ISERE_BUILD_ID "" CACHE STRING "Build identifier served at /__version")
option(WITH_LWIP "Build isère's own lwIP copy" OFF)

# the board's toolchain and sizes, before project()
include("${ISERE_DIR}/portable/${TARGET_PLATFORM}/platform_features.cmake")

include("${ISERE_DIR}/cmake/isere.cmake")
