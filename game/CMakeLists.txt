# mgrr - Metal Gear Rising: Revengeance (Xbox 360), recompiled with ReXGlue.
#
# SDK boilerplate lives in generated/rexglue.cmake (written by `rexglue codegen mgrr_manifest.toml`).
# Configure with -DREXSDK_DIR=<repo>/sdk.

cmake_minimum_required(VERSION 3.25)
project(mgrr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

set(MGRR_SOURCES
    src/main.cpp
)

add_executable(mgrr ${MGRR_SOURCES})

rexglue_setup_target(mgrr GPU_PLUGINS xenos)   # Xenia GPU emulation: reference renderer on PC (M1)
