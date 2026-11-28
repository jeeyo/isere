# An isère app: the JavaScript in app/, built into firmware for a board (or a Linux binary).
#
#   cmake -B build                                   # Linux, QuickJS
#   cmake -B build-k64f -DISERE_BOARD=frdm-k64f -DJS_RUNTIME=jerryscript
#   cmake --build build-k64f
cmake_minimum_required(VERSION 3.19)

# finds isère (ISERE_PATH, ./isere, or GitHub) and sets up the board's toolchain
include(isere_import.cmake)

project(hello_world C CXX ASM)

isere_init()
isere_add_app(hello_world APP_DIRS app)
