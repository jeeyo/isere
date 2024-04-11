# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

set(PICO_TINYUSB_PATH 3rdparty/pico-sdk/lib/tinyusb)
set(TINYUSB_LIBNETWORKING_SOURCES
  ${PICO_TINYUSB_PATH}/lib/networking/dhserver.c
  ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c
)

list(APPEND SOURCES
  portable/${TARGET_PLATFORM}/src/tusb_lwip_glue.c
  portable/${TARGET_PLATFORM}/src/usb_descriptor.c
  ${FREERTOS_DIR}/portable/MemMang/heap_4.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/port.c
  ${TINYUSB_LIBNETWORKING_SOURCES}
)
