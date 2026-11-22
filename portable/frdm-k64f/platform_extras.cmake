target_include_directories(isere PRIVATE ${K64F_INCLUDE_DIRS})
target_compile_definitions(isere PRIVATE ${K64F_DEFINITIONS} ISERE_WITH_LWIP "ISERE_BOARD_ENV=\"${ISERE_BOARD_ENV}\"")
target_link_options(isere PRIVATE -T${K64F_DIR}/isere_flash.ld -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/isere.map)
set_target_properties(isere PROPERTIES SUFFIX ".elf")

foreach(lib lwipcore lwipallapps)
  if(TARGET ${lib})
    target_include_directories(${lib} PRIVATE ${K64F_INCLUDE_DIRS})
    target_compile_definitions(${lib} PRIVATE ${K64F_DEFINITIONS})
  endif()
endforeach()
target_include_directories(isere_mbedtls PRIVATE ${K64F_INCLUDE_DIRS})

if(JS_RUNTIME STREQUAL quickjs)
  # quickjs-ng assumes a POSIX host (the same settings as the Pico 2, see its platform_extras.cmake);
  # int32_t is long on arm-none-eabi, so its int/int32_t pointer mixes are errors in GCC 14
  set_source_files_properties(
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/dtoa.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${ROOT}/src/runtimes/quickjs/vm.c
    PROPERTIES
      COMPILE_DEFINITIONS "EMSCRIPTEN;_POSIX_MONOTONIC_CLOCK=200112L"
      COMPILE_OPTIONS "-include;${K64F_DIR}/include/quickjs_compat.h;-Wno-incompatible-pointer-types"
  )
elseif(JS_RUNTIME STREQUAL jerryscript)
  target_include_directories(jerry-core PRIVATE ${K64F_INCLUDE_DIRS})
endif()

add_custom_command(TARGET isere POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:isere> ${CMAKE_CURRENT_BINARY_DIR}/isere.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:isere>
  VERBATIM)
target_link_libraries(isere PRIVATE m)
