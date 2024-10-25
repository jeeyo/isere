set(PICO_BOARD pico2)

include(3rdparty/pico-sdk/pico_sdk_init.cmake)

set(WITH_LWIP OFF)  # use included LwIP in pico-sdk
set(SUPPORT_DYNLINK OFF)

SET(JERRY_GLOBAL_HEAP_SIZE "(70)" CACHE STRING "")
