// Adapted from xbox360-nx (https://github.com/nx-mod/xbox360-nx, GPL-3) halo3/src/halo3_gpu.cpp @ 6575330, split
// out of mgrr_gpu.cpp so a recording can be replayed through it (mgrr_pm4.h).
//
// GPU writes to memory (MEM_WRITE, COND_WRITE, REG_TO_MEM, EVENT_WRITE_*, scratch registers) are as in the SDK's
// graphics/command_processor.cpp; occlusion queries report 1000 samples, like the SDK. Each DRAW_INDX goes to the
// SDK's deko3d renderer (rex::x360::RenderDk) as a PeticionDibujo: the register mirror, the shaders of the last
// IM_LOADs found in MGR's shader library (pixel shaders by microcode, vertex shaders by the address the
// CreateXShader hook recorded: mgrr_shader_hooks.h) and generations of the registers that changed, as xbox360-nx's
// NFS ring reader does (nfsmw_pal/src/nfsmw_nativo_sistema.cpp). Copy-mode draws are resolves and clears.

#include "mgrr_pm4.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <iterator>
#include <string>

#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>
#include <rex/thread.h>
#include <rex/types.h>
#include <rex/x360/deko/destinos.h>
#include <rex/x360/decodificador.h>

#include "mgrr_shader_hooks.h"
#include "mgrr_shaders.h"

namespace mgrr::pm4 {
namespace {

namespace xenos = rex::graphics::xenos;
using Clock = std::chrono::steady_clock;

constexpr int kMaxIndirectDepth = 4;
// The real GPU waits as long as it takes; without one some conditions are never met, so the wait is cut
// short and the ring goes on.
constexpr auto kWaitRegMemMax = std::chrono::milliseconds(200);
constexpr uint16_t kMaxExtent = 2048 >> 3;  // xenos::kTexture2DCubeMaxWidthHeight >> 3
constexpr int32_t kFakeSampleCount = 1000;  // the SDK's query_occlusion_fake_sample_count

// Register generations (NFS's generacion_*): the renderer redoes per-draw work only when its inputs changed.
constexpr uint32_t kRegFetchFirst = 0x4800;  // SHADER_CONSTANT_FETCH_00_0
constexpr uint32_t kRegFetchLast = 0x48BF;   // SHADER_CONSTANT_FETCH_31_5
// Viewport state: PA_SC_WINDOW_OFFSET .. SCISSOR_BR, PA_CL_VPORT_*, PA_CL_CLIP_CNTL, PA_SU_SC_MODE_CNTL,
// PA_CL_VTE_CNTL, PA_SU_VTX_CNTL. Not VGT_DRAW_INITIATOR: it changes on every draw.
bool IsViewportRegister(uint32_t index) {
  if (index - 0x2080 > 0x2302 - 0x2080) return false;
  return index <= 0x2082 || (index >= 0x210F && index <= 0x2114) || (index >= 0x2204 && index <= 0x2206) ||
         index == 0x2302;
}

bool Compare(uint32_t info, uint32_t value, uint32_t reference) {
  switch (info & 0x7) {
    case 0x0: return false;
    case 0x1: return value < reference;
    case 0x2: return value <= reference;
    case 0x3: return value == reference;
    case 0x4: return value != reference;
    case 0x5: return value >= reference;
    case 0x6: return value > reference;
    default: return true;
  }
}

// Register base of each SET_CONSTANT / LOAD_ALU_CONSTANT type, or UINT32_MAX.
uint32_t ConstantBase(uint32_t type_index) {
  switch ((type_index >> 16) & 0xFF) {
    case 0: return 0x4000;  // ALU
    case 1: return 0x4800;  // fetch
    case 2: return 0x4900;  // bool
    case 3: return 0x4908;  // loop
    case 4: return 0x2000;  // registers
    default: return UINT32_MAX;
  }
}

}  // namespace

uint32_t Reader::Peek() const { return rex::memory::load_and_swap<uint32_t>(base + size_t(pos) * 4); }

Decoder::Decoder(rex::memory::Memory* memory, Host& host)
    : memory_(memory), host_(host), registers_(kRegisterCount, 0) {}

void Decoder::LoadRegisters(const uint32_t* values, uint32_t count) {
  std::memcpy(registers_.data(), values, size_t(std::min(count, kRegisterCount)) * 4);
  Invalidate();
}

void Decoder::Invalidate() {
  ++constants_vs_generation_;
  ++constants_ps_generation_;
  ++fetch_generation_;
  ++viewport_generation_;
}

// Register writes from the ring, with the side effects the game relies on.
void Decoder::WriteRegister(uint32_t index, uint32_t value) {
  if (index >= kRegisterCount) {
    return;
  }
  if (index == rex::graphics::XE_GPU_REG_COHER_STATUS_HOST) {
    value |= UINT32_C(0x80000000);
  }
  if (registers_[index] != value) {
    if (index >= 0x4000 && index < 0x4800) {
      ++(index < 0x4400 ? constants_vs_generation_ : constants_ps_generation_);
    } else if (index >= kRegFetchFirst && index <= kRegFetchLast) {
      ++fetch_generation_;
    } else if (IsViewportRegister(index)) {
      ++viewport_generation_;
    }
  }
  registers_[index] = value;
  // Scratch registers are mirrored to memory (SCRATCH_ADDR) for the ones SCRATCH_UMSK selects: the game
  // waits on those values to know where the GPU is.
  if (index >= rex::graphics::XE_GPU_REG_SCRATCH_REG0 && index <= rex::graphics::XE_GPU_REG_SCRATCH_REG7) {
    const uint32_t n = index - rex::graphics::XE_GPU_REG_SCRATCH_REG0;
    if ((UINT32_C(1) << n) & registers_[rex::graphics::XE_GPU_REG_SCRATCH_UMSK]) {
      const uint32_t address = registers_[rex::graphics::XE_GPU_REG_SCRATCH_ADDR] + n * 4;
      rex::memory::store_and_swap<uint32_t>(memory_->TranslatePhysical(address), value);
    }
  }
}

// GPU memory: the two low bits of the address are the byte order.
uint32_t Decoder::ReadMemory(uint32_t address) const {
  uint32_t value;
  std::memcpy(&value, memory_->TranslatePhysical(address & ~uint32_t(0x3)), sizeof(value));
  return xenos::GpuSwap(value, static_cast<xenos::Endian>(address & 0x3));
}

void Decoder::WriteMemory(uint32_t address, uint32_t value) {
  const uint32_t swapped = xenos::GpuSwap(value, static_cast<xenos::Endian>(address & 0x3));
  std::memcpy(memory_->TranslatePhysical(address & ~uint32_t(0x3)), &swapped, sizeof(swapped));
}

bool Decoder::Packet(Reader& reader, int depth) {
  const uint32_t packet = reader.Peek();
  uint32_t count = 0;
  switch (packet >> 30) {
    case 0: count = packet ? ((packet >> 16) & 0x3FFF) + 1 : 0; break;
    case 1: count = 2; break;
    case 2: count = 0; break;
    default: count = ((packet >> 16) & 0x3FFF) + 1; break;
  }
  if (reader.Pending() < count + 1) {
    return false;
  }
  reader.Skip(1);
  ++stats_.packets;
  Reader data = reader;
  reader.Skip(count);
  if (!packet) {
    return true;
  }
  switch (packet >> 30) {
    case 0: {
      const uint32_t index = packet & 0x7FFF;
      const bool one_register = (packet >> 15) & 0x1;
      for (uint32_t i = 0; i < count; ++i) {
        WriteRegister(one_register ? index : index + i, data.Read());
      }
      break;
    }
    case 1: {
      const uint32_t value1 = data.Read();
      const uint32_t value2 = data.Read();
      WriteRegister(packet & 0x7FF, value1);
      WriteRegister((packet >> 11) & 0x7FF, value2);
      break;
    }
    case 2:
      break;
    default:
      PacketType3(data, packet, count, depth);
      break;
  }
  return true;
}

void Decoder::PacketType3(Reader& data, uint32_t packet, uint32_t count, int depth) {
  const uint32_t opcode = (packet >> 8) & 0x7F;
  if ((packet & 0x1) && (!(bin_select_ & bin_mask_) || opcode == xenos::PM4_XE_SWAP)) {
    return;  // predicate fails; predicated Swaps are always dropped
  }
  switch (opcode) {
    case xenos::PM4_INTERRUPT: {
      if (count < 1) break;
      const uint32_t cpus = data.Read();
      for (uint32_t cpu = 0; cpu < 6; ++cpu) {
        if (cpus & (1u << cpu)) {
          host_.Interrupt(1, cpu);
        }
      }
      break;
    }
    case xenos::PM4_XE_SWAP: {
      // VdSwap writes "SWAP", the frontbuffer's physical address and its size, and the frontbuffer descriptor
      // into fetch constant 0 just before: the renderer presents the resolved texture it names.
      uint32_t width = 1280, height = 720;
      if (count >= 4) {
        data.Read();
        data.Read();
        width = data.Read();
        height = data.Read();
      }
      ++stats_.swaps;
      host_.Swap(width, height);
      break;
    }
    case xenos::PM4_INDIRECT_BUFFER:
    case xenos::PM4_INDIRECT_BUFFER_PFD: {
      if (count < 2) break;
      const uint32_t address = data.Read() & 0x1FFFFFFF;
      const uint32_t length = data.Read() & 0xFFFFF;
      if (depth < kMaxIndirectDepth) {
        Reader indirect;
        indirect.base = memory_->TranslatePhysical(address);
        indirect.end = length;
        while (indirect.Pending() && Packet(indirect, depth + 1)) {
        }
      }
      break;
    }
    case xenos::PM4_WAIT_REG_MEM: {
      if (count < 5) break;
      // The game waits for the GPU here: afterwards it may rewrite vertex memory already drawn from, so the
      // renderer's upload dedupe forgets what it saw.
      rex::x360::g_sincronizaciones_anillo.fetch_add(1, std::memory_order_relaxed);
      if (!host_.WaitsForGame()) break;
      const uint32_t info = data.Read();
      const uint32_t poll = data.Read();
      const uint32_t reference = data.Read();
      const uint32_t mask = data.Read();
      const auto limit = Clock::now() + kWaitRegMemMax;
      for (;;) {
        if (!(info & 0x10) && poll == rex::graphics::XE_GPU_REG_COHER_STATUS_HOST &&
            (Register(poll) & UINT32_C(0x80000000))) {
          registers_[poll] = 0;  // MakeCoherent: there is no GPU cache to flush
        }
        const uint32_t value = (info & 0x10) ? ReadMemory(poll) : Register(poll);
        if (Compare(info, value & mask, reference)) {
          break;
        }
        if (Clock::now() >= limit || !host_.Active()) {
          if (stats_.waits_cut_short++ == 0) {
            REXLOG_WARN("[gpu] WAIT_REG_MEM not met ({} {:08X} ref {:08X} mask {:08X}), going on",
                        (info & 0x10) ? "memory" : "register", poll, reference, mask);
          }
          break;
        }
        rex::thread::Sleep(std::chrono::microseconds(100));
      }
      break;
    }
    case xenos::PM4_REG_RMW: {
      if (count < 3) break;
      const uint32_t info = data.Read();
      const uint32_t and_value = data.Read();
      const uint32_t or_value = data.Read();
      uint32_t value = Register(info & 0x1FFF);
      value &= ((info >> 31) & 0x1) ? Register(and_value & 0x1FFF) : and_value;
      value |= ((info >> 30) & 0x1) ? Register(or_value & 0x1FFF) : or_value;
      WriteRegister(info & 0x1FFF, value);
      break;
    }
    case xenos::PM4_REG_TO_MEM: {
      if (count < 2) break;
      const uint32_t reg = data.Read();
      const uint32_t address = data.Read();
      WriteMemory(address, Register(reg));
      break;
    }
    case xenos::PM4_MEM_WRITE: {
      if (count < 1) break;
      uint32_t address = data.Read();
      for (uint32_t i = 1; i < count; ++i) {
        WriteMemory(address, data.Read());
        address += 4;
      }
      break;
    }
    case xenos::PM4_COND_WRITE: {
      if (count < 6) break;
      const uint32_t info = data.Read();
      const uint32_t poll = data.Read();
      const uint32_t reference = data.Read();
      const uint32_t mask = data.Read();
      const uint32_t target = data.Read();
      const uint32_t value_to_write = data.Read();
      const uint32_t value = (info & 0x10) ? ReadMemory(poll) : Register(poll);
      if (Compare(info, value & mask, reference)) {
        if (info & 0x100) {
          WriteMemory(target, value_to_write);
        } else {
          WriteRegister(target, value_to_write);
        }
      }
      break;
    }
    case xenos::PM4_EVENT_WRITE:
      if (count < 1) break;
      WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, data.Read() & 0x3F);
      break;
    case xenos::PM4_EVENT_WRITE_SHD: {
      if (count < 3) break;
      const uint32_t initiator = data.Read();
      const uint32_t address = data.Read();
      const uint32_t value = data.Read();
      WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
      WriteMemory(address, ((initiator >> 31) & 0x1) ? host_.Counter() : value);
      break;
    }
    case xenos::PM4_EVENT_WRITE_EXT: {
      if (count < 2) break;
      const uint32_t initiator = data.Read();
      const uint32_t address = data.Read();
      WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
      // Screen extent of the previous draw: the maximum, as the SDK does.
      const uint16_t extent[] = {0, kMaxExtent, 0, kMaxExtent, 0, 1};
      uint8_t* target = memory_->TranslatePhysical(address & ~uint32_t(0x3));
      for (size_t i = 0; i < std::size(extent); ++i) {
        rex::memory::store_and_swap<uint16_t>(target + i * 2, extent[i]);
      }
      break;
    }
    case xenos::PM4_EVENT_WRITE_ZPD: {
      if (count < 1) break;
      WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, data.Read() & 0x3F);
      OcclusionQuery(Register(rex::graphics::XE_GPU_REG_RB_SAMPLE_COUNT_ADDR));
      break;
    }
    case xenos::PM4_SET_CONSTANT: {
      if (count < 1) break;
      const uint32_t type_index = data.Read();
      const uint32_t base = ConstantBase(type_index);
      if (base != UINT32_MAX) {
        const uint32_t index = base + (type_index & 0x7FF);
        for (uint32_t i = 1; i < count; ++i) {
          WriteRegister(index + i - 1, data.Read());
        }
      }
      break;
    }
    case xenos::PM4_SET_CONSTANT2:
    case xenos::PM4_SET_SHADER_CONSTANTS: {
      // First word: the register index; the rest is written consecutively.
      if (count < 1) break;
      const uint32_t index = data.Read() & 0xFFFF;
      for (uint32_t i = 1; i < count; ++i) {
        WriteRegister(index + i - 1, data.Read());
      }
      break;
    }
    case xenos::PM4_LOAD_ALU_CONSTANT: {
      // Registers loaded from guest memory instead of from the ring.
      if (count < 3) break;
      const uint32_t address = data.Read() & 0x3FFFFFFF;
      const uint32_t type_index = data.Read();
      const uint32_t n = data.Read() & 0xFFF;
      const uint32_t base = ConstantBase(type_index);
      if (base != UINT32_MAX) {
        const uint8_t* source = memory_->TranslatePhysical(address);
        const uint32_t index = base + (type_index & 0x7FF);
        for (uint32_t i = 0; i < n; ++i) {
          WriteRegister(index + i, rex::memory::load_and_swap<uint32_t>(source + size_t(i) * 4));
        }
      }
      break;
    }
    case xenos::PM4_VIZ_QUERY: {
      // When a query ends it is reported as visible, in case the game reads the result (as the SDK does).
      if (count < 1) break;
      const uint32_t word = data.Read();
      const uint32_t id = word & 0x3F;
      if (!(word & 0x100)) {
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_START);
      } else {
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_END);
        const uint32_t reg = id < 32 ? uint32_t(rex::graphics::XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0)
                                     : uint32_t(rex::graphics::XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1);
        registers_[reg] |= UINT32_C(1) << (id < 32 ? id : id - 32);
      }
      break;
    }
    case xenos::PM4_SET_BIN_MASK_LO:
      if (count < 1) break;
      bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | data.Read();
      break;
    case xenos::PM4_SET_BIN_MASK_HI:
      if (count < 1) break;
      bin_mask_ = (bin_mask_ & 0xFFFFFFFFull) | (uint64_t(data.Read()) << 32);
      break;
    case xenos::PM4_SET_BIN_SELECT_LO:
      if (count < 1) break;
      bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | data.Read();
      break;
    case xenos::PM4_SET_BIN_SELECT_HI:
      if (count < 1) break;
      bin_select_ = (bin_select_ & 0xFFFFFFFFull) | (uint64_t(data.Read()) << 32);
      break;
    case xenos::PM4_SET_BIN_MASK: {
      if (count < 2) break;
      const uint64_t high = data.Read();
      bin_mask_ = (high << 32) | data.Read();
      break;
    }
    case xenos::PM4_SET_BIN_SELECT: {
      if (count < 2) break;
      const uint64_t high = data.Read();
      bin_select_ = (high << 32) | data.Read();
      break;
    }
    case xenos::PM4_DRAW_INDX:
    case xenos::PM4_DRAW_INDX_2: {
      // DRAW_INDX starts with the visibility query word. The initiator goes to its register; indices from a
      // buffer (DMA) bring its base and size, which the renderer reads from VGT_DMA_BASE/SIZE.
      const uint32_t skip = opcode == xenos::PM4_DRAW_INDX ? 1 : 0;
      if (count < skip + 1) break;
      if (skip) data.Read();
      const uint32_t initiator = data.Read();
      WriteRegister(rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR, initiator);
      if (((initiator >> 6) & 0x3) == uint32_t(xenos::SourceSelect::kDMA)) {
        if (count < skip + 3) break;
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_DMA_BASE, data.Read());
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_DMA_SIZE, data.Read());
      }
      ++stats_.draws;
      rex::x360::DestinosNativos* renderer = host_.Renderer();
      if (!renderer) break;
      if (rex::x360::EsCopia(registers_.data())) {
        // A resolve and/or clear: no shaders.
        const rex::x360::RegistrosCopia copy = rex::x360::LeerRegistrosCopia(registers_.data());
        host_.Copied(copy, renderer->Copiar(copy));
        break;
      }
      Draw();
      break;
    }
    case xenos::PM4_IM_LOAD: {
      // Microcode in GPU memory: address with the type in its two low bits (0 vertex, 1 pixel), size in words.
      if (count < 2) break;
      const uint32_t address_type = data.Read();
      const uint32_t words = data.Read() & 0xFFFF;
      const uint32_t address = address_type & ~uint32_t(0x3);
      const uint8_t* source = memory_->TranslatePhysical(address);
      microcode_.resize(words);
      for (uint32_t i = 0; i < words; ++i) {
        microcode_[i] = rex::memory::load_and_swap<uint32_t>(source + size_t(i) * 4);
      }
      LoadShader(address_type & 0x3, address);
      break;
    }
    case xenos::PM4_IM_LOAD_IMMEDIATE: {
      // Microcode inside the packet: type, then size, then the words.
      if (count < 2) break;
      const uint32_t type = data.Read();
      const uint32_t words = data.Read() & 0xFFFF;
      if (words > count - 2) break;
      microcode_.resize(words);
      for (uint32_t i = 0; i < words; ++i) {
        microcode_[i] = data.Read();
      }
      LoadShader(type & 0x3, 0);
      break;
    }
    default:
      // INVALIDATE_STATE, NOP...: nothing the game waits for.
      break;
  }
}

// An IM_LOAD's microcode (in microcode_) becomes the current shader of its stage. Pixel shaders are found by
// their microcode; vertex shaders by the address of the copy their CreateXShader recorded (D3D patches their
// microcode, mgrr_shaders.h), with the microcode as a fallback. The patched vertex microcode is kept: the
// renderer reads the vertex input from it.
void Decoder::LoadShader(uint32_t type, uint32_t address) {
  mgrr::shaders::Shaders& library = mgrr::shaders::Library();
  if (type == 1) {
    ++stats_.pixel_loads;
    ps_ = library.loaded() ? library.Identify(false, microcode_) : nullptr;
    if (!ps_) ++stats_.pixel_unknown;
    return;
  }
  if (type != 0) return;
  ++stats_.vertex_loads;
  const rex::x360::EntradaShader* vs = address ? mgrr::shaders::ShaderAtAddress(address) : nullptr;
  if (vs && vs->vertices) {
    ++stats_.vertex_by_address;
    if (vs->microcodigo.size() != microcode_.size() && address_misses_logged_ < 16) {
      ++address_misses_logged_;
      REXLOG_INFO("[gpu] VS IM_LOAD {:08X}: {} words, the shader created there has {} (used anyway)", address,
                  microcode_.size(), vs->microcodigo.size());
    }
  } else {
    if (address && address_misses_logged_ < 16) {
      // Why the address did not do: no copy there, another shader's, or a different size.
      ++address_misses_logged_;
      mgrr::shaders::CopyInfo below;
      if (mgrr::shaders::CopyBelow(address, below)) {
        REXLOG_INFO("[gpu] VS IM_LOAD {:08X}, {} words: copy below at {:08X} (+{:X}), {} bytes, {} shader of {} "
                    "words{}",
                    address, microcode_.size(), below.start, address - below.start, below.bytes,
                    below.entry->vertices ? "vertex" : "pixel", below.entry->microcodigo.size(),
                    vs ? " (inside it)" : "");
      } else {
        REXLOG_INFO("[gpu] VS IM_LOAD {:08X}, {} words: no copy recorded at or below", address, microcode_.size());
      }
    }
    vs = library.loaded() ? library.Identify(true, microcode_) : nullptr;
    if (vs) ++stats_.vertex_by_microcode;
    else ++stats_.vertex_unknown;
  }
  vs_ = vs;
  vs_microcode_.assign(microcode_.begin(), microcode_.end());
  ++vs_generation_;
}

void Decoder::Draw() {
  namespace g = rex::graphics;
  const uint32_t mode = registers_[g::XE_GPU_REG_RB_MODECONTROL] & 0x7;
  const uint32_t msaa = (registers_[g::XE_GPU_REG_RB_SURFACE_INFO] >> 16) & 0x3;
  // Which way the scene's depth test goes, for the depth value of the emulated clears.
  if (!msaa && (registers_[g::XE_GPU_REG_RB_DEPTHCONTROL] & 0x2)) {
    const uint32_t zfunc = (registers_[g::XE_GPU_REG_RB_DEPTHCONTROL] >> 4) & 0x7;
    if (zfunc == 4 || zfunc == 6) {
      ++depth_greater_;
    } else if (zfunc == 1 || zfunc == 3) {
      ++depth_less_;
    }
  }
  // Direct3D clears a target by drawing over it with its own internal shaders (not the game's, so not in the
  // library), through the same EDRAM seen as a multisampled surface of half the pitch; MGR's scene targets are cleared
  // this way. Done as the renderer's clears instead. As in ds2-nx (ds2_gpu.cpp, GPL-3): only full-surface
  // multisampled ones (window scissor to 8192), and single-sampled ones only in Direct3D's exact shape (rect list of 3
  // vertices, all four targets' mask, an 8-bit target and a 0..1 color in PS c0): other draws with an unknown VS that
  // look alike are real (a float target filled with game data, a small visibility pass).
  const uint32_t initiator = registers_[g::XE_GPU_REG_VGT_DRAW_INITIATOR];
  bool rect_clear = !msaa && (initiator & 0x3F) == 8 && (initiator >> 16) == 3 &&
                    registers_[g::XE_GPU_REG_RB_COLOR_MASK] == 0xFFFF &&
                    ((registers_[g::XE_GPU_REG_RB_COLOR_INFO] >> 16) & 0xF) <= 1;
  for (uint32_t j = 0; j < 4 && rect_clear; ++j) {
    float v;
    std::memcpy(&v, &registers_[g::XE_GPU_REG_SHADER_CONSTANT_256_X + j], 4);
    rect_clear = v >= 0.0f && v <= 1.0f;
  }
  if (!vs_ && ((msaa && (registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] & 0x3FFF) >= 0x2000) || rect_clear)) {
    EmulateClear(msaa);
    host_.Drew(vs_, ps_, "Direct3D clear, emulated");
    return;
  }
  // In depth-only mode (5) the Xenos does not run the pixel shader, so the vertex shader is enough.
  if (!vs_ || (!ps_ && mode != 5)) {
    ++stats_.draws_without_shaders;
    host_.Drew(vs_, ps_, "no shaders");
    return;
  }
  host_.BeforeDraw(registers_);
  rex::x360::PeticionDibujo request;
  request.registros = registers_.data();
  AliasScope alias(*this);
  if (msaa) {
    // Direct3D's quarter-rate passes: a 1x target drawn as a multisampled surface of half its size over the same
    // EDRAM. The renderer has no multisampling and keys targets by pitch, so the draw goes to the 1x surface with
    // everything in pixels scaled up (the same pixels covered).
    request.registros = alias.Apply(msaa);
    ++viewport_generation_;  // the scaled viewport and scissor are not the mirror's
    ++stats_.multisampled_draws;
  }
  request.vs = vs_;
  request.ps = ps_;
  request.vs_microcodigo = vs_microcode_;
  request.generacion_vs = vs_generation_;
  request.generacion_constantes_vs = constants_vs_generation_;
  request.generacion_constantes_ps = constants_ps_generation_;
  request.generacion_fetch = fetch_generation_;
  request.generacion_encuadre = viewport_generation_;
  const bool sent = host_.Renderer()->Dibujar(request);
  stats_.draws_sent += sent;
  if (msaa) ++viewport_generation_;  // back to the mirror's for the next draw
  host_.Drew(vs_, ps_, sent ? "" : "rejected");
}

// 4X doubles width and height (pitch x2), 2X doubles the height (samples stacked vertically); viewport and scissors
// scale the same way. Done in place on the registers involved (from ds2-nx).
const uint32_t* Decoder::AliasScope::Apply(uint32_t msaa) {
  namespace g = rex::graphics;
  uint32_t* r = decoder_.registers_.data();
  for (uint32_t index : {uint32_t(g::XE_GPU_REG_RB_SURFACE_INFO), uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE),
                         uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 1), uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 2),
                         uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 3), uint32_t(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL),
                         uint32_t(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR), uint32_t(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL),
                         uint32_t(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR)}) {
    saved_[count_++] = {index, r[index]};
  }
  const uint32_t sx = msaa == 2 ? 2 : 1, sy = 2;
  const uint32_t surface = r[g::XE_GPU_REG_RB_SURFACE_INFO];
  r[g::XE_GPU_REG_RB_SURFACE_INFO] = (surface & ~uint32_t(0x33FFF)) | std::min<uint32_t>((surface & 0x3FFF) * sx, 0x3FFF);
  auto scale_float = [&](uint32_t index, uint32_t factor) {
    float v;
    std::memcpy(&v, &r[index], 4);
    v *= float(factor);
    std::memcpy(&r[index], &v, 4);
  };
  scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE, sx);
  scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 1, sx);  // XOFFSET
  scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 2, sy);  // YSCALE
  scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 3, sy);  // YOFFSET
  auto scale_xy = [&](uint32_t index, uint32_t bits) {
    const uint32_t v = r[index];
    const uint32_t mask = (1u << bits) - 1;
    const uint32_t x = std::min((v & mask) * sx, mask), y = std::min(((v >> 16) & mask) * sy, mask);
    r[index] = (v & ~(mask | (mask << 16))) | x | (y << 16);
  };
  scale_xy(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 14);
  scale_xy(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, 14);
  scale_xy(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, 15);
  scale_xy(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, 15);
  return r;
}

Decoder::AliasScope::~AliasScope() {
  for (uint32_t i = 0; i < count_; ++i) decoder_.registers_[saved_[i].first] = saved_[i].second;
}

bool Decoder::ClearVertex(float position[3], float color[4]) const {
  namespace g = rex::graphics;
  // The vertex fetches (FetchOpcode::kVertexFetch) of the clear's vertex shader, from its exec instructions as
  // AddFetchedSamplers walks them (mgrr_shaders.cpp). Direct3D's clear reads float3 position and float4 color.
  struct Fetch {
    uint32_t slot, stride;
    int32_t offset;
  };
  std::vector<Fetch> fetches;
  const std::vector<uint32_t>& code = vs_microcode_;
  size_t first_instruction = code.size() / 3;
  for (size_t pair = 0; pair * 3 + 2 < code.size() && pair * 2 < first_instruction; ++pair) {
    const uint32_t* w = &code[pair * 3];
    const uint64_t cf[2] = {uint64_t(w[0]) | (uint64_t(w[1] & 0xFFFF) << 32),
                            uint64_t((w[1] >> 16) | (w[2] << 16)) | (uint64_t(w[2] >> 16) << 32)};
    for (uint64_t c : cf) {
      const uint32_t opcode = uint32_t(c >> 44) & 0xF;
      if (!((opcode >= 1 && opcode <= 6) || opcode == 13 || opcode == 14)) continue;
      const uint32_t address = uint32_t(c) & 0xFFF, count = uint32_t(c >> 12) & 0x7;
      const uint32_t sequence = uint32_t(c >> 16) & 0xFFF;
      if (count) first_instruction = std::min<size_t>(first_instruction, address);
      for (uint32_t i = 0; i < count; ++i) {
        const size_t at = size_t(address + i) * 3;
        if (!((sequence >> (i * 2)) & 0x1) || at + 2 >= code.size() || (code[at] & 0x1F) != 0) continue;
        const int32_t offset = (int32_t(code[at + 2] << 1) >> 9) * 4;
        const uint32_t stride = (code[at + 2] & 0xFF) * 4;
        if (stride) {
          fetches.push_back({((code[at] >> 20) & 0x1F) * 3 + ((code[at] >> 25) & 0x3), stride, offset});
        } else if (!fetches.empty()) {
          fetches.push_back({fetches.back().slot, fetches.back().stride, offset});  // a mini fetch: the same buffer
        }
      }
    }
  }
  if (fetches.size() < 2) return false;
  std::sort(fetches.begin(), fetches.end(), [](const Fetch& a, const Fetch& b) { return a.offset < b.offset; });
  auto read = [&](const Fetch& f, float* out, uint32_t n) {
    const uint32_t* fc = &registers_[g::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (f.slot / 3) * 6 + (f.slot % 3) * 2];
    const uint32_t base = (fc[0] & 0x1FFFFFFC) + uint32_t(f.offset);
    for (uint32_t k = 0; k < n; ++k) {
      const uint32_t word = rex::memory::load_and_swap<uint32_t>(memory_->TranslatePhysical<uint8_t*>(base + k * 4));
      std::memcpy(&out[k], &word, 4);
    }
  };
  read(fetches[0], position, 3);
  read(fetches[1], color, 4);
  return std::isfinite(position[2]);
}

// A Direct3D clear draw, as the renderer's clears (ds2-nx's EmulateClear, ds2_gpu.cpp). Direct3D's fast clear binds
// c0 (the color to clear) and, for the depth buffer, c1-c3 over the depth buffer's EDRAM written as color, with a
// placeholder depth target that may sit on c0's base. So: c0 is cleared as color; c1-c3 on another base than c0 clear
// the depth buffer at their base; the bound depth target is cleared only when depth or stencil is written and it is
// not on c0's base; depth-only mode clears no color. The stencil gets the reference value Direct3D's quad writes with
// a REPLACE (depthcontrol 00008777), not 0.
void Decoder::EmulateClear(uint32_t msaa) {
  namespace g = rex::graphics;
  rex::x360::DestinosNativos* renderer = host_.Renderer();
  const uint32_t rectangle = host_.ClearRectangle();
  if (!renderer || !rectangle) return;
  AliasScope alias(*this);
  const uint32_t* r = msaa ? alias.Apply(msaa) : registers_.data();
  rex::x360::RegistrosCopia base = rex::x360::LeerRegistrosCopia(r);
  // The whole surface: the renderer takes a copy's rectangle from three vertices in fetch constant 0 (where
  // Direct3D puts a resolve's), clipped by the window scissor.
  const float w = float(r[g::XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFF), h = 8192.0f;
  const float rect[6] = {0.0f, 0.0f, w, 0.0f, 0.0f, h};  // host order, read with Endian::kNone
  std::memcpy(memory_->TranslatePhysical<uint8_t*>(rectangle), rect, sizeof(rect));
  base.fetch_vertices[0] = (rectangle & ~uint32_t(0x3)) | uint32_t(xenos::FetchConstantType::kVertex);
  base.fetch_vertices[1] = (6u << 2) | uint32_t(xenos::Endian::kNone);
  base.pa_sc_window_offset = 0;
  base.pa_sc_window_scissor_tl = 0x80000000u;  // window_offset_disable, from 0,0
  base.pa_sc_window_scissor_br = 0x3FFF3FFFu;
  base.pa_su_sc_mode_cntl &= ~(1u << 16);  // no vtx_window_offset
  const uint32_t mask = r[g::XE_GPU_REG_RB_COLOR_MASK];
  const uint32_t depth_info = r[g::XE_GPU_REG_RB_DEPTH_INFO];
  const uint32_t depth_control = r[g::XE_GPU_REG_RB_DEPTHCONTROL];
  const bool depth_only = (r[g::XE_GPU_REG_RB_MODECONTROL] & 0x7) == 5;
  // The clear's own values are in its vertices: z is the depth, and the color is what its pixel shader writes. Without
  // them, a guess: the far end of the scene's depth test, and 0 for a multisampled color (as in Dead Space 2).
  float position[3] = {}, vertex_color[4] = {};
  const bool from_vertex = ClearVertex(position, vertex_color);
  const bool reversed = depth_greater_ > depth_less_;
  uint32_t stencil = 0;
  if ((depth_control & 0x1) && ((depth_control >> 14) & 0x7) == 2) {
    const uint32_t ref_mask = r[g::XE_GPU_REG_RB_STENCILREFMASK];
    stencil = ref_mask & (ref_mask >> 16) & 0xFF;
  }
  uint32_t depth24 = reversed ? 0u : 0xFFFFFFu;
  if (from_vertex) {
    const float z = std::clamp(position[2], 0.0f, 1.0f);
    depth24 = (depth_info >> 16) & 0x1 ? rex::graphics::xenos::Float32To20e4(z, true)  // DepthRenderTargetFormat::kD24FS8
                                       : uint32_t(std::lround(double(z) * 16777215.0));
  }
  const uint32_t depth_value = (depth24 << 8) | stencil;
  constexpr uint32_t kNullCommand = 3u << 20;  // CopyCommand::kNull: clear only, copy nothing
  std::string what;
  auto clear_depth_at = [&](uint32_t edram_base) {
    rex::x360::RegistrosCopia c = base;
    c.rb_depth_info = (depth_info & ~uint32_t(0xFFF)) | (edram_base & 0xFFF);
    c.rb_copy_control = 4u | (1u << 9) | kNullCommand;
    c.rb_depth_clear = depth_value;
    renderer->Copiar(c);
    what += fmt::format(" z={:03X}->{:08X}", edram_base & 0xFFF, depth_value);
  };
  const uint32_t c0_base = base.rb_color_info[0] & 0xFFF;
  const bool c0_written = !depth_only && (mask & 0xF);
  // The color: the multisampled ones clear to 0 (as seen in Dead Space 2); a single-sampled one takes what
  // Direct3D's clear shader writes, the pixel shader's c0.
  uint32_t color = 0;
  if (msaa && from_vertex) {
    for (uint32_t j = 0; j < 4; ++j) {
      color |= uint32_t(std::lround(std::clamp(vertex_color[j], 0.0f, 1.0f) * 255.0f)) << (j * 8);
    }
  } else if (!msaa) {
    for (uint32_t j = 0; j < 4; ++j) {
      float v;
      std::memcpy(&v, &registers_[g::XE_GPU_REG_SHADER_CONSTANT_256_X + j], 4);
      color |= uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)) << (j * 8);
    }
  }
  if (c0_written) {
    rex::x360::RegistrosCopia c = base;
    c.rb_copy_control = (1u << 8) | kNullCommand;
    c.rb_color_clear = color;
    c.rb_color_clear_lo = 0;
    renderer->Copiar(c);
    what += fmt::format(" c0={:03X}={:08X}", c0_base, color);
  }
  uint32_t aliased_depth[3] = {};
  uint32_t n_aliased = 0;
  for (uint32_t i = 1; i < 4 && !depth_only; ++i) {
    const uint32_t b = base.rb_color_info[i] & 0xFFF;
    if (!((mask >> (i * 4)) & 0xF) || (c0_written && b == c0_base)) continue;
    if (std::find(aliased_depth, aliased_depth + n_aliased, b) != aliased_depth + n_aliased) continue;
    aliased_depth[n_aliased++] = b;
    clear_depth_at(b);
  }
  const uint32_t z_base = depth_info & 0xFFF;
  const bool z_written = depth_only || (depth_control & 0x4) || (depth_control & 0x1);
  if (z_written && !(c0_written && z_base == c0_base) &&
      std::find(aliased_depth, aliased_depth + n_aliased, z_base) == aliased_depth + n_aliased) {
    clear_depth_at(z_base);
    // Depth written over c0's EDRAM: on the console the same bytes are the color target's, so the color is cleared
    // by it: to the depth and stencil word, seen as color (MGR's shadow mask reads that EDRAM as 8-bit color after a
    // depth-only clear to 1.0 and stencil 0xFF, and needs it white). The renderer keeps depth and color apart, so the
    // color image is cleared too.
    if (z_base == c0_base && (mask & 0xF)) {
      rex::x360::RegistrosCopia c = base;
      c.rb_copy_control = (1u << 8) | kNullCommand;
      c.rb_color_clear = depth_value;
      c.rb_color_clear_lo = 0;
      renderer->Copiar(c);
      what += fmt::format(" c0={:03X}={:08X} (by the depth over it)", c0_base, depth_value);
    }
  }
  ++stats_.clears_emulated;
  if (clears_logged_ < 16) {
    ++clears_logged_;
    REXLOG_INFO("[gpu] Direct3D clear (msaa {}, pitch {}, mode {}):{}; depthcontrol {:08X} mask {:04X}; {}", msaa,
                registers_[g::XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFF, r[g::XE_GPU_REG_RB_MODECONTROL] & 0x7, what,
                depth_control, mask,
                from_vertex ? fmt::format("from its vertex: z {} color {} {} {} {}", position[2], vertex_color[0],
                                          vertex_color[1], vertex_color[2], vertex_color[3])
                            : std::string("guessed (no vertex)"));
  }
}

// EVENT_WRITE_ZPD, as the SDK's CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD: D3D marks the
// structure with 0xFFFFFEED on Issue(END), and the counts are faked.
void Decoder::OcclusionQuery(uint32_t address) {
  if (!address) {
    return;
  }
  const uint32_t kQueryFinished = rex::byte_swap(0xFFFFFEEDu);  // written big-endian, the struct is LE
  auto* counts = memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(address);
  const bool end_via_z_pass = counts->ZPass_A == kQueryFinished || counts->ZPass_B == kQueryFinished;
  const bool end_via_z_fail = counts->ZFail_A == kQueryFinished || counts->ZFail_B == kQueryFinished;
  std::memset(counts, 0, sizeof(*counts));
  if (end_via_z_pass || end_via_z_fail) {
    counts->ZPass_A = kFakeSampleCount;
    counts->Total_A = kFakeSampleCount;
  }
}

}  // namespace mgrr::pm4
