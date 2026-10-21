// mgrr - Metal Gear Rising: Revengeance (Xbox 360), recompiled with ReXGlue.

#include "generated/default/mgrr_init.h"

#include "mgrr_app.h"

REX_DEFINE_APP(mgrr, MgrrApp::Create)

#if MGRR_DEKO3D
// Where the NRO is when the loader does not say (Ryujinx passes no argv; tools/emu_test.sh lays out its SD card so).
const char* RexSwitchDefaultExecutablePath() {
  return "sdmc:/switch/mgrr/mgrr.nro";
}
#endif
