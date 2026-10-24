set(PICO_BOARD pico2)

include(3rdparty/pico-sdk/pico_sdk_init.cmake)

set(WITH_LWIP OFF)  # use included LwIP in pico-sdk

if(WITH_OTEL)
  set(WITH_OTEL OFF)
  message(WARNING "Target 'pico2' doesn't support OpenTelemetry -- disabled")
endif()

SET(JERRY_GLOBAL_HEAP_SIZE "(70)" CACHE STRING "")

# C stack a JS engine may use on the httpd task (ISERE_HTTPD_TASK_STACK_SIZE: 24 KB)
set(ISERE_JS_STACK_KB 16)
