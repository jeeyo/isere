set(PLATFORM_SDK_HAS_FREERTOS ON) # esp-idf has FreeRTOS
set(PLATFORM_SDK_HAS_LWIP ON) # esp-idf has LwIP
set(PLATFORM_ENTRYPOINT app_main)
set(PLATFORM_SUPPORT_DYNLINK OFF)

if(JS_RUNTIME STREQUAL quickjs)
  set(JS_RUNTIME jerryscript)
  message(WARNING "Target 'esp32_ethernet_kit' doesn't support QuickJS -- switching to JerryScript")
endif()

SET(JERRY_GLOBAL_HEAP_SIZE "(70)" CACHE STRING "")

set(ESPIDF_DIR ./3rdparty/esp-idf)

# Include for ESP-IDF build system functions
include(${ESPIDF_DIR}/tools/cmake/idf.cmake)

set(CMAKE_TOOLCHAIN_FILE ${ESPIDF_DIR}/tools/cmake/toolchain-esp32.cmake)
set(TARGET esp32)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
