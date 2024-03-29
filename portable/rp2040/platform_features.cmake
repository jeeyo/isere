include(3rdparty/pico-sdk/pico_sdk_init.cmake)

set(NEED_FREERTOS_POSIX OFF)
set(NEED_LWIP OFF)  # use included LwIP in pico-sdk
set(SUPPORT_DYNLINK OFF)
