// MGR's shaders for the SDK renderer: the library (mgrr_shader_library.h) as rex::x360::EntradaShader entries, and
// identification of the shaders the game uses.
//
// The ring only carries microcode (PM4 IM_LOAD). Pixel shaders arrive as compiled, so they are found by their
// microcode. Vertex shaders do not: D3D rewrites their vertex fetches for each vertex declaration and drops outputs
// (the same as in NFS, see xbox360-nx nfsmw_nativo_shaders.h). Measured on PC with the title screen's shaders
// (NOTES.md, M2): all 88 pixel shaders found by microcode, only 11 of 71 vertex shaders. So vertex shaders are
// identified by their container, which the game passes to CreateXShader (0x826ACC10 / 0x826ACDF8,
// x360_work/FETCH_AND_SHADERS.md), and the ring reader maps the IM_LOAD address back to it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include <rex/x360/shader_entry.h>

namespace mgrr::shaders {

struct Stats {
  uint64_t containers_seen = 0;     // CreateXShader calls
  uint64_t containers_found = 0;    // of those, in the library
  uint64_t microcode_lookups = 0;   // Identify calls with a microcode not seen before
  uint64_t microcode_found = 0;
  uint64_t microcode_ambiguous = 0; // found in more than one container with different SPIR-V
};

class Shaders {
 public:
  Shaders();
  ~Shaders();

  // false, with a warning in the log, if the library is missing or damaged.
  bool Load(const std::filesystem::path& path);
  bool loaded() const;
  size_t size() const;

  // The entry of a whole container (as passed to CreateXShader), or nullptr. Any thread, once loaded.
  const rex::x360::EntradaShader* FindContainer(std::span<const uint8_t> container);

  // The entry whose microcode (host byte order) is this one, ignoring what D3D patches in vertex fetches.
  // nullptr if none. Any thread, once loaded.
  const rex::x360::EntradaShader* Identify(bool vertex, std::span<const uint32_t> microcode);

  // The entry of a container by its hash (EntradaShader::huella_contenedor), or nullptr: a recording's shader table.
  const rex::x360::EntradaShader* FindContainerHash(uint64_t hash) const;

  // The library name of an entry ("shader.deferredcomposition"), for reports.
  const char* Name(const rex::x360::EntradaShader& entry) const;

  Stats stats() const;

 private:
  struct Data;
  std::unique_ptr<Data> data_;
};

// Fills `entry` (everything but spirv, juego, numero and huella_contenedor) from a 2008 container. Returns the
// reason if the container cannot be read, nullptr if it was.
const char* ReadContainer(std::span<const uint8_t> container, rex::x360::EntradaShader& entry);

}  // namespace mgrr::shaders
