# the board's settings on isère's core (included by _isere_build_core, after the core exists)
target_include_directories(isere_core PUBLIC ${K64F_INCLUDE_DIRS})
target_compile_definitions(isere_core PUBLIC ${K64F_DEFINITIONS})
# only platform.c reads it
target_compile_definitions(isere_core PRIVATE "ISERE_BOARD_ENV=\"${ISERE_BOARD_ENV}\"")
target_link_libraries(isere_core PUBLIC m)

foreach(lib lwipcore lwipallapps)
  if(TARGET ${lib})
    target_include_directories(${lib} PRIVATE ${K64F_INCLUDE_DIRS})
    target_compile_definitions(${lib} PRIVATE ${K64F_DEFINITIONS})
  endif()
endforeach()
target_include_directories(isere_mbedtls PRIVATE ${K64F_INCLUDE_DIRS})

if(JS_RUNTIME STREQUAL quickjs)
  # the binding includes quickjs-ng's headers, so it needs the engine's flags
  # (ISERE_QUICKJS_DEFINITIONS and _OPTIONS, from platform_features.cmake)
  set_source_files_properties(${ISERE_DIR}/src/runtimes/quickjs/vm.c PROPERTIES
    COMPILE_DEFINITIONS "${ISERE_QUICKJS_DEFINITIONS}"
    COMPILE_OPTIONS "${ISERE_QUICKJS_OPTIONS}")
elseif(JS_RUNTIME STREQUAL jerryscript)
  target_include_directories(jerry-core PRIVATE ${K64F_INCLUDE_DIRS})
endif()

# each firmware image: the linker script, a map file, the .elf name and a raw .bin for flashing
function(isere_platform_target tgt)
  set(dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
  target_link_options(${tgt} PRIVATE -T${dir}/isere_flash.ld -Wl,-Map=$<TARGET_FILE_DIR:${tgt}>/${tgt}.map)
  set_target_properties(${tgt} PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${dir}/isere_flash.ld)
  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${tgt}> $<TARGET_FILE_DIR:${tgt}>/${tgt}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${tgt}>
    VERBATIM)
endfunction()
