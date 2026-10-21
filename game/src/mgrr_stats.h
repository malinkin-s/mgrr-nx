// Live numbers for the on-screen overlay (mgrr_overlay.cpp). Producers publish, the UI thread reads.

#pragma once

#include <cstdint>
#include <string>

namespace mgrr::stats {

// From the game's own Swap and draw calls (mgrr_draw_stats.cpp), over the last second.
struct Frames {
  double fps = 0;
  double frame_ms = 0;        // mean time between Swaps
  double frame_ms_max = 0;    // longest one
  double draws_per_frame = 0;
  double resolves_per_frame = 0;
  uint64_t total_frames = 0;
};
Frames CurrentFrames();

// From the ring reader and the deko3d renderer (mgrr_gpu.cpp, Switch only), per second.
struct Gpu {
  bool valid = false;
  double drawn_per_s = 0;
  double rejected_per_s = 0;
  double without_shaders_per_s = 0;
  std::string top_causes;  // "cause x count" of the most frequent rejections since start
  uint64_t vs_by_address = 0, vs_by_microcode = 0, vs_unknown = 0, ps_loads = 0, ps_unknown = 0;
};
void PublishGpu(const Gpu& gpu);
Gpu CurrentGpu();

}  // namespace mgrr::stats
