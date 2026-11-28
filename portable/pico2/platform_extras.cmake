# the board's settings on isère's core, an INTERFACE library on this board
target_include_directories(isere_core INTERFACE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/non_secure
  ${PICO_SDK_PATH}/lib/tinyusb/src
  ${PICO_SDK_PATH}/lib/tinyusb/lib/networking
)

target_link_libraries(isere_core INTERFACE
  pico_stdlib
  pico_stdio_uart
  pico_multicore
  pico_lwip
  pico_lwip_contrib_freertos
  pico_rand
  hardware_exception
  pico_unique_id
  tinyusb_device
)

target_compile_definitions(isere_core INTERFACE
                            PICO_STDOUT_MUTEX=0
                            ISERE_WITH_LWIP)

# each firmware image: pico-sdk's stdio choice and its .uf2/.bin/.hex/.map outputs
function(isere_platform_target tgt)
  pico_enable_stdio_usb(${tgt} 0)
  pico_enable_stdio_uart(${tgt} 1)
  pico_enable_stdio_semihosting(${tgt} 0)
  pico_add_extra_outputs(${tgt})

  if(JS_RUNTIME STREQUAL quickjs)
    # the engine compiles in each app here (INTERFACE core), so its flags go on the app's directory
    set_source_files_properties(
      ${ISERE_DIR}/3rdparty/quickjs/quickjs.c
      ${ISERE_DIR}/3rdparty/quickjs/dtoa.c
      ${ISERE_DIR}/3rdparty/quickjs/libregexp.c
      ${ISERE_DIR}/3rdparty/quickjs/libunicode.c
      ${ISERE_DIR}/src/runtimes/quickjs/vm.c
      TARGET_DIRECTORY ${tgt}
      PROPERTIES
        COMPILE_DEFINITIONS "${ISERE_QUICKJS_DEFINITIONS}"
        COMPILE_OPTIONS "${ISERE_QUICKJS_OPTIONS}"
    )
  endif()
endfunction()
