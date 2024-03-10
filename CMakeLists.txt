cmake_minimum_required(VERSION 3.13)

# include(3rdparty/pico-sdk/pico_sdk_init.cmake)

project(isere)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initialize the Raspberry Pi Pico SDK
# pico_sdk_init()

# # pull in common dependencies
# target_link_libraries(isere pico_stdlib)

find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
add_compile_options(-D_GNU_SOURCE -DCONFIG_BIGNUM -DEMSCRIPTEN -DCONFIG_VERSION="${COMMIT_ID}")

set(FREERTOS_DIR ./3rdparty/FreeRTOS)
set(FREERTOS_POSIX_DIR ./3rdparty/FreeRTOS-Plus-POSIX)
set(QUICKJS_DIR ./3rdparty/quickjs)
set(LWIP_DIR ./3rdparty/lwip)
set(LLHTTP_DIR ./3rdparty/llhttp)
set(LIBYUAREL_DIR ./3rdparty/libyuarel)
set(CPPUTEST_DIR ./3rdparty/cpputest)
set(CAPNPROTO_DIR ./3rdparty/c-capnproto)
set(EXAMPLES_DIR ./examples)

set(SOURCES
  src/main.c
  src/hooks.c
  src/http_handler.c
  src/httpd.c
  src/ini.c
  src/js.c
  src/polyfills/fetch.c
  src/polyfills/timer.c
  portable/linux/src/fs.c
  portable/linux/src/loader.c
  portable/linux/src/logger.c
  portable/linux/src/rtc.c
  portable/linux/src/tcp.c
  schemas/config.capnp.c
  ${FREERTOS_DIR}/croutine.c
  ${FREERTOS_DIR}/event_groups.c
  ${FREERTOS_DIR}/list.c
  ${FREERTOS_DIR}/queue.c
  ${FREERTOS_DIR}/stream_buffer.c
  ${FREERTOS_DIR}/tasks.c
  ${FREERTOS_DIR}/timers.c
  ${FREERTOS_DIR}/portable/MemMang/heap_4.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
  # ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/port.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_clock.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_mqueue.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_barrier.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_cond.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_mutex.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_sched.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_semaphore.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_timer.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_unistd.c
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_utils.c
  ${QUICKJS_DIR}/quickjs.c
  ${QUICKJS_DIR}/quickjs-libc.c
  ${QUICKJS_DIR}/libbf.c
  ${QUICKJS_DIR}/libregexp.c
  ${QUICKJS_DIR}/libunicode.c
  ${QUICKJS_DIR}/cutils.c
  # $(wildcard ${LWIP_DIR}/src/api/*.c)
  # $(wildcard ${LWIP_DIR}/src/core/ipv4/*.c)
  # $(wildcard ${LWIP_DIR}/src/core/*.c)
  # ${LWIP_DIR}/src/netif/ethernet.c
  # $(wildcard ${LWIP_DIR}/src/netif/ppp/*.c)
  # $(wildcard ${LWIP_DIR}/src/netif/ppp/polarssl/*.c)
  ${LLHTTP_DIR}/src/api.c
  ${LLHTTP_DIR}/src/http.c
  ${LLHTTP_DIR}/src/llhttp.c
  ${LIBYUAREL_DIR}/yuarel.c
  ${CAPNPROTO_DIR}/lib/capn-malloc.c
  ${CAPNPROTO_DIR}/lib/capn-stream.c
  ${CAPNPROTO_DIR}/lib/capn.c
  )

add_executable(isere ${SOURCES})

# # create map/bin/hex file etc.
# pico_add_extra_outputs(isere)

target_include_directories(isere PRIVATE
  ./include
  ./portable/include
  ./schemas
  ${FREERTOS_DIR}/include
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
  # ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/include
  # ${FREERTOS_POSIX_DIR}/include
  # ${FREERTOS_POSIX_DIR}/include/FreeRTOS_POSIX
  # ${FREERTOS_POSIX_DIR}/include/private
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include
  # ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include/portable
  ${QUICKJS_DIR}
  # ${LWIP_DIR}/src/include
  ${LLHTTP_DIR}/include
  ${LIBYUAREL_DIR}
  ${CAPNPROTO_DIR}/lib
)

# add_custom_command(
#   OUTPUT 3rdparty/c-capnproto/build/capnpc-c/config.capnp.c
#   COMMAND capnp compile -o 3rdparty/c-capnproto/build/capnpc-c schemas/config.capnp
#   DEPENDS schemas/config.capnp
#   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#   COMMENT "Compiling Cap'n Proto schema"
#   VERBATIM
#   )

add_library(js SHARED ${EXAMPLES_DIR}/echo.esm.c)
add_custom_command(
  OUTPUT ${EXAMPLES_DIR}/echo.esm.c
  COMMAND xxd -i ${EXAMPLES_DIR}/echo.esm.js ${EXAMPLES_DIR}/echo.esm.c
  COMMAND sed -i -E "s/[a-z_]+\\[\\]/handler\\[\\]/" ${EXAMPLES_DIR}/echo.esm.c # change code array variable name to "handler"
  COMMAND sed -i -E "s/[a-z_]+_len/handler_len/" ${EXAMPLES_DIR}/echo.esm.c # change code length variable name to "handler_len"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Compiling JavaScript files"
  VERBATIM
  )
