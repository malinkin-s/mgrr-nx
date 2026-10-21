// MGR's shader library: every shader container of the game with its SPIR-V translation (deko3d mode).
//
// Built on the PC from the player's own disc (tools/shaders/build_library.sh) and loaded by the game; it holds data
// derived from the game, so it is never committed or shipped. The renderer finds a shader in it by its container
// (vertex shaders, through the CreateXShader hook) or by its microcode (pixel shaders, from the ring): mgrr_shaders.h.
//
// File (little-endian): "MGRSHLIB", u32 version, u32 count, then per shader
//   u32 name bytes, name (e.g. "shader.deferredcomposition"), u32 container bytes, container (as on the disc),
//   u32 SPIR-V words, SPIR-V
// and at the end the XXH3-64 of everything before it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mgrr::shaders {

struct LibraryShader {
  std::string name;
  std::vector<uint8_t> container;  // big-endian, as the game passes it to CreateXShader
  std::vector<uint32_t> spirv;
  uint64_t container_hash = 0;  // XXH3-64 of container
  bool vertex = false;          // signature 0x102A1101
};

inline constexpr uint32_t kLibraryVersion = 1;

// Serializes the shaders; throws std::runtime_error on a malformed one.
std::vector<uint8_t> PackLibrary(const std::vector<LibraryShader>& shaders);

// Parses and validates a library; throws std::runtime_error with the reason. Fills container_hash and vertex.
std::vector<LibraryShader> LoadLibrary(std::span<const uint8_t> file);
std::vector<LibraryShader> LoadLibrary(const std::filesystem::path& path);

uint64_t HashBytes(std::span<const uint8_t> bytes);

}  // namespace mgrr::shaders
