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
# Without the PCH: clang rejects a PCH built at another -O level (the file includes mgrr_init.h itself).
set_property(SOURCE generated/default/mgrr_register.cpp APPEND PROPERTY COMPILE_OPTIONS -O1)
set_property(SOURCE generated/default/mgrr_register.cpp PROPERTY SKIP_PRECOMPILE_HEADERS ON)

set(MGRR_SOURCES
    src/main.cpp
    src/mgrr_draw_stats.cpp
)

add_executable(mgrr ${MGRR_SOURCES})

if(REXGLUE_PLATFORM_SWITCH)
    # Configure with the devkitPro Switch.cmake toolchain, -DREXGLUE_PLATFORM_SWITCH=ON -DREXGLUE_SWITCH_DEKO3D=ON.
    # No GPU emulation on Switch: the deko3d renderer of the port draws (M2).
    # src/mgrr_gpu.cpp stands in for the GPU (ring reader, interrupts, GPU memory writes); nothing is drawn yet.
    target_sources(mgrr PRIVATE src/mgrr_gpu.cpp)
    rexglue_setup_target(mgrr)
    rexglue_switch_add_deko3d(mgrr)
    target_compile_definitions(mgrr PRIVATE MGRR_DEKO3D=1)
    rexglue_switch_add_nro(mgrr NAME "Metal Gear Rising" AUTHOR "mgr-nx" VERSION "0.0.1")
else()
    rexglue_setup_target(mgrr GPU_PLUGINS xenos)   # Xenia GPU emulation: reference renderer on PC (M1)
endif()
