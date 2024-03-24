# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

option(NEED_FREERTOS_POSIX "" true)
option(NEED_LWIP "" true)

list(APPEND SOURCES
  ${FREERTOS_DIR}/portable/MemMang/heap_4.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/port.c
)
