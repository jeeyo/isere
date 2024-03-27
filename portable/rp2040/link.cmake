target_include_directories(isere PRIVATE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/include
  ${PICO_TINYUSB_PATH}/src
  ${PICO_TINYUSB_PATH}/lib/networking
)

target_link_libraries(isere pico_stdlib hardware_exception pico_unique_id tinyusb_device lwipallapps lwipcore)

pico_enable_stdio_usb(isere 0)
pico_enable_stdio_uart(isere 0)

target_compile_definitions(isere PRIVATE PICO_ENTER_USB_BOOT_ON_EXIT=1)

# create map/bin/hex file etc.
pico_add_extra_outputs(isere)
