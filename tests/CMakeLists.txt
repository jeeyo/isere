cmake_minimum_required(VERSION 3.5)

project(iseretests C CXX ASM)

set(FREERTOS_DIR ../3rdparty/FreeRTOS)
set(QUICKJS_DIR ../3rdparty/quickjs)
set(LLHTTP_DIR ../3rdparty/llhttp)
set(LIBYUAREL_DIR ../3rdparty/libyuarel)

if (DEFINED ENV{CPPUTEST_HOME} AND NOT "$ENV{CPPUTEST_HOME}" STREQUAL "")
  add_custom_command(
    OUTPUT handler_for_test.c
    COMMAND xxd -i ../tests/handler_for_test.js handler_for_test.c
    COMMAND sed -i.bak -E "s/[a-z_]+\\[\\]/handler\\[\\]/" handler_for_test.c # change code array variable name to "handler"
    COMMAND sed -i.bak -E "s/[a-z_]+_len/handler_len/" handler_for_test.c # change code length variable name to "handler_len"
    COMMENT "Compiling JavaScript files for unit tests"
    MAIN_DEPENDENCY ../tests/handler_for_test.js
    VERBATIM
  )

  add_executable(iseretests
    ../src/hooks.c
    # ../src/http_handler.c
    ../src/httpd.c
    # ../src/js.c
    ../src/polyfills/pvPortRealloc.c
    ../src/internals/uv_poll.c

    ../portable/linux/src/loader.c

    handler_for_test.c
    # ../tests/loader_test.cpp
    ../tests/main.cpp

    ${FREERTOS_DIR}/croutine.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/portable/MemMang/heap_3.c
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
    ${LLHTTP_DIR}/src/api.c
    ${LLHTTP_DIR}/src/http.c
    ${LLHTTP_DIR}/src/llhttp.c
    ${LIBYUAREL_DIR}/yuarel.c
  )
  target_include_directories(iseretests PRIVATE
    $ENV{CPPUTEST_HOME}/include
    ../include
    ../portable/include
    ../portable/linux/include
    ../schemas
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
    ${QUICKJS_DIR}
    ${LLHTTP_DIR}/include
    ${LIBYUAREL_DIR}
  )
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
  target_compile_definitions(iseretests PRIVATE _GNU_SOURCE CONFIG_BIGNUM EMSCRIPTEN CONFIG_VERSION="${COMMIT_ID}")

  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(iseretests PRIVATE Threads::Threads m)

  target_link_libraries(iseretests PRIVATE $ENV{CPPUTEST_HOME}/lib/libCppUTest.a $ENV{CPPUTEST_HOME}/lib/libCppUTestExt.a)
else()
  message("CPPUTEST_HOME is not defined, skipping iseretests")
endif()
