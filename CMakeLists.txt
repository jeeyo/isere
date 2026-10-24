cmake_minimum_required(VERSION 3.19)

set(TARGET_PLATFORM linux CACHE STRING "Platform to compile isere for")

# build configuration
option(DEBUG "Debug mode" OFF)
set(JS_RUNTIME quickjs CACHE STRING "JavaScript runtime") # quickjs (default), jerryscript

option(WITH_OTEL "OpenTelemetry Exporter enabled" OFF)
set(OTEL_HOST "127.0.0.1" CACHE STRING "OpenTelemetry Collector OTLP/HTTP host")
set(OTEL_PORT 4318 CACHE STRING "OpenTelemetry Collector OTLP/HTTP port")

# the application: one or more directories, each with a routes.json and its JS modules
set(ISERE_APP_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/app" CACHE STRING "App directories to embed (;-separated)")

# platform-specific configuration
option(WITH_LWIP "Include LwIP" OFF)

include(portable/${TARGET_PLATFORM}/platform_features.cmake)

project(isere C CXX ASM)

# every path is absolute, so the build directory can be anywhere
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(FREERTOS_DIR ${ROOT}/3rdparty/FreeRTOS)
set(QUICKJS_DIR ${ROOT}/3rdparty/quickjs)
set(JERRYSCRIPT_DIR ${ROOT}/3rdparty/jerryscript)
set(MBEDTLS_DIR ${ROOT}/3rdparty/mbedtls)
set(LWIP_DIR ${ROOT}/3rdparty/lwip)
set(NANOPB_DIR ${ROOT}/3rdparty/nanopb)
set(OTELPROTO_DIR ${ROOT}/3rdparty/opentelemetry-proto)

set(SOURCES )
set(LIBS )

list(APPEND SOURCES
  ${ROOT}/src/crypto.c
  ${ROOT}/src/engine.c
  ${ROOT}/src/fetch.c
  ${ROOT}/src/hooks.c
  ${ROOT}/src/http_parser.c
  ${ROOT}/src/httpd.c
  ${ROOT}/src/loop.c
  ${ROOT}/src/mem.c
  ${ROOT}/src/metrics.c
  ${ROOT}/src/internals/uv_poll.c
  ${ROOT}/portable/${TARGET_PLATFORM}/src/fs.c
  ${ROOT}/portable/${TARGET_PLATFORM}/src/logger.c
  ${ROOT}/portable/${TARGET_PLATFORM}/src/rtc.c
  ${ROOT}/portable/${TARGET_PLATFORM}/src/tcp.c
  ${ROOT}/portable/${TARGET_PLATFORM}/src/platform.c
  ${FREERTOS_DIR}/croutine.c
  ${FREERTOS_DIR}/event_groups.c
  ${FREERTOS_DIR}/list.c
  ${FREERTOS_DIR}/queue.c
  ${FREERTOS_DIR}/stream_buffer.c
  ${FREERTOS_DIR}/tasks.c
  ${FREERTOS_DIR}/timers.c
  ${CMAKE_CURRENT_BINARY_DIR}/app_bundle.c
)

include(portable/${TARGET_PLATFORM}/platform_sources.cmake OPTIONAL)

if(WITH_LWIP)
  set(LWIP_INCLUDE_DIRS
    ${LWIP_DIR}/src/include
    ${ROOT}/portable/${TARGET_PLATFORM}/include
  )
  include(${LWIP_DIR}/src/Filelists.cmake)
endif()

# the app bundle: routes, modules, config and the prelude, regenerated when any of them change
set(APP_DEPENDS ${ROOT}/src/prelude.js ${ROOT}/tools/bundle_app.cmake)
foreach(dir IN LISTS ISERE_APP_DIRS)
  file(GLOB app_files CONFIGURE_DEPENDS ${dir}/*)
  list(APPEND APP_DEPENDS ${app_files})
endforeach()
string(REPLACE ";" "|" APP_DIRS_ARG "${ISERE_APP_DIRS}")
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/app_bundle.c
  COMMAND ${CMAKE_COMMAND} "-DAPP_DIRS=${APP_DIRS_ARG}" -DPRELUDE=${ROOT}/src/prelude.js
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/app_bundle.c -P ${ROOT}/tools/bundle_app.cmake
  DEPENDS ${APP_DEPENDS}
  COMMENT "Bundling the app from ${ISERE_APP_DIRS}"
  VERBATIM
)

if(JS_RUNTIME STREQUAL quickjs)
  list(APPEND SOURCES
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/dtoa.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${ROOT}/src/runtimes/quickjs/vm.c
  )
elseif(JS_RUNTIME STREQUAL jerryscript)
  list(APPEND SOURCES
    ${ROOT}/src/runtimes/jerryscript/vm.c
    ${ROOT}/src/runtimes/jerryscript/jerry-port.c
  )
endif()

# mbedTLS: only the modules isere_mbedtls_config.h enables end up in the binary
file(GLOB MBEDTLS_SOURCES ${MBEDTLS_DIR}/library/*.c)
add_library(isere_mbedtls STATIC ${MBEDTLS_SOURCES})
target_include_directories(isere_mbedtls PUBLIC ${MBEDTLS_DIR}/include ${ROOT}/include)
target_compile_definitions(isere_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="isere_mbedtls_config.h")
list(APPEND LIBS isere_mbedtls)

add_executable(isere ${SOURCES} ${ROOT}/src/main.c)

target_compile_definitions(isere PRIVATE
  ISERE_RUNTIME_NAME="${JS_RUNTIME}"
  __STDC_FORMAT_MACROS=1)

if(WITH_OTEL)
  target_sources(isere PRIVATE ${ROOT}/src/otel.c)
  target_compile_definitions(isere PRIVATE
    ISERE_WITH_OTEL
    ISERE_OTEL_HOST="${OTEL_HOST}"
    ISERE_OTEL_PORT=${OTEL_PORT})

  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${NANOPB_DIR}/extra)
  find_package(Nanopb REQUIRED)
  set(PROTOC_OPTIONS --experimental_allow_proto3_optional)
  nanopb_generate_cpp(TARGET otelproto RELPATH ${OTELPROTO_DIR}
    ${OTELPROTO_DIR}/opentelemetry/proto/metrics/v1/metrics.proto
    ${OTELPROTO_DIR}/opentelemetry/proto/common/v1/common.proto
    ${OTELPROTO_DIR}/opentelemetry/proto/resource/v1/resource.proto)
  target_link_libraries(isere PRIVATE otelproto)
endif()

if(JS_RUNTIME STREQUAL quickjs)
  target_compile_definitions(isere PRIVATE ISERE_RUNTIME_QUICKJS)
elseif(JS_RUNTIME STREQUAL jerryscript)
  # one context per engine instance, switched by the binding
  set(JERRY_EXTERNAL_CONTEXT ON CACHE BOOL "" FORCE)
  set(JERRY_PROMISE_CALLBACK ON CACHE BOOL "" FORCE)
  set(JERRY_ERROR_MESSAGES ON CACHE BOOL "" FORCE)
  set(JERRY_MEM_STATS ON CACHE BOOL "" FORCE)
  # lets the handler deadline stop a script that never yields
  set(JERRY_VM_HALT ON CACHE BOOL "" FORCE)
  set(JERRY_LINE_INFO ON CACHE BOOL "" FORCE)

  add_custom_target(amalgam)
  add_subdirectory(${JERRYSCRIPT_DIR}/jerry-core ${CMAKE_CURRENT_BINARY_DIR}/jerry-core)
  add_subdirectory(${JERRYSCRIPT_DIR}/jerry-ext ${CMAKE_CURRENT_BINARY_DIR}/jerry-ext)
  list(APPEND LIBS
    jerry-core
    jerry-ext
  )
  target_compile_definitions(isere PRIVATE ISERE_RUNTIME_JERRYSCRIPT)
  target_include_directories(isere PRIVATE
    ${JERRYSCRIPT_DIR}/jerry-core/include
    ${JERRYSCRIPT_DIR}/jerry-ext/include
  )
endif()

find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
  WORKING_DIRECTORY ${ROOT} OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(isere PRIVATE _GNU_SOURCE CONFIG_VERSION="${COMMIT_ID}")

set(ISERE_BUILD_ID "${COMMIT_ID}" CACHE STRING "Build identifier served at /__version")
target_compile_definitions(isere PRIVATE ISERE_BUILD_ID="${ISERE_BUILD_ID}")

if(DEBUG)
  target_compile_options(isere PRIVATE -O0 -g)

  if(JS_RUNTIME STREQUAL jerryscript)
    target_compile_options(jerry-core PRIVATE -O0 -g)
    target_compile_options(jerry-ext PRIVATE -O0 -g)
  endif()
else()
  target_compile_options(isere PRIVATE -O2 -g)
endif()

include(portable/${TARGET_PLATFORM}/platform_extras.cmake OPTIONAL)

target_include_directories(isere PRIVATE
  ${ROOT}/include
  ${ROOT}/portable/include
  ${ROOT}/portable/${TARGET_PLATFORM}/include
  ${FREERTOS_DIR}/include
  ${QUICKJS_DIR}
)

if(WITH_LWIP)
  target_include_directories(isere PRIVATE
    ${LWIP_DIR}/src/include
  )
  list(APPEND LIBS
    lwipallapps
    lwipcore
  )
  target_compile_definitions(isere PRIVATE ISERE_WITH_LWIP)
endif()

target_link_libraries(isere PRIVATE ${LIBS})
