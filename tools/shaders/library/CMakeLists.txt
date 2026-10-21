# Host tools for MGR's shader library (game/src/mgrr_shader_library.h):
#   mgrr_hlsl          containers -> HLSL (vendored XenosRecomp)
#   mgrr_pack_shaders  containers + SPIR-V -> mgrr_shaders.mgsl, then checks it the way the game reads it
#   cmake -S tools/shaders/library -B build/shader_tools -G Ninja && cmake --build build/shader_tools
cmake_minimum_required(VERSION 3.20)
project(mgrr_shader_tools CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(SDK "${REPO}/sdk")

add_executable(mgrr_hlsl "${REPO}/tools/shaders/mgrr_hlsl.cpp" "${REPO}/tools/shaders/xenosrecomp/shader_recompiler.cpp")
target_include_directories(mgrr_hlsl PRIVATE "${REPO}/tools/shaders/xenosrecomp" "${SDK}/thirdparty/fmt/include"
                                              "${SDK}/thirdparty/xxHash")
target_compile_definitions(mgrr_hlsl PRIVATE FMT_HEADER_ONLY XXH_INLINE_ALL NFSMW_RECOMP)
target_compile_options(mgrr_hlsl PRIVATE -include "${REPO}/tools/shaders/xenosrecomp/pch_min.h" -w)
set_target_properties(mgrr_hlsl PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

add_executable(mgrr_pack_shaders mgrr_pack_shaders.cpp "${REPO}/game/src/mgrr_shader_library.cpp"
                                 "${REPO}/game/src/mgrr_shaders.cpp")
target_include_directories(mgrr_pack_shaders PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/stub" "${REPO}/game/src"
                                                      "${SDK}/include" "${SDK}/thirdparty/fmt/include"
                                                      "${SDK}/thirdparty/xxHash")
target_compile_definitions(mgrr_pack_shaders PRIVATE FMT_HEADER_ONLY)
set_target_properties(mgrr_pack_shaders PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
