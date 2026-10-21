# dksh_probe: SPIR-V of the deko3d shader library -> GLSL (SDK, SpirvAGlsl) -> DKSH (uam), on the PC.
# The same two steps the console's compile worker does, so a shader that fails here fails there.
#   cmake -S tools/shaders/dksh_probe -B build/dksh_probe -G Ninja && cmake --build build/dksh_probe
cmake_minimum_required(VERSION 3.20)
project(dksh_probe C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk")

add_subdirectory("${SDK}/thirdparty/uam" uam EXCLUDE_FROM_ALL)

set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${SDK}/thirdparty/SPIRV-Cross" spirv-cross EXCLUDE_FROM_ALL)

add_executable(dksh_probe dksh_probe.cpp "${SDK}/src/ui/deko3d/glsl.cpp")
target_include_directories(dksh_probe PRIVATE "${SDK}/include")
target_link_libraries(dksh_probe PRIVATE uamlib spirv-cross-glsl spirv-cross-core)
set_target_properties(dksh_probe PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
