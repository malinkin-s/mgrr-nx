// mgrr - the PM4 decoder: Xenos command packets become register state, GPU memory writes and draws for the SDK's
// deko3d renderer. Fed by the live GPU (mgrr_gpu.cpp: the game's ring) and by the replay player (a recording made
// on PC, rex/graphics/gpu_trace.h); what differs between them goes through Host.

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace rex::memory {
class Memory;
}
namespace rex::x360 {
class DestinosNativos;
struct EntradaShader;
struct RegistrosCopia;
}  // namespace rex::x360

namespace mgrr::pm4 {

constexpr uint32_t kRegisterCount = 0x5003;  // RegisterFile::kRegisterCount

// Reads the ring (a power-of-two buffer of words that wraps) or an indirect buffer (linear). Words are big endian.
struct Reader {
  const uint8_t* base = nullptr;
  uint32_t mask = 0;  // words - 1 in the ring; 0 in a linear buffer
  uint32_t pos = 0;
  uint32_t end = 0;

  uint32_t Pending() const { return mask ? ((end - pos) & mask) : (end - pos); }
  uint32_t Peek() const;
  uint32_t Read() {
    const uint32_t value = Peek();
    Skip(1);
    return value;
  }
  void Skip(uint32_t words) { pos = mask ? ((pos + words) & mask) : (pos + words); }
};

// Totals since start.
struct Stats {
  uint64_t swaps = 0, draws = 0, packets = 0, waits_cut_short = 0;
  uint64_t vertex_loads = 0, vertex_by_address = 0, vertex_by_microcode = 0, vertex_unknown = 0;
  uint64_t pixel_loads = 0, pixel_unknown = 0, draws_sent = 0, draws_without_shaders = 0;
  uint64_t clears_emulated = 0, multisampled_draws = 0;
};

// What the decoder needs from whoever runs it.
class Host {
 public:
  virtual ~Host() = default;
  // PM4_INTERRUPT: the game's callback for that CPU.
  virtual void Interrupt(uint32_t source, uint32_t cpu) = 0;
  // WAIT_REG_MEM: the live GPU waits for the game (up to a limit); a replay goes on, as the memory and registers it
  // would wait on are those of the recording.
  virtual bool WaitsForGame() const = 0;
  virtual bool Active() const = 0;
  // vblanks + swaps, for EVENT_WRITE_SHD.
  virtual uint32_t Counter() const = 0;
  // The renderer, created on first use; nullptr while there is none.
  virtual rex::x360::DestinosNativos* Renderer() = 0;
  // XE_SWAP, after the decoder counted it: present the frontbuffer (Decoder::registers() name it).
  virtual void Swap(uint32_t width, uint32_t height) = 0;
  // Physical address of 32 bytes the decoder may overwrite: the rectangle of the Direct3D clears it emulates
  // (Decoder::EmulateClear). 0 if there is none.
  virtual uint32_t ClearRectangle() = 0;
  // A resolve and/or clear went to the renderer (done: it accepted it).
  virtual void Copied(const rex::x360::RegistrosCopia&, bool) {}
  // A draw is about to go to the renderer with these registers (diagnostics may change them; Drew comes after).
  virtual void BeforeDraw(std::vector<uint32_t>&) {}
  // A draw (not a copy) with the shaders it had (nullptr: not identified) and what became of it: "" sent to the
  // renderer, otherwise why not.
  virtual void Drew(const rex::x360::EntradaShader*, const rex::x360::EntradaShader*, const char*) {}
};

class Decoder {
 public:
  Decoder(rex::memory::Memory* memory, Host& host);

  // Consumes a whole packet, or leaves the position untouched if it is not complete yet.
  bool Packet(Reader& reader, int depth = 0);

  // The register mirror. MMIO writes go straight in (no side effects, as graphics_system.cpp).
  std::vector<uint32_t>& registers() { return registers_; }
  uint32_t Register(uint32_t index) const { return index < kRegisterCount ? registers_[index] : 0; }
  // Replaces the whole register file (a recording's start) and invalidates what the renderer derived from it.
  void LoadRegisters(const uint32_t* values, uint32_t count);
  // After registers() was changed directly (diagnostics): the renderer derives everything again.
  void Invalidate();

  const Stats& stats() const { return stats_; }
  // The current vertex shader's microcode as D3D patched it (its vertex fetches), for diagnostics.
  const std::vector<uint32_t>& vs_microcode() const { return vs_microcode_; }

 private:
  void WriteRegister(uint32_t index, uint32_t value);
  uint32_t ReadMemory(uint32_t address) const;
  void WriteMemory(uint32_t address, uint32_t value);
  void PacketType3(Reader& data, uint32_t packet, uint32_t count, int depth);
  void LoadShader(uint32_t type, uint32_t address);
  void Draw();
  void EmulateClear(uint32_t msaa);
  // The first vertex of a Direct3D clear draw: position (its z is the clear depth) and color, read through the vertex
  // fetches of the clear's vertex shader. false if they cannot be found.
  bool ClearVertex(float position[3], float color[4]) const;
  void OcclusionQuery(uint32_t address);

  // The register mirror with a multisampled surface turned into the 1x surface it aliases, undone when the scope
  // ends (Decoder::Draw).
  class AliasScope {
   public:
    explicit AliasScope(Decoder& decoder) : decoder_(decoder) {}
    ~AliasScope();
    const uint32_t* Apply(uint32_t msaa);

   private:
    Decoder& decoder_;
    std::pair<uint32_t, uint32_t> saved_[12];
    uint32_t count_ = 0;
  };

  rex::memory::Memory* memory_;
  Host& host_;
  std::vector<uint32_t> registers_;
  uint64_t bin_mask_ = ~uint64_t(0);
  uint64_t bin_select_ = ~uint64_t(0);

  const rex::x360::EntradaShader* vs_ = nullptr;
  const rex::x360::EntradaShader* ps_ = nullptr;
  std::vector<uint32_t> microcode_;     // the last IM_LOAD, host byte order
  std::vector<uint32_t> vs_microcode_;  // the current vertex shader as D3D patched it
  uint32_t address_misses_logged_ = 0;
  uint64_t vs_generation_ = 0, constants_vs_generation_ = 0, constants_ps_generation_ = 0;
  uint64_t fetch_generation_ = 0, viewport_generation_ = 0;
  uint64_t depth_greater_ = 0, depth_less_ = 0;  // the scene's depth test direction, for the emulated clears
  uint32_t clears_logged_ = 0;
  Stats stats_;
};

}  // namespace mgrr::pm4
