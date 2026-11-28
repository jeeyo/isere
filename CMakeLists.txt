# isère's own firmware: an app like any other, built with isère's CMake API (cmake/isere.cmake).
# An app project of your own does the same; see examples/hello-world.
cmake_minimum_required(VERSION 3.19)

set(ISERE_PATH "${CMAKE_CURRENT_LIST_DIR}")
include(cmake/isere_import.cmake)

project(isere C CXX ASM)

# the application: one or more directories, each with a routes.json and its JS modules; by
# default the JWT example (examples/jwks), and the test scripts add tests/e2e/app
set(ISERE_APP_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/examples/jwks/app" CACHE STRING "App directories to embed (;-separated)")

isere_init()
isere_add_app(isere APP_DIRS ${ISERE_APP_DIRS})
