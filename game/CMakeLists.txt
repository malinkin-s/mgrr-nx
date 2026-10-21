# mgrr - Metal Gear Rising: Revengeance (Xbox 360), recompiled with ReXGlue.
#
# SDK boilerplate lives in generated/rexglue.cmake (written by `rexglue codegen mgrr_manifest.toml`).
# Configure with -DREXSDK_DIR=<repo>/sdk.

cmake_minimum_required(VERSION 3.25)
project(mgrr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# One function with ~86k SetFunction calls, run once at start-up: GCC -O3 did not finish it in 30+ min.
set_property(SOURCE generated/default/mgrr_register.cpp APPEND PROPERTY COMPILE_OPTIONS -O1)

set(MGRR_SOURCES
    src/main.cpp
)

add_executable(mgrr ${MGRR_SOURCES})

if(REXGLUE_PLATFORM_SWITCH)
    # Configure with the devkitPro Switch.cmake toolchain, -DREXGLUE_PLATFORM_SWITCH=ON -DREXGLUE_SWITCH_DEKO3D=ON.
    # No GPU emulation on Switch: the deko3d renderer of the port draws (M2).
    rexglue_setup_target(mgrr)
    rexglue_switch_add_deko3d(mgrr)
    rexglue_switch_add_nro(mgrr NAME "Metal Gear Rising" AUTHOR "mgr-nx" VERSION "0.0.1")
else()
    rexglue_setup_target(mgrr GPU_PLUGINS xenos)   # Xenia GPU emulation: reference renderer on PC (M1)
endif()
