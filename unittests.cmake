if (DEFINED ENV{CPPUTEST_HOME} AND NOT "$ENV{CPPUTEST_HOME}" STREQUAL "")
  add_custom_command(
    OUTPUT handler_for_test.c
    COMMAND xxd -i ../tests/handler.js handler_for_test.c
    COMMAND sed -i.bak -E "s/[a-z_]+\\[\\]/handler\\[\\]/" handler_for_test.c # change code array variable name to "handler"
    COMMAND sed -i.bak -E "s/[a-z_]+_len/handler_len/" handler_for_test.c # change code length variable name to "handler_len"
    COMMENT "Compiling JavaScript files for unit tests"
    MAIN_DEPENDENCY ./tests/handler.js
    VERBATIM
  )

  add_executable(unittests
    ${SOURCES}
    handler_for_test.c
    ./tests/js_test.cpp
    ./tests/loader_test.cpp
    ./tests/main.cpp
  )
  target_include_directories(unittests PRIVATE
    $ENV{CPPUTEST_HOME}/include
    ./include
    ./portable/include
    ./portable/linux/include
    ./schemas
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
    ${QUICKJS_DIR}
    ${LLHTTP_DIR}/include
    ${LIBYUAREL_DIR}
    ${CAPNPROTO_DIR}/lib
  )
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
  target_compile_definitions(unittests PRIVATE _GNU_SOURCE CONFIG_BIGNUM EMSCRIPTEN CONFIG_VERSION="${COMMIT_ID}")

  target_link_options(unittests PRIVATE -L$ENV{CPPUTEST_HOME}/lib -lCppUTest -lCppUTestExt -lm -ldl -pthread)
else()
  message("CPPUTEST_HOME is not defined, skipping unittests")
endif()
