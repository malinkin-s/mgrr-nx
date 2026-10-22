// mgrr - replay of a GPU recording (rex/graphics/gpu_trace.h, made by the PC build) through the Switch renderer.
//
// With --mgrr_replay=FILE.gtr the NRO does not start the game: the recorded frames go through the same PM4 decoder
// (mgrr_pm4.h) and deko3d renderer as in the game, and each presented frame is saved as a PNG with a report of what
// the renderer drew and rejected. No guest runs, so it works where the game cannot (Ryujinx) as well as on the
// console, which stays the reference.

#pragma once

namespace rex::system {
class IGraphicsSystem;
}
namespace rex::ui {
class WindowedAppContext;
}

namespace mgrr::replay {

// --mgrr_replay is set.
bool Enabled();

// Replays on a thread of its own with the graphics system's provider and presenter, then quits the app.
void Start(rex::system::IGraphicsSystem* graphics, rex::ui::WindowedAppContext* app_context);

}  // namespace mgrr::replay
