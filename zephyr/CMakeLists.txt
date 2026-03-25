cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(isere LANGUAGES C)

# Build the Rust application
rust_cargo_application()

# QuickJS C library (Phase 5 - uncomment when ready)
# add_subdirectory(c_libs)

# Embed handler.js as a generated C header for the loader
# This converts handler.js into a byte array accessible from Rust via include_bytes!
