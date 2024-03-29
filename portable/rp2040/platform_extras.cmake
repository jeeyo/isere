target_include_directories(isere PRIVATE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/include
  ${PICO_TINYUSB_PATH}/src
  ${PICO_TINYUSB_PATH}/lib/networking
)

target_link_libraries(isere
  pico_stdlib
  pico_lwip
  pico_lwip_freertos
  pico_lwip_contrib_freertos
  hardware_exception
  pico_unique_id
  tinyusb_device
)

target_compile_options(isere PRIVATE -fno-math-errno)

pico_enable_stdio_usb(isere 0)
pico_enable_stdio_uart(isere 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(isere)
