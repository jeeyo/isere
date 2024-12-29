set(PICO_BOARD pico2)

include(3rdparty/pico-sdk/pico_sdk_init.cmake)

set(PLATFORM_SDK_HAS_FREERTOS OFF)
set(PLATFORM_SDK_HAS_LWIP ON)  # pico-sdk has LwIP
set(PLATFORM_SUPPORT_DYNLINK OFF)

if(WITH_OTEL)
  set(WITH_OTEL OFF)
  message(WARNING "Target 'pico2' doesn't support OpenTelemetry -- disabled")
endif()

SET(JERRY_GLOBAL_HEAP_SIZE "(70)" CACHE STRING "")
