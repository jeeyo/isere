cmake_minimum_required(VERSION 3.5)

set(TARGET_PLATFORM linux CACHE STRING "Platform to compile isere for")

# build configuration
option(DEBUG "Debug mode" OFF)
set(JS_RUNTIME quickjs CACHE STRING "JavaScript runtime") # quickjs (default), jerryscript

option(WITH_OTEL "OpenTelemetry Exporter enabled" OFF)
set(OTEL_HOST "127.0.0.1" CACHE STRING "OpenTelemetry Collector OTLP/HTTP host")
set(OTEL_PORT 4318 CACHE STRING "OpenTelemetry Collector OTLP/HTTP port")

# platform-specific configuration
option(WITH_LWIP "Include LwIP" OFF)
option(SUPPORT_DYNLINK "Platform supports Dynamic Linking" OFF)

include(portable/${TARGET_PLATFORM}/platform_features.cmake)

project(isere C CXX ASM)

set(FREERTOS_DIR ./3rdparty/FreeRTOS)
set(QUICKJS_DIR ./3rdparty/quickjs)
set(JERRYSCRIPT_DIR ./3rdparty/jerryscript)
set(LWIP_DIR ./3rdparty/lwip)
set(LLHTTP_DIR ./3rdparty/llhttp)
set(LIBYUAREL_DIR ./3rdparty/libyuarel)
set(NANOPB_DIR ./3rdparty/nanopb)
set(OTELPROTO_DIR ./3rdparty/opentelemetry-proto)
set(JS_DIR ./js)

set(SOURCES )
set(LIBS )

list(APPEND SOURCES
  ./src/hooks.c
  ./src/http_handler.c
  ./src/httpd.c
  ./src/js.c
  ./src/polyfills/pvPortRealloc.c
  ./src/internals/uv_poll.c
  ./portable/${TARGET_PLATFORM}/src/fs.c
  ./portable/${TARGET_PLATFORM}/src/loader.c
  ./portable/${TARGET_PLATFORM}/src/logger.c
  ./portable/${TARGET_PLATFORM}/src/rtc.c
  ./portable/${TARGET_PLATFORM}/src/tcp.c
  ./portable/${TARGET_PLATFORM}/src/platform.c
  ${FREERTOS_DIR}/croutine.c
  ${FREERTOS_DIR}/event_groups.c
  ${FREERTOS_DIR}/list.c
  ${FREERTOS_DIR}/queue.c
  ${FREERTOS_DIR}/stream_buffer.c
  ${FREERTOS_DIR}/tasks.c
  ${FREERTOS_DIR}/timers.c
  ${FREERTOS_DIR}/portable/MemMang/heap_3.c
  ${LLHTTP_DIR}/src/api.c
  ${LLHTTP_DIR}/src/http.c
  ${LLHTTP_DIR}/src/llhttp.c
  ${LIBYUAREL_DIR}/yuarel.c
)

include(portable/${TARGET_PLATFORM}/platform_sources.cmake OPTIONAL)

if(WITH_LWIP)
  set(LWIP_INCLUDE_DIRS
    ${LWIP_DIR}/src/include
    ./portable/${TARGET_PLATFORM}/include
  )
  include(${LWIP_DIR}/src/Filelists.cmake)
endif()

if(SUPPORT_DYNLINK)
  add_executable(handler handler.c)
  set_target_properties(handler PROPERTIES PREFIX "")
  set_target_properties(handler PROPERTIES SUFFIX ".so")
  target_link_options(handler PRIVATE -shared -fPIC)
else()
  list(APPEND SOURCES
    handler.c
  )
endif()

if(JS_RUNTIME STREQUAL quickjs)
  list(APPEND SOURCES
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/libbf.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${QUICKJS_DIR}/cutils.c
    ./src/runtimes/quickjs/quickjs.c
    ./src/runtimes/quickjs/quickjs-libc.c
    ./src/polyfills/quickjs/fetch.c
    ./src/polyfills/quickjs/timer.c
  )
elseif(JS_RUNTIME STREQUAL jerryscript)
  list(APPEND SOURCES
    ./src/runtimes/jerryscript/jerryscript.c
    ./src/runtimes/jerryscript/jerry-common.c
    ./src/runtimes/jerryscript/jerry-port.c
    ./src/polyfills/jerryscript/timer.c
  )
endif()

add_custom_command(
  OUTPUT handler.c
  COMMAND xxd -i ../${JS_DIR}/handler.js handler.c
  COMMAND sed -i.bak -E "s/[a-z_]+\\[\\]/handler\\[\\]/" handler.c # change code array variable name to "handler"
  COMMAND sed -i.bak -E "s/[a-z_]+_len/handler_len/" handler.c # change code length variable name to "handler_len"
  COMMENT "Compiling JavaScript files"
  MAIN_DEPENDENCY ../${JS_DIR}/handler.js
  VERBATIM
)

add_executable(isere ${SOURCES} ./src/main.c)

target_compile_definitions(isere PRIVATE
  ISERE_RUNTIME_NAME="${JS_RUNTIME}"
  __STDC_FORMAT_MACROS=1)

if(WITH_OTEL)
  list(APPEND SOURCES
    ./src/otel.c
  )
  target_compile_definitions(isere PRIVATE
    ISERE_WITH_OTEL
    ISERE_OTEL_HOST="${OTEL_HOST}"
    ISERE_OTEL_PORT=${OTEL_PORT})

  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/${NANOPB_DIR}/extra)
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
  SET(JERRY_EXTERNAL_CONTEXT ON CACHE BOOL "")
  SET(JERRY_PROMISE_CALLBACK ON CACHE BOOL "")
  SET(JERRY_ERROR_MESSAGES ON CACHE BOOL "")
  SET(JERRY_MEM_STATS ON CACHE BOOL "")

  add_custom_target(amalgam)
  add_subdirectory(${JERRYSCRIPT_DIR}/jerry-core)
  add_subdirectory(${JERRYSCRIPT_DIR}/jerry-ext)
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

if(SUPPORT_DYNLINK)
  target_compile_definitions(isere PRIVATE ISERE_USE_DYNLINK)
endif()

find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(isere PRIVATE _GNU_SOURCE CONFIG_BIGNUM EMSCRIPTEN CONFIG_VERSION="${COMMIT_ID}")

if(DEBUG)
  target_compile_options(isere PRIVATE -O0 -g)

  if(JS_RUNTIME STREQUAL jerryscript)
    target_compile_options(jerry-core PRIVATE -O0 -g)
    target_compile_options(jerry-ext PRIVATE -O0 -g)
  endif()

  # enable profiling on Linux
  if(TARGET_PLATFORM STREQUAL linux)
    # target_compile_definitions(isere PRIVATE DUMP_LEAKS DUMP_MEM)
    target_compile_options(isere PRIVATE -pg)
    target_link_options(isere PRIVATE -pg)
  endif()
endif()

include(portable/${TARGET_PLATFORM}/platform_extras.cmake OPTIONAL)

target_include_directories(isere PRIVATE
  ./include
  ./portable/include
  ./portable/${TARGET_PLATFORM}/include
  ./schemas
  ${FREERTOS_DIR}/include
  ${QUICKJS_DIR}
  ${LLHTTP_DIR}/include
  ${LIBYUAREL_DIR}
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
