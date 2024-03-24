# create map/bin/hex file etc.
pico_add_extra_outputs(isere)

target_include_directories(isere PRIVATE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/include
)

target_link_libraries(isere pico_stdlib hardware_exception)
