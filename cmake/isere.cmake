# isère as a library for apps. After project():
#
#   isere_init()                                   builds isère's core once
#   isere_add_app(<target> APP_DIRS <dir>... [BUILD_ID <id>])
#
# <target> is an ordinary executable afterwards. Call both in the top-level CMakeLists.txt.
# Targets isère creates: isere_core, isere_quickjs, isere_jerry_internal, isere_mbedtls,
# jerry-core, jerry-ext, amalgam, lwipcore, lwipallapps, otelproto.

include_guard(GLOBAL)

# runs in the caller's scope: pico-sdk's init must run at the top level
macro(isere_init)
  if(TARGET isere_core)
    message(FATAL_ERROR "isere_init() was already called")
  endif()
  if(NOT DEFINED PROJECT_NAME)
    message(FATAL_ERROR "call isere_init() after project()")
  endif()
  get_property(_isere_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
  foreach(_isere_lang C ASM)
    if(NOT _isere_lang IN_LIST _isere_languages)
      enable_language(${_isere_lang})
    endif()
  endforeach()
  if(COMMAND pico_sdk_init)
    pico_sdk_init()
  endif()
  _isere_build_core()
endmacro()

function(_isere_git_describe dir out)
  set(id "unknown")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
      WORKING_DIRECTORY "${dir}" OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE rc ERROR_QUIET)
    if(rc EQUAL 0 AND sha)
      set(id "${sha}")
      execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${dir}" OUTPUT_VARIABLE dirty ERROR_QUIET)
      if(dirty)
        string(APPEND id "-dirty")
      endif()
    endif()
  endif()
  set(${out} "${id}" PARENT_SCOPE)
endfunction()

# everything but main.c and the app: built once, linked into every app
function(_isere_build_core)
  set(ROOT "${ISERE_DIR}")
  set(FREERTOS_DIR "${ISERE_DIR}/3rdparty/FreeRTOS")
  set(QUICKJS_DIR "${ISERE_DIR}/3rdparty/quickjs")
  set(JERRYSCRIPT_DIR "${ISERE_DIR}/3rdparty/jerryscript")
  set(MBEDTLS_DIR "${ISERE_DIR}/3rdparty/mbedtls")
  set(LWIP_DIR "${ISERE_DIR}/3rdparty/lwip")
  set(NANOPB_DIR "${ISERE_DIR}/3rdparty/nanopb")
  set(OTELPROTO_DIR "${ISERE_DIR}/3rdparty/opentelemetry-proto")

  if(NOT EXISTS "${FREERTOS_DIR}/tasks.c")
    message(FATAL_ERROR "isère's submodules are not checked out; run:\n"
      "  git -C ${ISERE_DIR} submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls 3rdparty/lwip")
  endif()

  # OBJECT by default, so link-time overrides (vector tables, strong IRQ handlers,
  # syscalls, FreeRTOS hooks) reach the link as they would from the app's own sources;
  # a board whose SDK resolves libraries per executable (pico2) uses INTERFACE
  if(NOT ISERE_CORE_KIND)
    set(ISERE_CORE_KIND OBJECT)
  endif()
  if(ISERE_CORE_KIND STREQUAL INTERFACE)
    set(scope INTERFACE)
  else()
    set(scope PUBLIC)
  endif()

  set(ISERE_CORE_SOURCES
    ${ISERE_DIR}/src/crypto.c
    ${ISERE_DIR}/src/engine.c
    ${ISERE_DIR}/src/fetch.c
    ${ISERE_DIR}/src/hooks.c
    ${ISERE_DIR}/src/http_parser.c
    ${ISERE_DIR}/src/httpd.c
    ${ISERE_DIR}/src/loop.c
    ${ISERE_DIR}/src/mem.c
    ${ISERE_DIR}/src/metrics.c
    ${ISERE_DIR}/src/internals/uv_poll.c
    ${ISERE_DIR}/portable/${TARGET_PLATFORM}/src/fs.c
    ${ISERE_DIR}/portable/${TARGET_PLATFORM}/src/logger.c
    ${ISERE_DIR}/portable/${TARGET_PLATFORM}/src/rtc.c
    ${ISERE_DIR}/portable/${TARGET_PLATFORM}/src/tcp.c
    ${ISERE_DIR}/portable/${TARGET_PLATFORM}/src/platform.c
    ${FREERTOS_DIR}/croutine.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/timers.c
  )
  if(JS_RUNTIME STREQUAL quickjs)
    list(APPEND ISERE_CORE_SOURCES ${ISERE_DIR}/src/runtimes/quickjs/vm.c)
  else()
    list(APPEND ISERE_CORE_SOURCES
      ${ISERE_DIR}/src/runtimes/jerryscript/vm.c
      ${ISERE_DIR}/src/runtimes/jerryscript/jerry-port.c)
  endif()
  if(ISERE_WITH_OTEL)
    list(APPEND ISERE_CORE_SOURCES ${ISERE_DIR}/src/otel.c)
  endif()

  # the board's FreeRTOS port, heap, drivers: appended to ISERE_CORE_SOURCES
  include(${ISERE_DIR}/portable/${TARGET_PLATFORM}/platform_sources.cmake OPTIONAL)

  if(ISERE_CORE_KIND STREQUAL INTERFACE)
    add_library(isere_core INTERFACE)
    target_sources(isere_core INTERFACE ${ISERE_CORE_SOURCES})
  else()
    add_library(isere_core ${ISERE_CORE_KIND} ${ISERE_CORE_SOURCES})
  endif()

  set(libs)

  if(WITH_LWIP)
    set(LWIP_INCLUDE_DIRS
      ${LWIP_DIR}/src/include
      ${ISERE_DIR}/portable/${TARGET_PLATFORM}/include
    )
    include(${LWIP_DIR}/src/Filelists.cmake)
    target_include_directories(isere_core ${scope} ${LWIP_DIR}/src/include)
    target_compile_definitions(isere_core ${scope} ISERE_WITH_LWIP)
    list(APPEND libs lwipallapps lwipcore)
  endif()

  # mbedTLS: only the modules isere_mbedtls_config.h enables end up in the binary
  file(GLOB mbedtls_sources ${MBEDTLS_DIR}/library/*.c)
  add_library(isere_mbedtls STATIC ${mbedtls_sources})
  target_include_directories(isere_mbedtls PUBLIC ${MBEDTLS_DIR}/include ${ISERE_DIR}/include)
  target_compile_definitions(isere_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="isere_mbedtls_config.h")
  list(APPEND libs isere_mbedtls)

  set(object_libs)
  if(JS_RUNTIME STREQUAL quickjs)
    set(quickjs_sources
      ${QUICKJS_DIR}/quickjs.c
      ${QUICKJS_DIR}/dtoa.c
      ${QUICKJS_DIR}/libregexp.c
      ${QUICKJS_DIR}/libunicode.c)
    if(ISERE_CORE_KIND STREQUAL INTERFACE)
      # compiled in each app; the board sets their properties in isere_platform_target()
      target_sources(isere_core INTERFACE ${quickjs_sources})
    else()
      # its own target, so the board's flags (ISERE_QUICKJS_DEFINITIONS/OPTIONS) apply
      # wherever the app is defined
      add_library(isere_quickjs OBJECT ${quickjs_sources})
      target_include_directories(isere_quickjs PRIVATE ${QUICKJS_DIR})
      target_compile_definitions(isere_quickjs PRIVATE _GNU_SOURCE __STDC_FORMAT_MACROS=1 ${ISERE_QUICKJS_DEFINITIONS})
      target_compile_options(isere_quickjs PRIVATE ${ISERE_QUICKJS_OPTIONS})
      list(APPEND object_libs isere_quickjs)
    endif()
    target_compile_definitions(isere_core ${scope} ISERE_RUNTIME_QUICKJS)
  else()
    # one context per engine instance, switched by the binding
    set(JERRY_EXTERNAL_CONTEXT ON CACHE BOOL "" FORCE)
    set(JERRY_PROMISE_CALLBACK ON CACHE BOOL "" FORCE)
    set(JERRY_ERROR_MESSAGES ON CACHE BOOL "" FORCE)
    set(JERRY_MEM_STATS ON CACHE BOOL "" FORCE)
    # lets the handler deadline stop a script that never yields
    set(JERRY_VM_HALT ON CACHE BOOL "" FORCE)
    set(JERRY_LINE_INFO ON CACHE BOOL "" FORCE)
    # deep recursion raises a RangeError instead of overflowing the httpd task's stack
    set(JERRY_STACK_LIMIT ${ISERE_JS_STACK_KB} CACHE STRING "" FORCE)

    # JerryScript's CMakeLists predates CMake 3.5, which CMake 4 no longer accepts by default
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
      set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    endif()
    if(NOT TARGET amalgam)
      add_custom_target(amalgam)
    endif()
    add_subdirectory(${JERRYSCRIPT_DIR}/jerry-core ${CMAKE_BINARY_DIR}/isere/jerry-core)
    add_subdirectory(${JERRYSCRIPT_DIR}/jerry-ext ${CMAKE_BINARY_DIR}/isere/jerry-ext)
    list(APPEND libs jerry-core jerry-ext)

    # src/runtimes/jerryscript/stack.c needs JerryScript's private headers
    add_library(isere_jerry_internal OBJECT ${ISERE_DIR}/src/runtimes/jerryscript/stack.c)
    target_link_libraries(isere_jerry_internal PRIVATE jerry-core)
    target_include_directories(isere_jerry_internal PRIVATE $<TARGET_PROPERTY:jerry-core,INCLUDE_DIRECTORIES>)
    list(APPEND object_libs isere_jerry_internal)

    target_compile_definitions(isere_core ${scope} ISERE_RUNTIME_JERRYSCRIPT)
    target_include_directories(isere_core ${scope}
      ${JERRYSCRIPT_DIR}/jerry-core/include
      ${JERRYSCRIPT_DIR}/jerry-ext/include)
  endif()

  if(ISERE_WITH_OTEL)
    target_compile_definitions(isere_core ${scope}
      ISERE_WITH_OTEL
      ISERE_OTEL_HOST="${ISERE_OTEL_HOST}"
      ISERE_OTEL_PORT=${ISERE_OTEL_PORT})
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${NANOPB_DIR}/extra)
    find_package(Nanopb REQUIRED)
    set(PROTOC_OPTIONS --experimental_allow_proto3_optional)
    nanopb_generate_cpp(TARGET otelproto RELPATH ${OTELPROTO_DIR}
      ${OTELPROTO_DIR}/opentelemetry/proto/metrics/v1/metrics.proto
      ${OTELPROTO_DIR}/opentelemetry/proto/common/v1/common.proto
      ${OTELPROTO_DIR}/opentelemetry/proto/resource/v1/resource.proto)
    list(APPEND libs otelproto)
  endif()

  _isere_git_describe("${ISERE_DIR}" isere_sha)
  string(REGEX REPLACE "-dirty$" "" isere_sha "${isere_sha}")
  target_compile_definitions(isere_core ${scope}
    ISERE_RUNTIME_NAME="${JS_RUNTIME}"
    ISERE_JS_STACK_SIZE=\(${ISERE_JS_STACK_KB}*1024\)
    __STDC_FORMAT_MACROS=1
    _GNU_SOURCE
    CONFIG_VERSION="${isere_sha}")
  target_include_directories(isere_core ${scope}
    ${ISERE_DIR}/include
    ${ISERE_DIR}/portable/include
    ${ISERE_DIR}/portable/${TARGET_PLATFORM}/include
    ${FREERTOS_DIR}/include
    ${QUICKJS_DIR})

  if(DEBUG)
    target_compile_options(isere_core ${scope} -O0 -g)
    if(TARGET jerry-core)
      target_compile_options(jerry-core PRIVATE -O0 -g)
      target_compile_options(jerry-ext PRIVATE -O0 -g)
    endif()
  endif()

  target_link_libraries(isere_core ${scope} ${libs})

  # the board's include directories, flags and libraries on the core, and its
  # isere_platform_target(<app>) for per-firmware steps (linker script, image outputs)
  include(${ISERE_DIR}/portable/${TARGET_PLATFORM}/platform_extras.cmake OPTIONAL)

  # objects of OBJECT libraries reach only the targets that link them directly
  set_property(GLOBAL PROPERTY ISERE_LINK_OBJECT_LIBS ${object_libs})
endfunction()

function(isere_add_app tgt)
  cmake_parse_arguments(ARG "" "BUILD_ID" "APP_DIRS" ${ARGN})
  if(NOT TARGET isere_core)
    message(FATAL_ERROR "isere_add_app(${tgt}): call isere_init() after project() first")
  endif()
  if(NOT ARG_APP_DIRS)
    message(FATAL_ERROR "isere_add_app(${tgt}): give at least one APP_DIRS directory with a routes.json")
  endif()

  set(app_dirs)
  set(app_depends ${ISERE_DIR}/src/prelude.js ${ISERE_DIR}/tools/bundle_app.cmake)
  file(GLOB prelude_parts CONFIGURE_DEPENDS ${ISERE_DIR}/src/prelude/*.js)
  list(APPEND app_depends ${prelude_parts})
  foreach(dir IN LISTS ARG_APP_DIRS)
    get_filename_component(dir "${dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${dir}/routes.json")
      message(FATAL_ERROR "isere_add_app(${tgt}): ${dir} has no routes.json")
    endif()
    list(APPEND app_dirs "${dir}")
    file(GLOB app_files CONFIGURE_DEPENDS "${dir}/*")
    list(APPEND app_depends ${app_files})
  endforeach()

  # the app: routes, modules, config and the prelude, regenerated when any of them change
  set(gen "${CMAKE_CURRENT_BINARY_DIR}/isere_gen/${tgt}")
  string(REPLACE ";" "|" app_dirs_arg "${app_dirs}")
  add_custom_command(
    OUTPUT ${gen}/app_bundle.c
    COMMAND ${CMAKE_COMMAND} "-DAPP_DIRS=${app_dirs_arg}" -DPRELUDE=${ISERE_DIR}/src/prelude.js
      -DOUTPUT=${gen}/app_bundle.c -P ${ISERE_DIR}/tools/bundle_app.cmake
    DEPENDS ${app_depends}
    COMMENT "Bundling ${tgt}'s app from ${app_dirs}"
    VERBATIM
  )

  # the build ID: given, or the app's commit plus isère's (just isère's for isère's own build)
  if(ARG_BUILD_ID)
    set(ISERE_APP_BUILD_ID "${ARG_BUILD_ID}")
  elseif(ISERE_BUILD_ID)
    set(ISERE_APP_BUILD_ID "${ISERE_BUILD_ID}")
  else()
    _isere_git_describe("${ISERE_DIR}" isere_sha)
    string(REGEX REPLACE "-dirty$" "" isere_sha "${isere_sha}")
    get_filename_component(app_root "${CMAKE_SOURCE_DIR}" REALPATH)
    get_filename_component(isere_root "${ISERE_DIR}" REALPATH)
    if(app_root STREQUAL isere_root)
      set(ISERE_APP_BUILD_ID "${isere_sha}")
    else()
      _isere_git_describe("${CMAKE_SOURCE_DIR}" app_sha)
      set(ISERE_APP_BUILD_ID "${app_sha}+isere.${isere_sha}")
    endif()
  endif()
  # configure_file rewrites it only when the ID changes, so a new ID recompiles one file
  configure_file(${ISERE_DIR}/cmake/build_info.c.in ${gen}/build_info.c @ONLY)

  add_executable(${tgt} ${ISERE_DIR}/src/main.c ${gen}/app_bundle.c ${gen}/build_info.c)
  target_link_libraries(${tgt} PRIVATE isere_core)
  get_property(object_libs GLOBAL PROPERTY ISERE_LINK_OBJECT_LIBS)
  foreach(lib IN LISTS object_libs)
    target_sources(${tgt} PRIVATE $<TARGET_OBJECTS:${lib}>)
  endforeach()

  if(COMMAND isere_platform_target)
    isere_platform_target(${tgt})
  endif()
endfunction()
