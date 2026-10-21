// mgrr - Metal Gear Rising: Revengeance (Xbox 360), recompiled with ReXGlue.
//
// Game-specific hooks go here as overrides of rex::ReXApp.

#pragma once

#include <rex/filesystem.h>
#include <rex/rex_app.h>

class MgrrApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<MgrrApp>(new MgrrApp(ctx, "mgrr", PPCImageConfig));
  }

  // Without --game_data_root the disc contents are expected in game/ next to the executable
  // (on Switch: sdmc:/switch/mgrr/game/default.xex, media/...).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty())
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
  }
};
