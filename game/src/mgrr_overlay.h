// mgrr's on-screen debug overlay: FPS, draws, and what the renderer does with them (mgrr_stats.h).
// Shown from start when --mgrr_overlay is on (default on Switch); the SDK's own overlay stays on L+R+Up.

#pragma once

#include <memory>

namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
}  // namespace rex::ui

namespace mgrr {

// nullptr if --mgrr_overlay is off.
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer);

}  // namespace mgrr
