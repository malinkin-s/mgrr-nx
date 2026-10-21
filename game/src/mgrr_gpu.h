// mgrr - the game's graphics system on the Switch (see mgrr_gpu.cpp).

#pragma once

#include <memory>

#include <rex/system/interfaces/graphics.h>

namespace mgrr::gpu {

// Stands in for the Xbox 360 GPU: answers what the game's Direct3D waits for, and reads its command ring.
std::unique_ptr<rex::system::IGraphicsSystem> CreateGraphicsSystem();

}  // namespace mgrr::gpu
