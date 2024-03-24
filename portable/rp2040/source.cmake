# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

option(NEED_FREERTOS_POSIX "" false)
option(NEED_LWIP "" false)

list(APPEND SOURCES
  portable/${TARGET_PLATFORM}/src/quickjs-libc.c
  ${FREERTOS_DIR}/portable/MemMang/heap_4.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/port.c
)
