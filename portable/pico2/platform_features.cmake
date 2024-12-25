set(PICO_BOARD pico2)

include(3rdparty/pico-sdk/pico_sdk_init.cmake)

set(WITH_LWIP OFF)  # use included LwIP in pico-sdk
set(SUPPORT_DYNLINK OFF)

if(WITH_OTEL)
  set(WITH_OTEL OFF)
  message(WARNING "Target 'pico2' doesn't support OpenTelemetry -- disabled")
endif()

SET(JERRY_GLOBAL_HEAP_SIZE "(70)" CACHE STRING "")
