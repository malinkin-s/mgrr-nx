// The game's shader creation, matched against the shader library (mgrr_shaders.h).
//
// CreateXShader (0x826ACC10 and 0x826ACDF8; x360_work/FETCH_AND_SHADERS.md) receives the whole compiled container
// in r3: [virtual size at +4][physical size at +8], magic 0x102A1101 vertex, 0x102A1100 pixel. Both functions take
// both types (measured on PC). It returns the D3D shader object in r3; the object keeps its own copy of the
// microcode in GPU memory, pointer at +24 for a pixel shader and +32 for a vertex shader (object dumps on PC), and
// IM_LOAD later loads from that copy (plus the block's offset into it), patched by D3D for vertex shaders.
//
// So the container is looked up here, and the copy's address range is recorded with the entry: the ring reader
// (mgrr_gpu.cpp) finds a vertex shader by its IM_LOAD address, which its patched microcode cannot do.
// --mgrr_shader_library (default: mgrr_shaders.mgsl next to the executable), built by tools/shaders/build_library.sh.

#include "generated/default/mgrr_init.h"

#include "mgrr_shader_hooks.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "mgrr_shaders.h"

REXCVAR_DEFINE_STRING(mgrr_shader_library, "", "MGRR",
                      "Shader library (tools/shaders/build_library.sh); empty: mgrr_shaders.mgsl next to the executable");

namespace mgrr::shaders {
namespace {

Shaders g_shaders;
std::once_flag g_load_once;

// Microcode copies of created shaders: physical start -> (bytes, entry). Written by the game thread that creates
// shaders, read by the ring thread on every IM_LOAD.
struct Copy {
  uint32_t bytes;
  const rex::x360::EntradaShader* entry;
};
std::shared_mutex g_copies_lock;
std::map<uint32_t, Copy> g_copies;

std::mutex g_report_lock;
std::unordered_set<uint32_t> g_missing_logged;  // container sizes already reported, to keep the log short
uint32_t g_missing_lines = 0;
uint64_t g_recorded = 0;
std::chrono::steady_clock::time_point g_last_report = std::chrono::steady_clock::now();

// GPU physical address of a guest address (REX_PHYS_HOST_OFFSET: the 0xE0000000 view is 4 KB further in). Checked
// on console: vertex copies at EA4BC000 (even pages; pixel ones odd), IM_LOAD of a vertex shader at 0A4BD000.
uint32_t Physical(uint32_t guest) {
  return (guest & 0x1FFFFFFF) + (guest >= 0xE0000000u ? 0x1000u : 0u);
}

}  // namespace

Shaders& Library() {
  std::call_once(g_load_once, [] {
    std::string path = REXCVAR_GET(mgrr_shader_library);
    if (path.empty()) path = (rex::filesystem::GetExecutableFolder() / "mgrr_shaders.mgsl").string();
    g_shaders.Load(path);
  });
  return g_shaders;
}

const rex::x360::EntradaShader* ShaderAtAddress(uint32_t physical_address) {
  std::shared_lock<std::shared_mutex> guard(g_copies_lock);
  auto it = g_copies.upper_bound(physical_address);
  if (it == g_copies.begin()) return nullptr;
  --it;
  return physical_address - it->first < it->second.bytes ? it->second.entry : nullptr;
}

bool CopyBelow(uint32_t physical_address, CopyInfo& out) {
  std::shared_lock<std::shared_mutex> guard(g_copies_lock);
  auto it = g_copies.upper_bound(physical_address);
  if (it == g_copies.begin()) return false;
  --it;
  out = {it->first, it->second.bytes, it->second.entry};
  return true;
}

bool WriteCopies(const char* path) {
  FILE* f = std::fopen(path, "w");
  if (!f) return false;
  std::fprintf(f, "# physical bytes container_hash name\n");
  std::shared_lock<std::shared_mutex> guard(g_copies_lock);
  for (const auto& [start, copy] : g_copies) {
    std::fprintf(f, "%08X %X %016llX %s\n", start, copy.bytes, (unsigned long long)copy.entry->huella_contenedor,
                 g_shaders.Name(*copy.entry));
  }
  std::fclose(f);
  return true;
}

void AddCopy(uint32_t physical_address, uint32_t bytes, const rex::x360::EntradaShader* entry) {
  if (!entry) return;
  std::unique_lock<std::shared_mutex> guard(g_copies_lock);
  g_copies[physical_address] = {bytes, entry};
}

void OnFrameForTrace(uint64_t frame) {
  // gpu_trace.h counts the GPU's swaps, this the game's; the game is ahead by the frames in flight, so a second
  // of margin. The table only grows, so later is never wrong.
  static bool written = false;
  if (written) return;
  const std::string path = rex::cvar::GetFlagByName("gpu_trace_path");
  if (path.empty()) {
    written = true;
    return;
  }
  const uint64_t end = uint64_t(std::max(0, rex::cvar::Query<int32_t>("gpu_trace_start"))) +
                       uint64_t(std::max(0, rex::cvar::Query<int32_t>("gpu_trace_frames"))) + 60;
  if (frame < end) return;
  written = true;
  const std::string table = path + ".shaders";
  if (WriteCopies(table.c_str())) {
    REXLOG_INFO("[shaders] recorded shader copies written to {}", table);
  } else {
    REXLOG_WARN("[shaders] cannot write {}", table);
  }
}

}  // namespace mgrr::shaders

namespace {

using namespace mgrr::shaders;

// Looks the container up before the original runs (it may reuse the memory afterwards); records the copy after.
struct Created {
  const rex::x360::EntradaShader* entry = nullptr;
  uint32_t physical_size = 0;
};

Created Lookup(uint8_t* base, uint32_t container, bool first_hook) {
  Shaders& shaders = Library();
  if (!shaders.loaded() || !container) return {};
  const uint32_t virtual_size = REX_LOAD_U32(container + 4);
  const uint32_t physical_size = REX_LOAD_U32(container + 8);
  const uint64_t size = uint64_t(virtual_size) + physical_size;
  if (size < 36 || size > 256 * 1024) return {};
  const rex::x360::EntradaShader* e =
      shaders.FindContainer(std::span<const uint8_t>(REX_RAW_ADDR(container), size_t(size)));
  std::lock_guard<std::mutex> guard(g_report_lock);
  if (!e && g_missing_lines < 32 && g_missing_logged.insert(uint32_t(size)).second) {
    ++g_missing_lines;
    REXLOG_WARN("[shaders] CreateXShader ({} hook): container of {} bytes at {:08X} not in the library",
                first_hook ? "first" : "second", size, container);
  }
  return {e, physical_size};
}

void Record(uint8_t* base, uint32_t object, const Created& created) {
  if (!created.entry || !object) return;
  // The microcode copy: +24 in a pixel shader object, +32 in a vertex shader one (+24 is 0 there; dumped on PC).
  const uint32_t copy = REX_LOAD_U32(object + (created.entry->vertices ? 32 : 24));
  if (!copy) return;
  if (g_recorded < 8) {
    REXLOG_INFO("[shaders] copy {}: object {:08X}, copy {:08X} -> physical {:08X}, {} bytes, {} shader, {} "
                "microcode words",
                g_recorded, object, copy, Physical(copy), created.physical_size,
                created.entry->vertices ? "vertex" : "pixel", created.entry->microcodigo.size());
  }
  {
    std::unique_lock<std::shared_mutex> guard(g_copies_lock);
    g_copies[Physical(copy)] = {created.physical_size, created.entry};
  }
  std::lock_guard<std::mutex> guard(g_report_lock);
  ++g_recorded;
  const auto now = std::chrono::steady_clock::now();
  const Stats s = Library().stats();
  if (now - g_last_report >= std::chrono::seconds(10) || s.containers_seen % 250 == 0) {
    g_last_report = now;
    REXLOG_INFO("[shaders] CreateXShader: {} containers, {} in the library, {} microcode copies recorded",
                s.containers_seen, s.containers_found, g_recorded);
  }
}

}  // namespace

REX_HOOK_RAW(sub_826ACC10) {
  const Created created = Lookup(base, ctx.r3.u32, true);
  __imp__sub_826ACC10(ctx, base);
  Record(base, ctx.r3.u32, created);
}

REX_HOOK_RAW(sub_826ACDF8) {
  const Created created = Lookup(base, ctx.r3.u32, false);
  __imp__sub_826ACDF8(ctx, base);
  Record(base, ctx.r3.u32, created);
}
