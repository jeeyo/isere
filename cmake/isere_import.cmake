# isère import file: copy it into your app project and include() it before project().
#
#   cmake_minimum_required(VERSION 3.19)
#   include(isere_import.cmake)
#   project(my_app C CXX ASM)
#   isere_init()
#   isere_add_app(my_app APP_DIRS app)
#
# isère is found at, in order:
#   - ISERE_PATH, as a CMake variable (-DISERE_PATH=...) or an environment variable
#   - an isere/ or lib/isere/ folder in the app (for example a git submodule)
#   - otherwise it is fetched from GitHub at ISERE_FETCH_FROM_GIT_TAG (default main),
#     with only the submodules the chosen board needs
#
# The canonical copy is cmake/isere_import.cmake in isère; keep yours identical to it.

if(DEFINED PROJECT_NAME)
  message(FATAL_ERROR "include isere_import.cmake before project(): it chooses the board's toolchain")
endif()

if(NOT ISERE_PATH AND DEFINED ENV{ISERE_PATH})
  set(ISERE_PATH "$ENV{ISERE_PATH}")
endif()

if(NOT ISERE_PATH)
  foreach(_isere_candidate
      "${CMAKE_CURRENT_LIST_DIR}/isere"
      "${CMAKE_SOURCE_DIR}/isere"
      "${CMAKE_SOURCE_DIR}/lib/isere")
    if(EXISTS "${_isere_candidate}/cmake/isere_init.cmake")
      set(ISERE_PATH "${_isere_candidate}")
      break()
    endif()
  endforeach()
endif()

if(NOT ISERE_PATH)
  set(ISERE_FETCH_FROM_GIT_TAG main CACHE STRING "isère branch or tag to fetch when ISERE_PATH is not set")
  # only what the board needs: a recursive clone would pull pico-sdk and its submodules
  set(_isere_submodules 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls)
  if(ISERE_BOARD STREQUAL "frdm-k64f" OR TARGET_PLATFORM STREQUAL "frdm-k64f")
    list(APPEND _isere_submodules 3rdparty/lwip)
  elseif(ISERE_BOARD STREQUAL "pico2" OR TARGET_PLATFORM STREQUAL "pico2")
    list(APPEND _isere_submodules 3rdparty/pico-sdk)
  endif()
  message(STATUS "Fetching isère ${ISERE_FETCH_FROM_GIT_TAG} from GitHub (set ISERE_PATH to use a local copy)")
  include(FetchContent)
  FetchContent_Populate(isere
    QUIET
    GIT_REPOSITORY https://github.com/jeeyo/isere.git
    GIT_TAG ${ISERE_FETCH_FROM_GIT_TAG}
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ${_isere_submodules}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/isere-src
    SUBBUILD_DIR ${CMAKE_BINARY_DIR}/_deps/isere-subbuild
    BINARY_DIR ${CMAKE_BINARY_DIR}/_deps/isere-build)
  set(ISERE_PATH "${CMAKE_BINARY_DIR}/_deps/isere-src")
endif()

get_filename_component(ISERE_PATH "${ISERE_PATH}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
if(NOT EXISTS "${ISERE_PATH}/cmake/isere_init.cmake")
  message(FATAL_ERROR "ISERE_PATH (${ISERE_PATH}) is not an isère checkout: it has no cmake/isere_init.cmake")
endif()
set(ISERE_PATH "${ISERE_PATH}" CACHE PATH "isère's source directory" FORCE)

include("${ISERE_PATH}/cmake/isere_init.cmake")
