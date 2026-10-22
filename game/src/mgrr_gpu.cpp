// Adapted from xbox360-nx (https://github.com/nx-mod/xbox360-nx, GPL-3) halo3/src/halo3_gpu.cpp @ 6575330:
// renamed to mgrr; the logic is game-neutral (PM4 and SDK only, no Halo addresses).
// mgrr - the game's graphics system on the Switch.
//
// What it does
//   Stands in for the Xbox 360 GPU, so the game runs past its first frame. The game's own Direct3D keeps
//   running unchanged and writes its list of GPU commands (the PM4 ring). A thread reads that list and does
//   what the game waits for:
//     1. MMIO registers at 0x7FC80000, with the fixed values the SDK's emulation returns;
//     2. the ring read pointer, written back where the game watches it (it spins on it to know how much
//        room is left);
//     3. interrupts: vblank, and PM4_INTERRUPT packets (the GPU reached that point of the ring);
//     4. the packets themselves go to the PM4 decoder (mgrr_pm4.h): GPU memory writes, draws for the SDK's deko3d
//        renderer, Swaps present. The replay player feeds the same decoder from a recording.
//
// Presentation (the deko3d build): the SDK's deko3d provider and presenter (rex/ui/deko3d/presentador.h). ReXApp
// sees them and sets up the window and the ImGui overlay as usual; until the renderer refreshes the guest output,
// the presenter paints black under the overlay.
//
// Without it the runtime drops the game's Vd* calls ("no GPU emulation loaded") and the render thread waits
// forever for a GPU that never reads its commands.

#include "mgrr_gpu.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

#include <rex/graphics/registers.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xobject.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>
#include <rex/system/xvideo.h>
#include <rex/thread.h>
#include <rex/types.h>
#include <rex/ui/graphics_provider.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>

#if MGRR_DEKO3D
#include <rex/ui/deko3d/presentador.h>
#include <rex/x360/deko/destinos.h>
#include <rex/x360/decodificador.h>
#endif

#include "mgrr_pm4.h"
#include "mgrr_stats.h"

namespace mgrr::gpu {
namespace {

using rex::X_STATUS;  // X_STATUS_SUCCESS and X_STATUS_UNSUCCESSFUL need it too
using Clock = std::chrono::steady_clock;

constexpr uint32_t kMmioBase = 0x7FC80000;
constexpr uint32_t kMmioMask = 0xFFFF0000;
constexpr uint32_t kMmioSize = 0x0000FFFF;
constexpr uint32_t kRegisterCount = mgrr::pm4::kRegisterCount;

constexpr uint32_t kRegCpRbWptr = rex::graphics::XE_GPU_REG_CP_RB_WPTR;
constexpr uint32_t kRegRbEdramTiming = 0x0F00;
constexpr uint32_t kRegRbBcControl = 0x0F01;
constexpr uint32_t kRegD1ModeVCounter = 0x194C;
constexpr uint32_t kRegD1ModeVblankVlineStatus = 0x1951;
constexpr uint32_t kRegD1ModeViewportSize = 0x1961;

constexpr auto kReportEvery = std::chrono::seconds(10);

class RingGraphicsSystem final : public rex::system::IGraphicsSystem, public mgrr::pm4::Host {
 public:
  RingGraphicsSystem() = default;
  ~RingGraphicsSystem() override { Shutdown(); }

  X_STATUS SetupPresentation(rex::ui::WindowedAppContext* app_context) override {
#if MGRR_DEKO3D
    if (presenter_) {
      return X_STATUS_SUCCESS;
    }
    app_context_ = app_context;
    if (!provider_) {
      provider_ = rex::deko3d::ProveedorDeko3d::Crear();  // a deko3d failure ends the process, logged
      if (!provider_) {
        REXLOG_ERROR("[gpu] could not create the deko3d device");
        return X_STATUS_UNSUCCESSFUL;
      }
    }
    auto create = [this]() { presenter_ = provider_->CreatePresenter(); };
    if (app_context_) {
      app_context_->CallInUIThreadSynchronous(create);
    } else {
      create();
    }
    if (!presenter_) {
      REXLOG_ERROR("[gpu] could not create the deko3d presenter");
      return X_STATUS_UNSUCCESSFUL;
    }
    REXLOG_INFO("[gpu] deko3d presenter up (the game's frames come with the renderer)");
#else
    (void)app_context;
#endif
    return X_STATUS_SUCCESS;
  }

  X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* dispatcher,
                         rex::system::KernelState* kernel_state) override {
    dispatcher_ = dispatcher;
    kernel_state_ = kernel_state;
    memory_ = dispatcher->memory();
    if (!decoder_) {
      decoder_ = std::make_unique<mgrr::pm4::Decoder>(memory_, *this);
    }
    if (!mmio_registered_) {
      mmio_registered_ =
          memory_->AddVirtualMappedRange(kMmioBase, kMmioMask, kMmioSize, this, &ReadMmio, &WriteMmio);
      if (!mmio_registered_) {
        REXLOG_ERROR("[gpu] could not register the GPU's MMIO range");
        return X_STATUS_UNSUCCESSFUL;
      }
    }
    if (active_.exchange(true)) {
      return X_STATUS_SUCCESS;
    }
    last_report_ = Clock::now();
    vblank_thread_ = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        kernel_state_, 128 * 1024, 0, [this]() { return VblankLoop(); }));
    vblank_thread_->set_name("GPU vblank");
    vblank_thread_->Create();
    ring_thread_ = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        kernel_state_, 128 * 1024, 0, [this]() { return RingLoop(); }));
    ring_thread_->set_name("GPU ring");
    ring_thread_->Create();
    REXLOG_INFO("[gpu] ring reader started");
    return X_STATUS_SUCCESS;
  }

  bool has_presentation() const override { return presenter_ != nullptr; }
  rex::ui::GraphicsProvider* provider() const override { return provider_.get(); }
  rex::ui::Presenter* presenter() const override { return presenter_.get(); }

  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override {
    callback_data_.store(user_data, std::memory_order_release);
    callback_.store(callback, std::memory_order_release);
    REXLOG_INFO("[gpu] interrupt callback {:08X} ({:08X})", callback, user_data);
  }

  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override {
    // As in CommandProcessor::InitializeRingBuffer: (1 << (size_log2 + 3)) bytes.
    ring_words_.store(uint32_t(1) << (size_log2 + 1), std::memory_order_release);
    ring_base_.store(ptr, std::memory_order_release);
    ring_generation_.fetch_add(1, std::memory_order_acq_rel);
    REXLOG_INFO("[gpu] ring at {:08X}, {} words", ptr, uint32_t(1) << (size_log2 + 1));
  }

  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override {
    (void)block_size_log2;
    read_pointer_writeback_.store(ptr, std::memory_order_release);
  }

  void Shutdown() override {
    if (!active_.exchange(false)) {
      DestroyPresentation();
      return;
    }
    {
      std::lock_guard<std::mutex> lock(ring_mutex_);
    }
    ring_cv_.notify_all();
    if (ring_thread_) {
      ring_thread_->Wait(0, 0, 0, nullptr);
      ring_thread_.reset();
    }
    if (vblank_thread_) {
      vblank_thread_->Wait(0, 0, 0, nullptr);
      vblank_thread_.reset();
    }
    DestroyPresentation();
  }

  // After the threads: the presenter goes before its provider, on the UI thread.
  void DestroyPresentation() {
    destinos_.reset();  // frees its deko3d memory before the device goes
    if (presenter_) {
      if (app_context_) {
        app_context_->CallInUIThreadSynchronous([this]() { presenter_.reset(); });
      }
      presenter_.reset();
    }
    provider_.reset();
  }

 private:
  // --- MMIO registers ---------------------------------------------------------

  static uint32_t ReadMmio(void* ppc_context, void* context, uint32_t address) {
    (void)ppc_context;
    return static_cast<RingGraphicsSystem*>(context)->ReadMmioRegister(address);
  }

  static void WriteMmio(void* ppc_context, void* context, uint32_t address, uint32_t value) {
    (void)ppc_context;
    static_cast<RingGraphicsSystem*>(context)->WriteMmioRegister(address, value);
  }

  // The same fixed values as the SDK's emulation (graphics/graphics_system.cpp).
  uint32_t ReadMmioRegister(uint32_t address) {
    const uint32_t r = (address & 0xFFFF) / 4;
    switch (r) {
      case kRegRbEdramTiming:
        return 0x08100748;
      case kRegRbBcControl:
        return 0x0000200E;
      case kRegD1ModeVCounter: {
        rex::system::X_VIDEO_MODE mode;
        rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
        return std::min(uint32_t(mode.display_height), uint32_t(0x0FFF));
      }
      case kRegD1ModeVblankVlineStatus:
        return 1;  // in vblank
      case kRegD1ModeViewportSize: {
        rex::system::X_VIDEO_MODE mode;
        rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
        const uint32_t width = std::min(uint32_t(mode.display_width), uint32_t(0x0FFF));
        const uint32_t height = std::min(uint32_t(mode.display_height), uint32_t(0x0FFF));
        return (width << 16) | height;
      }
      default:
        return decoder_->Register(r);
    }
  }

  void WriteMmioRegister(uint32_t address, uint32_t value) {
    const uint32_t r = (address & 0xFFFF) / 4;
    if (r < kRegisterCount) {
      decoder_->registers()[r] = value;  // no side effects through MMIO (graphics_system.cpp)
    }
    if (r == kRegCpRbWptr) {
      write_pointer_.store(value, std::memory_order_release);
      // Taking the lock for an instant avoids losing the wakeup if the ring thread is between checking
      // and going to sleep.
      {
        std::lock_guard<std::mutex> lock(ring_mutex_);
      }
      ring_cv_.notify_one();
    }
  }

  // --- The decoder's host (mgrr_pm4.h), on the ring thread ---------------------

  // Runs the game's interrupt callback on the calling thread (one of ours, an XHostThread).
  void Interrupt(uint32_t source, uint32_t cpu) override {
    const uint32_t callback = callback_.load(std::memory_order_acquire);
    if (!callback || !dispatcher_) {
      return;
    }
    auto* thread = rex::system::XThread::GetCurrentThread();
    if (!thread) {
      return;
    }
    thread->SetActiveCpu(uint8_t(cpu));
    uint64_t args[] = {source, callback_data_.load(std::memory_order_acquire)};
    dispatcher_->ExecuteInterrupt(thread->thread_state(), callback, args, 2);
  }

  bool WaitsForGame() const override { return true; }
  bool Active() const override { return active_.load(std::memory_order_acquire); }
  uint32_t Counter() const override { return counter_.load(std::memory_order_relaxed); }

  // Creates the deko renderer backend on first use (on the ring thread, as the device allows). MGR plugs in no
  // effects, so it uses the default hooks.
  rex::x360::DestinosNativos* Renderer() override {
    if (destinos_) {
      return destinos_.get();
    }
    if (!provider_ || !presenter_) {
      return nullptr;
    }
    destinos_ = rex::x360::CrearDestinosDeko3d(
        static_cast<rex::deko3d::ProveedorDeko3d*>(provider_.get()), memory_);
    if (destinos_) {
      REXLOG_INFO("[gpu] deko3d renderer backend up");
    }
    return destinos_.get();
  }

  // 32 bytes of guest physical memory, allocated once, for the emulated Direct3D clears' rectangle.
  uint32_t ClearRectangle() override {
    if (!clear_rectangle_) {
      const uint32_t guest = memory_->SystemHeapAlloc(32, 32, rex::memory::kSystemHeapPhysical);
      if (guest) clear_rectangle_ = memory_->GetPhysicalAddress(guest);
    }
    return clear_rectangle_;
  }

  void Swap(uint32_t width, uint32_t height) override {
    counter_.fetch_add(1, std::memory_order_relaxed);
    if (rex::x360::DestinosNativos* renderer = Renderer()) {
      renderer->Presentar(presenter_.get(), rex::x360::LeerTexturaSwap(decoder_->registers().data()), width,
                          height);
    }
  }

  // --- Threads ----------------------------------------------------------------

  int VblankLoop() {
    rex::system::X_VIDEO_MODE mode;
    rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
    const double hz = std::max(1.0, double(float(mode.refresh_rate)));
    const auto interval =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / hz));
    auto next = Clock::now() + interval;
    while (active_.load(std::memory_order_acquire)) {
      const auto now = Clock::now();
      // After a long pause (loading, a debugger) the missed vblanks are not replayed.
      if (now - next > std::chrono::milliseconds(250)) {
        next = now;
      }
      while (now >= next) {
        counter_.fetch_add(1, std::memory_order_relaxed);
        vblanks_.fetch_add(1, std::memory_order_relaxed);
        Interrupt(0, 2);
        next += interval;
      }
      rex::thread::Sleep(std::chrono::milliseconds(1));
    }
    return 0;
  }

  int RingLoop() {
    uint32_t read = 0;
    uint32_t generation = 0;
    while (active_.load(std::memory_order_acquire)) {
      {
        std::unique_lock<std::mutex> lock(ring_mutex_);
        ring_cv_.wait_for(lock, std::chrono::milliseconds(4), [&] {
          return !active_.load(std::memory_order_acquire) ||
                 write_pointer_.load(std::memory_order_acquire) != read ||
                 ring_generation_.load(std::memory_order_acquire) != generation;
        });
      }
      if (!active_.load(std::memory_order_acquire)) {
        break;
      }
      Report();
      PublishOverlay();
      const uint32_t gen = ring_generation_.load(std::memory_order_acquire);
      if (gen != generation) {
        generation = gen;
        read = 0;  // InitializeRingBuffer leaves the read pointer at zero
      }
      const uint32_t base = ring_base_.load(std::memory_order_acquire);
      const uint32_t words = ring_words_.load(std::memory_order_acquire);
      if (!base || !words) {
        continue;
      }
      const uint32_t written = write_pointer_.load(std::memory_order_acquire) & (words - 1);
      if (written == read) {
        continue;
      }
      mgrr::pm4::Reader reader;
      reader.base = memory_->TranslatePhysical(base);
      reader.mask = words - 1;
      reader.pos = read;
      reader.end = written;
      while (reader.Pending()) {
        if (!decoder_->Packet(reader)) {
          break;  // partial packet: the rest comes with the next CP_RB_WPTR
        }
      }
      read = reader.pos;
      const uint32_t writeback = read_pointer_writeback_.load(std::memory_order_acquire);
      if (writeback) {
        rex::memory::store_and_swap<uint32_t>(memory_->TranslatePhysical(writeback), read);
      }
    }
    return 0;
  }

  // One line every kReportEvery, on the ring thread, so the log shows whether the game is advancing.
  void Report() {
    const auto now = Clock::now();
    if (now - last_report_ < kReportEvery) {
      return;
    }
    const double seconds = std::chrono::duration<double>(now - last_report_).count();
    last_report_ = now;
    const mgrr::pm4::Stats& s = decoder_->stats();
    REXLOG_INFO("[gpu] {:.1f} s: {} swaps ({:.1f}/s), {} draws, {} packets, {} vblanks, {} waits cut short",
                seconds, s.swaps - reported_swaps_, double(s.swaps - reported_swaps_) / seconds,
                s.draws - reported_draws_, s.packets - reported_packets_,
                vblanks_.load(std::memory_order_relaxed), s.waits_cut_short);
    // Totals since start: how the shaders of the IM_LOADs were found, and what the renderer did with the draws.
    REXLOG_INFO("[gpu] shaders: VS loads {} (by address {}, by microcode {}, unknown {}), PS loads {} (unknown {}); "
                "draws sent {}, without shaders {}",
                s.vertex_loads, s.vertex_by_address, s.vertex_by_microcode, s.vertex_unknown, s.pixel_loads,
                s.pixel_unknown, s.draws_sent, s.draws_without_shaders);
    if (destinos_) {
      const rex::x360::EstadisticasDibujos stats = destinos_->EstadisticasDeDibujos();
      std::string causes;
      for (size_t i = 0; i < stats.causas.size() && i < 8; ++i) {
        causes += fmt::format(" {}x{}", stats.causas[i].first, stats.causas[i].second);
      }
      REXLOG_INFO("[gpu] renderer: drawn {}, rejected {} (cause x count:{}), pipelines {}, textures {}",
                  stats.dibujados, stats.rechazados, causes, stats.pipelines, stats.texturas);
    }
    reported_swaps_ = s.swaps;
    reported_draws_ = s.draws;
    reported_packets_ = s.packets;
  }

  // Once a second, the overlay's numbers (mgrr_stats.h), on the ring thread like the renderer's statistics.
  void PublishOverlay() {
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - last_overlay_).count();
    if (seconds < 1.0) {
      return;
    }
    last_overlay_ = now;
    const mgrr::pm4::Stats& s = decoder_->stats();
    mgrr::stats::Gpu g;
    if (destinos_) {
      const rex::x360::EstadisticasDibujos stats = destinos_->EstadisticasDeDibujos();
      g.drawn_per_s = double(stats.dibujados - overlay_drawn_) / seconds;
      g.rejected_per_s = double(stats.rechazados - overlay_rejected_) / seconds;
      overlay_drawn_ = stats.dibujados;
      overlay_rejected_ = stats.rechazados;
      for (size_t i = 0; i < stats.causas.size() && i < 4; ++i) {
        g.top_causes += fmt::format(" {}x{}", stats.causas[i].first, stats.causas[i].second);
      }
    }
    g.without_shaders_per_s = double(s.draws_without_shaders - overlay_without_shaders_) / seconds;
    overlay_without_shaders_ = s.draws_without_shaders;
    g.vs_by_address = s.vertex_by_address;
    g.vs_by_microcode = s.vertex_by_microcode;
    g.vs_unknown = s.vertex_unknown;
    g.ps_loads = s.pixel_loads;
    g.ps_unknown = s.pixel_unknown;
    mgrr::stats::PublishGpu(g);
  }

  rex::ui::WindowedAppContext* app_context_ = nullptr;
  std::unique_ptr<rex::ui::GraphicsProvider> provider_;
  std::unique_ptr<rex::ui::Presenter> presenter_;
  std::unique_ptr<rex::x360::DestinosNativos> destinos_;  // the universal deko renderer backend
  rex::runtime::FunctionDispatcher* dispatcher_ = nullptr;
  rex::system::KernelState* kernel_state_ = nullptr;
  rex::memory::Memory* memory_ = nullptr;
  uint32_t clear_rectangle_ = 0;  // ClearRectangle
  std::unique_ptr<mgrr::pm4::Decoder> decoder_;
  bool mmio_registered_ = false;
  std::atomic<bool> active_{false};

  std::atomic<uint32_t> callback_{0};
  std::atomic<uint32_t> callback_data_{0};
  std::atomic<uint32_t> ring_base_{0};
  std::atomic<uint32_t> ring_words_{0};
  std::atomic<uint32_t> ring_generation_{0};
  std::atomic<uint32_t> write_pointer_{0};
  std::atomic<uint32_t> read_pointer_writeback_{0};
  std::atomic<uint32_t> counter_{0};  // vblanks + swaps, for EVENT_WRITE_SHD
  std::atomic<uint64_t> vblanks_{0};
  std::mutex ring_mutex_;
  std::condition_variable ring_cv_;
  rex::system::object_ref<rex::system::XHostThread> ring_thread_;
  rex::system::object_ref<rex::system::XHostThread> vblank_thread_;

  // Only the ring thread touches these.
  uint64_t reported_swaps_ = 0, reported_draws_ = 0, reported_packets_ = 0;
  Clock::time_point last_report_;
  Clock::time_point last_overlay_ = Clock::now();
  uint64_t overlay_drawn_ = 0, overlay_rejected_ = 0, overlay_without_shaders_ = 0;
};

}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem> CreateGraphicsSystem() {
  return std::make_unique<RingGraphicsSystem>();
}

}  // namespace mgrr::gpu
