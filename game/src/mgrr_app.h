// mgrr - Metal Gear Rising: Revengeance (Xbox 360), recompiled with ReXGlue.
//
// Game-specific hooks go here as overrides of rex::ReXApp.

#pragma once

#include <memory>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

#include "mgrr_overlay.h"

#if MGRR_DEKO3D
#include "mgrr_gpu.h"
#include "mgrr_nxlink.h"
#include "mgrr_replay.h"
#endif

class MgrrApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<MgrrApp>(new MgrrApp(ctx, "mgrr", PPCImageConfig));
  }

#if MGRR_DEKO3D
  // The deko3d build has no Xenos emulation plugin: the graphics system is the game's own (mgrr_gpu.cpp).
  // Without one the game stops at VdInitializeRingBuffer, waiting for a GPU that never reads its ring.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics) {
      config.graphics = mgrr::gpu::CreateGraphicsSystem();
    }
    graphics_ = config.graphics.get();
  }

  // --mgrr_replay: the window and the deko3d presenter are up, and instead of the game a GPU recording is replayed
  // (mgrr_replay.h). The runtime is never constructed, so no guest memory either.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (!mgrr::replay::Enabled()) return rex::ReXApp::OnFinalizePaths(defaults, std::move(resume));
    mgrr::replay::Start(graphics_, &app_context());
    return std::nullopt;
  }
#endif

#if MGRR_DEKO3D
  // Sent with nxlink (tools/nxlink.sh): the log goes to the PC too.
  void OnPostInitLogging() override { mgrr::StartNxlinkLog(); }
#endif

  // The FPS and draw overlay (mgrr_overlay.h), on the UI thread with the SDK's own dialogs.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    overlay_ = mgrr::CreateOverlay(drawer);
#if !MGRR_DEKO3D
    // PC: F9 (--mgrr_trace_key) records the next frames for the graphics test bench (with --gpu_trace_path; sdk
    // gpu_trace.h).
    rex::ui::RegisterBind("bind_gpu_trace", rex::cvar::GetFlagByName("mgrr_trace_key"),
                          "Record the next frames (gpu_trace_path)",
                          [] {
                            if (rex::cvar::GetFlagByName("gpu_trace_path").empty()) {
                              REXLOG_WARN("[gpu_trace] key pressed, but no --gpu_trace_path: nothing recorded");
                              return;
                            }
                            rex::cvar::SetFlagByName("gpu_trace_now", "true");
                            REXLOG_INFO("[gpu_trace] key pressed: recording the next frames");
                          });
#endif
  }
  void OnShutdown() override { overlay_.reset(); }

  // Without --game_data_root the disc contents are expected in game/ next to the executable
  // (on Switch: sdmc:/switch/mgrr/game/default.xex, media/...).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty())
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
  }

 private:
  std::unique_ptr<rex::ui::ImGuiDialog> overlay_;
#if MGRR_DEKO3D
  rex::system::IGraphicsSystem* graphics_ = nullptr;
#endif
};
