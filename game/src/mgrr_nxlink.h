// Switch only: the log goes to the PC too when the NRO was sent with nxlink (tools/nxlink.sh).

#pragma once

namespace mgrr {

// After the SDK set up its log; does nothing unless started through nxlink.
void StartNxlinkLog();

}  // namespace mgrr
