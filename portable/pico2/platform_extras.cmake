target_include_directories(isere PRIVATE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure
  ${PICO_SDK_PATH}/lib/tinyusb/src
  ${PICO_SDK_PATH}/lib/tinyusb/lib/networking
)

list(APPEND LIBS
  pico_stdlib
  pico_stdio_uart
  pico_multicore
  pico_lwip
  pico_lwip_contrib_freertos
  pico_rand
  hardware_exception
  pico_unique_id
  tinyusb_device
)

target_compile_definitions(isere PRIVATE
                            PICO_STDOUT_MUTEX=0
                            ISERE_WITH_LWIP)
# target_compile_options(isere PRIVATE -fno-math-errno)

pico_enable_stdio_usb(isere 0)
pico_enable_stdio_uart(isere 1)
pico_enable_stdio_semihosting(isere 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(isere)

if(JS_RUNTIME STREQUAL quickjs)
  # quickjs-ng assumes a POSIX host:
  # - EMSCRIPTEN turns off its pthread and C11 atomics support, which newlib can't provide
  #   (this also disables computed-goto dispatch, same as the old QuickJS build)
  # - _POSIX_MONOTONIC_CLOCK makes newlib declare CLOCK_MONOTONIC for js__hrtime_ns()
  set_source_files_properties(
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/dtoa.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ./src/runtimes/quickjs/quickjs.c
    ./src/runtimes/quickjs/quickjs-libc.c
    ./src/polyfills/quickjs/fetch.c
    ./src/polyfills/quickjs/timer.c
    PROPERTIES
      COMPILE_DEFINITIONS "EMSCRIPTEN;_POSIX_MONOTONIC_CLOCK=200112L"
      COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/portable/pico2/include/quickjs_compat.h"
  )
endif()
