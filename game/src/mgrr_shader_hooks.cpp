// The game's shader creation, matched against the shader library (mgrr_shaders.h).
//
// CreateXShader (0x826ACC10 and 0x826ACDF8, one per shader type; x360_work/FETCH_AND_SHADERS.md) receives the
// whole compiled container in r3: [virtual size at +4][physical size at +8], magic 0x102A1101 vertex,
// 0x102A1100 pixel. It returns the D3D shader object, which keeps a copy of the microcode; the ring later loads
// that copy (IM_LOAD), patched by D3D for vertex shaders. Looking the container up here is how a vertex shader is
// identified; the ring reader (M2) will map its IM_LOAD address back to the entry through the object.
//
// For now this checks coverage: every shader the game creates is looked up, and the counts are logged with the
// [draws] statistics' period. --mgrr_shader_library (default: mgrr_shaders.mgsl next to the executable), built by
// tools/shaders/build_library.sh.

#include "generated/default/mgrr_init.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
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

namespace {

mgrr::shaders::Shaders g_shaders;
std::once_flag g_load_once;
bool g_loaded = false;

std::mutex g_report_lock;
std::unordered_set<uint32_t> g_missing_logged;  // container sizes already reported, to keep the log short
uint32_t g_missing_lines = 0;
std::chrono::steady_clock::time_point g_last_report = std::chrono::steady_clock::now();

void LoadOnce() {
  std::call_once(g_load_once, [] {
    std::string path = REXCVAR_GET(mgrr_shader_library);
    if (path.empty()) path = (rex::filesystem::GetExecutableFolder() / "mgrr_shaders.mgsl").string();
    g_loaded = g_shaders.Load(path);
  });
}

void Check(uint8_t* base, uint32_t container, bool vertex_hook) {
  LoadOnce();
  if (!g_loaded || !container) return;
  const uint32_t virtual_size = REX_LOAD_U32(container + 4);
  const uint32_t physical_size = REX_LOAD_U32(container + 8);
  const uint64_t size = uint64_t(virtual_size) + physical_size;
  if (size < 36 || size > 256 * 1024) return;
  const std::span<const uint8_t> bytes(REX_RAW_ADDR(container), size_t(size));
  const rex::x360::EntradaShader* e = g_shaders.FindContainer(bytes);

  std::lock_guard<std::mutex> guard(g_report_lock);
  if (!e && g_missing_lines < 32 && g_missing_logged.insert(uint32_t(size)).second) {
    ++g_missing_lines;
    REXLOG_WARN("[shaders] CreateXShader ({} hook): container of {} bytes at {:08X} not in the library",
                vertex_hook ? "first" : "second", size, container);
  }
  const auto now = std::chrono::steady_clock::now();
  const mgrr::shaders::Stats now_stats = g_shaders.stats();
  if (now - g_last_report >= std::chrono::seconds(10) || now_stats.containers_seen % 250 == 0) {
    g_last_report = now;
    const mgrr::shaders::Stats s = g_shaders.stats();
    REXLOG_INFO("[shaders] CreateXShader: {} containers, {} in the library", s.containers_seen, s.containers_found);
  }
}

}  // namespace

REX_HOOK_RAW(sub_826ACC10) {
  Check(base, ctx.r3.u32, true);
  __imp__sub_826ACC10(ctx, base);
}

REX_HOOK_RAW(sub_826ACDF8) {
  Check(base, ctx.r3.u32, false);
  __imp__sub_826ACDF8(ctx, base);
}
