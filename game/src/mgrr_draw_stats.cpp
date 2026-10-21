// Draw-call statistics per frame (S1 input: how many draws MGR issues, for the 60 fps budget).
//
// Counts calls of the game's D3D draw functions between two Swaps and logs, every
// --mgrr_draw_stats_seconds, the frames/s and draws per frame (mean, p50, p95, max) of that window,
// plus resolves per frame. Addresses: x360_work/DRAW_AND_RESOLVE.md.
//   0x826BC928 DrawVertices         (dev, primType, startVertex, vertexCount)
//   0x826BCD40 DrawIndexedVertices  (dev, primType, baseVertex, startIndex, indexCount)
//   0x826BB0D0 Resolve wrapper
//   0x826B3510 Swap (calls VdSwap)

#include "generated/default/mgrr_init.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#if MGRR_DEKO3D
#define MGRR_DRAW_STATS_DEFAULT true   // Switch test builds: the log is the only output for now
#else
#define MGRR_DRAW_STATS_DEFAULT false
#endif
REXCVAR_DEFINE_BOOL(mgrr_draw_stats, MGRR_DRAW_STATS_DEFAULT, "MGRR", "Log draw calls per frame (mean/p50/p95/max) periodically");
REXCVAR_DEFINE_INT32(mgrr_draw_stats_seconds, 10, "MGRR", "Window of the draw statistics, seconds");

namespace {

std::atomic<uint32_t> g_draws{0}, g_indexed{0}, g_prims{0}, g_resolves{0};

struct Window {
  std::mutex lock;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::vector<uint32_t> draws;      // per frame
  uint64_t indexed = 0, prims = 0, resolves = 0;
  uint64_t total_frames = 0;
  uint32_t max_ever = 0;
} g_win;

// Primitives of a draw from the element count (DRAW_AND_RESOLVE.md, table at 0x8218C968).
uint32_t Primitives(uint32_t type, uint32_t count) {
  switch (type) {
    case 1: return count;                          // point list
    case 2: return count / 2;                      // line list
    case 3: return count ? count - 1 : 0;          // line strip
    case 4: case 8: return count / 3;              // triangle list, rect list
    case 5: case 6: return count > 2 ? count - 2 : 0;  // fan, strip
    case 13: return count / 4;                     // quad list
    default: return 0;
  }
}

uint32_t Percentile(std::vector<uint32_t>& v, double p) {
  size_t i = size_t(p * double(v.size() - 1) + 0.5);
  std::nth_element(v.begin(), v.begin() + i, v.end());
  return v[i];
}

void EndFrame() {
  const uint32_t draws = g_draws.exchange(0, std::memory_order_relaxed);
  const uint32_t indexed = g_indexed.exchange(0, std::memory_order_relaxed);
  const uint32_t prims = g_prims.exchange(0, std::memory_order_relaxed);
  const uint32_t resolves = g_resolves.exchange(0, std::memory_order_relaxed);
  std::lock_guard<std::mutex> guard(g_win.lock);
  g_win.draws.push_back(draws);
  g_win.indexed += indexed;
  g_win.prims += prims;
  g_win.resolves += resolves;
  g_win.total_frames++;
  g_win.max_ever = std::max(g_win.max_ever, draws);
  const auto now = std::chrono::steady_clock::now();
  const double secs = std::chrono::duration<double>(now - g_win.start).count();
  if (secs < double(std::max(1, REXCVAR_GET(mgrr_draw_stats_seconds))))
    return;
  auto& v = g_win.draws;
  const size_t n = v.size();
  uint64_t sum = 0;
  for (uint32_t d : v) sum += d;
  const uint32_t p50 = Percentile(v, 0.50), p95 = Percentile(v, 0.95);
  const uint32_t mx = *std::max_element(v.begin(), v.end());
  REXLOG_INFO(
      "[draws] {:.1f} s: {} frames ({:.1f}/s) | draws/frame mean {:.0f} p50 {} p95 {} max {} | indexed {:.0f}% | "
      "primitives/frame {:.0f} | resolves/frame {:.1f} | total frames {} max ever {}",
      secs, n, double(n) / secs, double(sum) / double(n), p50, p95, mx,
      sum ? 100.0 * double(g_win.indexed) / double(sum) : 0.0, double(g_win.prims) / double(n),
      double(g_win.resolves) / double(n), g_win.total_frames, g_win.max_ever);
  v.clear();
  g_win.indexed = g_win.prims = g_win.resolves = 0;
  g_win.start = now;
}

}  // namespace

REX_HOOK_RAW(sub_826BC928) {
  if (REXCVAR_GET(mgrr_draw_stats)) {
    g_draws.fetch_add(1, std::memory_order_relaxed);
    g_prims.fetch_add(Primitives(ctx.r4.u32, ctx.r6.u32), std::memory_order_relaxed);
  }
  __imp__sub_826BC928(ctx, base);
}

REX_HOOK_RAW(sub_826BCD40) {
  if (REXCVAR_GET(mgrr_draw_stats)) {
    g_draws.fetch_add(1, std::memory_order_relaxed);
    g_indexed.fetch_add(1, std::memory_order_relaxed);
    g_prims.fetch_add(Primitives(ctx.r4.u32, ctx.r7.u32), std::memory_order_relaxed);
  }
  __imp__sub_826BCD40(ctx, base);
}

REX_HOOK_RAW(sub_826BB0D0) {
  if (REXCVAR_GET(mgrr_draw_stats))
    g_resolves.fetch_add(1, std::memory_order_relaxed);
  __imp__sub_826BB0D0(ctx, base);
}

REX_HOOK_RAW(sub_826B3510) {
  if (REXCVAR_GET(mgrr_draw_stats))
    EndFrame();
  __imp__sub_826B3510(ctx, base);
}
