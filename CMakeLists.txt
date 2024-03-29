cmake_minimum_required(VERSION 3.5)

set(TARGET_PLATFORM linux CACHE STRING "Platform to compile isere for")

option(DEBUG "Debug mode" OFF)
option(NEED_FREERTOS_POSIX "Include FreeRTOS-Plus-POSIX" OFF)
option(NEED_LWIP "Include LwIP" OFF)
option(SUPPORT_DYNLINK "Platform supports Dynamic Linking" OFF)

include(portable/${TARGET_PLATFORM}/platform_features.cmake)

project(isere C CXX ASM)

set(FREERTOS_DIR ./3rdparty/FreeRTOS)
set(FREERTOS_POSIX_DIR ./3rdparty/FreeRTOS-Plus-POSIX)
set(QUICKJS_DIR ./3rdparty/quickjs)
set(LWIP_DIR ./3rdparty/lwip)
set(LLHTTP_DIR ./3rdparty/llhttp)
set(LIBYUAREL_DIR ./3rdparty/libyuarel)
set(CAPNPROTO_DIR ./3rdparty/c-capnproto)
set(EXAMPLES_DIR ./examples)

set(SOURCES )

list(APPEND SOURCES
  ./src/main.c
  ./src/hooks.c
  ./src/http_handler.c
  ./src/httpd.c
  ./src/ini.c
  ./src/js.c
  ./src/polyfills/fetch.c
  ./src/polyfills/timer.c
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
  ${QUICKJS_DIR}/quickjs.c
  ${QUICKJS_DIR}/libbf.c
  ${QUICKJS_DIR}/libregexp.c
  ${QUICKJS_DIR}/libunicode.c
  ${QUICKJS_DIR}/cutils.c
  ${LLHTTP_DIR}/src/api.c
  ${LLHTTP_DIR}/src/http.c
  ${LLHTTP_DIR}/src/llhttp.c
  ${LIBYUAREL_DIR}/yuarel.c
  ${CAPNPROTO_DIR}/lib/capn-malloc.c
  ${CAPNPROTO_DIR}/lib/capn-stream.c
  ${CAPNPROTO_DIR}/lib/capn.c
)

if(NEED_FREERTOS_POSIX)
  list(APPEND SOURCES
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_clock.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_mqueue.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_barrier.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_cond.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_mutex.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_sched.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_semaphore.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_timer.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_unistd.c
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_utils.c
  )
endif()

include(portable/${TARGET_PLATFORM}/platform_sources.cmake OPTIONAL)

if(NEED_LWIP)
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
  COMMAND xxd -i ../${EXAMPLES_DIR}/handler.js handler.c
  COMMAND sed -i.bak -E "s/[a-z_]+\\[\\]/handler\\[\\]/" handler.c # change code array variable name to "handler"
  COMMAND sed -i.bak -E "s/[a-z_]+_len/handler_len/" handler.c # change code length variable name to "handler_len"
  COMMENT "Compiling JavaScript files"
  VERBATIM
)

add_executable(isere ${SOURCES})

if(SUPPORT_DYNLINK)
  target_compile_definitions(isere PRIVATE ISERE_USE_DYNLINK)
endif()

find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(isere PRIVATE _GNU_SOURCE CONFIG_BIGNUM EMSCRIPTEN CONFIG_VERSION="${COMMIT_ID}")

if(DEBUG)
  target_compile_options(isere PRIVATE -O0 -g)
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

if(NEED_FREERTOS_POSIX)
  target_include_directories(isere PRIVATE
    ${FREERTOS_POSIX_DIR}/include
    ${FREERTOS_POSIX_DIR}/include/FreeRTOS_POSIX
    ${FREERTOS_POSIX_DIR}/include/private
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include
    ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include/portable
  )
endif()

if(NEED_LWIP)
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
  VERBATIM
)

# add_executable(unittests
#   ./tests/js_test.cpp
#   ./tests/loader_test.cpp
#   ./tests/main.cpp
# )
# target_link_options(unittests PRIVATE -LCppUTest -lCppUTest -lCppUTestExt)
