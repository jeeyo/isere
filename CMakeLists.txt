cmake_minimum_required(VERSION 3.5)

set(TARGET_PLATFORM linux CACHE STRING "Platform to compile isere for")

# build configuration
option(DEBUG "Debug mode" OFF)
set(JS_RUNTIME "quickjs" CACHE STRING "JavaScript runtime") # quickjs (default), jerryscript

# platform-specific configuration
option(WITH_LWIP "Include LwIP" OFF)
option(SUPPORT_DYNLINK "Platform supports Dynamic Linking" OFF)

include(portable/${TARGET_PLATFORM}/platform_features.cmake)

project(isere C CXX ASM)

set(FREERTOS_DIR ./3rdparty/FreeRTOS)
set(FREERTOS_POSIX_DIR ./3rdparty/FreeRTOS-Plus-POSIX)
set(QUICKJS_DIR ./3rdparty/quickjs)
set(JERRYSCRIPT_DIR ./3rdparty/jerryscript)
set(LWIP_DIR ./3rdparty/lwip)
set(LLHTTP_DIR ./3rdparty/llhttp)
set(LIBYUAREL_DIR ./3rdparty/libyuarel)
set(CAPNPROTO_DIR ./3rdparty/c-capnproto)
set(JS_DIR ./js)

set(SOURCES )

list(APPEND SOURCES
  ./src/hooks.c
  ./src/http_handler.c
  ./src/httpd.c
  ./src/ini.c
  ./src/js.c
  ./src/quickjs-libc.c
  ./src/polyfills/fetch.c
  ./src/polyfills/timer.c
  ./src/polyfills/pvPortRealloc.c
  ./portable/${TARGET_PLATFORM}/src/fs.c
  ./portable/${TARGET_PLATFORM}/src/loader.c
  ./portable/${TARGET_PLATFORM}/src/logger.c
  ./portable/${TARGET_PLATFORM}/src/rtc.c
  ./portable/${TARGET_PLATFORM}/src/tcp.c
  ./portable/${TARGET_PLATFORM}/src/platform.c
  ./schemas/config.capnp.c
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
  ${CAPNPROTO_DIR}/lib/capn-malloc.c
  ${CAPNPROTO_DIR}/lib/capn-stream.c
  ${CAPNPROTO_DIR}/lib/capn.c
)

if(JS_RUNTIME STREQUAL "quickjs")
  list(APPEND SOURCES
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/libbf.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${QUICKJS_DIR}/cutils.c
    ./src/runtimes/quickjs.c
  )
elseif()
  add_custom_target(amalgam)
  add_subdirectory(${JERRYSCRIPT_DIR}/jerry-math)
  add_subdirectory(${JERRYSCRIPT_DIR}/jerry-core)
  target_link_libraries(isere PRIVATE jerry-math jerry-core)
endif()

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

if(SUPPORT_DYNLINK)
  target_compile_definitions(isere PRIVATE ISERE_USE_DYNLINK)
endif()

find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(isere PRIVATE _GNU_SOURCE CONFIG_BIGNUM EMSCRIPTEN CONFIG_VERSION="${COMMIT_ID}")

if(DEBUG)
  target_compile_options(isere PRIVATE -O0 -g)

  # enable profiling on Linux
  if(TARGET_PLATFORM STREQUAL "linux")
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
  ${CAPNPROTO_DIR}/lib
)

if(WITH_LWIP)
  target_include_directories(isere PRIVATE
    ${LWIP_DIR}/src/include
  )
  target_link_libraries(isere lwipallapps lwipcore)
endif()

add_custom_command(
  OUTPUT ${CAPNPROTO_DIR}/build/capnpc-c/config.capnp.c
  COMMAND capnp compile -o ${CAPNPROTO_DIR}/build/capnpc-c ./schemas/config.capnp
  DEPENDS schemas/config.capnp
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Compiling Cap'n Proto schema"
  MAIN_DEPENDENCY ./schemas/config.capnp
  VERBATIM
)

if(TARGET_PLATFORM STREQUAL "linux")
  include(./tests/CMakeLists.txt)
endif()
