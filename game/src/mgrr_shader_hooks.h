// What the CreateXShader hooks (mgrr_shader_hooks.cpp) learn, for the ring reader (mgrr_gpu.cpp).

#pragma once

#include <cstdint>

#include <rex/x360/shader_entry.h>

namespace mgrr::shaders {

class Shaders;

// The shader library, loaded on first use (--mgrr_shader_library). Check loaded() before relying on it.
Shaders& Library();

// The library entry of the shader whose microcode copy holds this GPU physical address (an IM_LOAD address), as
// recorded when the game created it; nullptr if no created shader covers it. Any thread.
const rex::x360::EntradaShader* ShaderAtAddress(uint32_t physical_address);

// For diagnostics: the recorded copy that starts at or below this address (false if none).
struct CopyInfo {
  uint32_t start = 0, bytes = 0;
  const rex::x360::EntradaShader* entry = nullptr;
};
bool CopyBelow(uint32_t physical_address, CopyInfo& out);

// Writes the recorded copies as text, one per line: "physical bytes container_hash name" (hex), for replaying a GPU
// recording (sdk gpu_trace.h; tools/gputrace.py): the replay registers them again with AddCopy. false on error.
bool WriteCopies(const char* path);

// A copy known from elsewhere (a recording's table), as Record would have added it.
void AddCopy(uint32_t physical_address, uint32_t bytes, const rex::x360::EntradaShader* entry);

// Called once per guest frame (mgrr_draw_stats.cpp): writes <file>.shaders when a GPU recording ends (gpu_trace_last).
void OnFrameForTrace(uint64_t frame);

}  // namespace mgrr::shaders
