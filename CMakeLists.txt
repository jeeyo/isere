cmake_minimum_required(VERSION 3.13)

include(3rdparty/pico-sdk/pico_sdk_init.cmake)

project(isere)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# # pull in common dependencies
# target_link_libraries(isere pico_stdlib)

set(FREERTOS_DIR ./3rdparty/FreeRTOS)
set(FREERTOS_POSIX_DIR ./3rdparty/FreeRTOS-Plus-POSIX)
set(QUICKJS_DIR ./3rdparty/quickjs)
set(LWIP_DIR ./3rdparty/lwip)
set(LLHTTP_DIR ./3rdparty/llhttp)
set(LIBYUAREL_DIR ./3rdparty/libyuarel)
set(CPPUTEST_DIR ./3rdparty/cpputest)
set(CAPNPROTO_DIR ./3rdparty/c-capnproto)

set(SOURCES
  src/main.c
  src/hooks.c
  src/http_handler.c
  src/httpd.c
  src/ini.c
  src/js.c
  src/polyfills/fetch.c
  src/polyfills/timer.c
  portable/src/fs.c
  portable/src/loader.c
  portable/src/logger.c
  portable/src/rtc.c
  portable/src/tcp.c
  schemas/config.capnp.c
  ${FREERTOS_DIR}/croutine.c
  ${FREERTOS_DIR}/event_groups.c
  ${FREERTOS_DIR}/list.c
  ${FREERTOS_DIR}/queue.c
  ${FREERTOS_DIR}/stream_buffer.c
  ${FREERTOS_DIR}/tasks.c
  ${FREERTOS_DIR}/timers.c
  ${FREERTOS_DIR}/portable/MemMang/heap_4.c
  # ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
  # ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/port.c
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
  ${QUICKJS_DIR}/quickjs.c
  # ${QUICKJS_DIR}/quickjs-libc.c
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

# create map/bin/hex file etc.
pico_add_extra_outputs(isere)

target_include_directories(isere PRIVATE
  ./include
  ./portable/include
  ./schemas
  ${FREERTOS_DIR}/include
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/include
  ${FREERTOS_POSIX_DIR}/include
  ${FREERTOS_POSIX_DIR}/include/FreeRTOS_POSIX
  ${FREERTOS_POSIX_DIR}/include/private
  ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include
  ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include/portable
  ${QUICKJS_DIR}/include
  # ${LWIP_DIR}/src/include
  ${LLHTTP_DIR}/include
  ${LIBYUAREL_DIR}
  ${CAPNPROTO_DIR}/lib
)
