# pico_sdk_init() has already run (isere_init)
set(TINYUSB_LIBNETWORKING_SOURCES
  ${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dhserver.c
  ${PICO_SDK_PATH}/lib/tinyusb/lib/networking/rndis_reports.c
)

list(APPEND ISERE_CORE_SOURCES
  ${ISERE_DIR}/portable/pico2/src/tusb_lwip_glue.c
  ${ISERE_DIR}/portable/pico2/src/usb_descriptor.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure/port.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure/portasm.c
  ${FREERTOS_DIR}/portable/MemMang/heap_3.c
  ${TINYUSB_LIBNETWORKING_SOURCES}
)
