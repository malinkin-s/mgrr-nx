// See mgrr_overlay.h.

#include "mgrr_overlay.h"

#include <mutex>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

#include "mgrr_stats.h"

#if MGRR_DEKO3D
#define MGRR_OVERLAY_DEFAULT true  // Switch: the screen is the quickest way to see what the build does
#else
#define MGRR_OVERLAY_DEFAULT false
#endif
REXCVAR_DEFINE_BOOL(mgrr_overlay, MGRR_OVERLAY_DEFAULT, "MGRR", "Show the FPS and draw overlay");

namespace mgrr::stats {
namespace {
std::mutex g_gpu_lock;
Gpu g_gpu;
}  // namespace

void PublishGpu(const Gpu& gpu) {
  std::lock_guard<std::mutex> guard(g_gpu_lock);
  g_gpu = gpu;
  g_gpu.valid = true;
}

Gpu CurrentGpu() {
  std::lock_guard<std::mutex> guard(g_gpu_lock);
  return g_gpu;
}

}  // namespace mgrr::stats

namespace mgrr {
namespace {

class Overlay final : public rex::ui::ImGuiDialog {
 public:
  explicit Overlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    // Top right, out of the way of the SDK's overlay (top left).
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.0f, 10.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.55f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("mgrr##overlay", nullptr, kFlags)) {
      ImGui::SetWindowFontScale(1.3f);  // readable on the handheld screen
      const stats::Frames f = stats::CurrentFrames();
      ImGui::Text("%.1f FPS  %.1f ms (max %.1f)", f.fps, f.frame_ms, f.frame_ms_max);
      ImGui::Text("draws/frame %.0f  resolves %.1f", f.draws_per_frame, f.resolves_per_frame);
      const stats::Gpu g = stats::CurrentGpu();
      if (g.valid) {
        ImGui::Separator();
        ImGui::Text("drawn %.0f/s  rejected %.0f/s  no shader %.0f/s", g.drawn_per_s, g.rejected_per_s,
                    g.without_shaders_per_s);
        if (!g.top_causes.empty()) ImGui::Text("rejects:%s", g.top_causes.c_str());
        ImGui::Text("VS addr %llu  ucode %llu  unknown %llu", (unsigned long long)g.vs_by_address,
                    (unsigned long long)g.vs_by_microcode, (unsigned long long)g.vs_unknown);
        ImGui::Text("PS loads %llu  unknown %llu", (unsigned long long)g.ps_loads,
                    (unsigned long long)g.ps_unknown);
      }
    }
    ImGui::End();
  }
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  if (!REXCVAR_GET(mgrr_overlay) || !drawer) return nullptr;
  return std::make_unique<Overlay>(drawer);
}

}  // namespace mgrr
