// mgrr - Metal Gear Rising: Revengeance (Xbox 360), recompiled with ReXGlue.
//
// Game-specific hooks go here as overrides of rex::ReXApp.

#pragma once

#include <memory>

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>

#include "mgrr_overlay.h"

#if MGRR_DEKO3D
#include "mgrr_gpu.h"
#include "mgrr_nxlink.h"
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
  }
#endif

#if MGRR_DEKO3D
  // Sent with nxlink (tools/nxlink.sh): the log goes to the PC too.
  void OnPostInitLogging() override { mgrr::StartNxlinkLog(); }
#endif

  // The FPS and draw overlay (mgrr_overlay.h), on the UI thread with the SDK's own dialogs.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override { overlay_ = mgrr::CreateOverlay(drawer); }
  void OnShutdown() override { overlay_.reset(); }

  // Without --game_data_root the disc contents are expected in game/ next to the executable
  // (on Switch: sdmc:/switch/mgrr/game/default.xex, media/...).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty())
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
  }

 private:
  std::unique_ptr<rex::ui::ImGuiDialog> overlay_;
};
