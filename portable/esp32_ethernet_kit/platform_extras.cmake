target_compile_options(isere PRIVATE -Wno-error=incompatible-pointer-types)
target_include_directories(isere PRIVATE
  ${ESPIDF_DIR}/components/freertos/FreeRTOS-Kernel-SMP/include/freertos
)

# Link the static libraries to the executable
list(APPEND LIBS idf::freertos idf::esp_event idf::esp_netif idf::lwip idf::spi_flash)

# Attach additional targets to the executable file for flashing,
# linker script generation, partition_table generation, etc.
idf_build_executable(isere)
