# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

set(PICO_SDK_PATH 3rdparty/pico-sdk)
set(TINYUSB_LIBNETWORKING_SOURCES
  ${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dhserver.c
  ${PICO_SDK_PATH}/lib/tinyusb/lib/networking/rndis_reports.c
)

list(APPEND SOURCES
  portable/${TARGET_PLATFORM}/src/tusb_lwip_glue.c
  portable/${TARGET_PLATFORM}/src/usb_descriptor.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure/port.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure/portasm.c
  ${TINYUSB_LIBNETWORKING_SOURCES}
)
