# JWT verification against a cached JWKS, as an isère app: the JavaScript in app/, built into
# firmware for a board (or into a Linux binary).
#
#   cmake -B build -DISERE_PATH=<isère checkout>     # Linux; inside isère's repository: -DISERE_PATH=../..
#   cmake -B build-k64f -DISERE_BOARD=frdm-k64f -DJS_RUNTIME=jerryscript -DISERE_PATH=../..
#   cmake --build build-k64f
cmake_minimum_required(VERSION 3.19)

# finds isère (ISERE_PATH, ./isere, or GitHub) and sets up the board's toolchain
include(isere_import.cmake)

project(isere_jwks C CXX ASM)

isere_init()
isere_add_app(isere_jwks APP_DIRS app)
