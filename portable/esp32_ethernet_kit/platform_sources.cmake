list(APPEND SOURCES
  portable/${TARGET_PLATFORM}/src/ethernet.c
)

# Create idf::{target} and idf::freertos static libraries
idf_build_process("esp32"
  # try and trim the build; additional components
  # will be included as needed based on dependency tree
  #
  # although esptool_py does not generate static library,
  # processing the component is needed for flashing related
  # targets and file generation
  COMPONENTS freertos esp_driver_gpio esp_eth esp_event esp_netif lwip esptool_py
  SDKCONFIG ${CMAKE_CURRENT_LIST_DIR}/sdkconfig
  BUILD_DIR ${CMAKE_BINARY_DIR}
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
