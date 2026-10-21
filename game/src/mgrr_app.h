// mgrr - Metal Gear Rising: Revengeance (Xbox 360), recompiled with ReXGlue.
//
// Game-specific hooks go here as overrides of rex::ReXApp.

#pragma once

#include <rex/filesystem.h>
#include <rex/rex_app.h>

#if MGRR_DEKO3D
#include "mgrr_gpu.h"
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

  // Without --game_data_root the disc contents are expected in game/ next to the executable
  // (on Switch: sdmc:/switch/mgrr/game/default.xex, media/...).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty())
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
  }
};
