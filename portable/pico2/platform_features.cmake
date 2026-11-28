set(PICO_BOARD pico2)

# pico-sdk: PICO_SDK_PATH if given, otherwise isère's submodule
if(NOT PICO_SDK_PATH)
  get_filename_component(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../3rdparty/pico-sdk" ABSOLUTE)
endif()
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

set(WITH_LWIP OFF)  # use included LwIP in pico-sdk

if(ISERE_WITH_OTEL)
  set(ISERE_WITH_OTEL OFF)
  message(WARNING "Target 'pico2' doesn't support OpenTelemetry -- disabled")
endif()

# pico-sdk's libraries are INTERFACE libraries resolved per executable, so isère's
# sources compile in each app instead of in an OBJECT core
set(ISERE_CORE_KIND INTERFACE)

SET(JERRY_GLOBAL_HEAP_SIZE "(70)" CACHE STRING "")

# C stack a JS engine may use on the httpd task (ISERE_HTTPD_TASK_STACK_SIZE: 24 KB)
set(ISERE_JS_STACK_KB 16)

# quickjs-ng assumes a POSIX host:
# - EMSCRIPTEN turns off its pthread and C11 atomics support, which newlib can't provide
#   (this also disables computed-goto dispatch, same as the old QuickJS build)
# - _POSIX_MONOTONIC_CLOCK makes newlib declare CLOCK_MONOTONIC for js__hrtime_ns()
set(ISERE_QUICKJS_DEFINITIONS EMSCRIPTEN _POSIX_MONOTONIC_CLOCK=200112L)
set(ISERE_QUICKJS_OPTIONS -include ${CMAKE_CURRENT_LIST_DIR}/include/quickjs_compat.h)
