// mgrr - replay of a GPU recording through the Switch renderer (mgrr_replay.h).
//
// The recording (rex/graphics/gpu_trace.h) is read record by record: 'M' copies a blob into the replay's physical
// memory, 'P' goes through the PM4 decoder, whose XE_SWAP presents and saves the frame. <FILE>.shaders, written next
// to it by the PC build, gives the addresses of the vertex shaders' microcode copies (mgrr_shader_hooks.h), so they
// are found as in the game. Pipelines compile in the background, so the first pass misses some draws: the recording
// is played --mgrr_replay_passes times and every pass is saved.
//
// Output in --mgrr_replay_out (default: FILE.out/): pass<P>_frame<F>.png and report.txt, also in the log.

#include "mgrr_replay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xmemory.h>
#include <rex/thread.h>
#include <rex/ui/deko3d/presentador.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/x360/deko/destinos.h>
#include <rex/x360/decodificador.h>
#include <rex/x360/shader_entry.h>

#include "mgrr_pm4.h"
#include "mgrr_shader_hooks.h"
#include "mgrr_shaders.h"

REXCVAR_DEFINE_STRING(mgrr_replay, "", "MGRR",
                      "Replay this GPU recording (gpu_trace_path of the PC build) instead of running the game");
REXCVAR_DEFINE_STRING(mgrr_replay_out, "", "MGRR", "With mgrr_replay: output folder; empty: <recording>.out/");
REXCVAR_DEFINE_INT32(mgrr_replay_passes, 3, "MGRR", "With mgrr_replay: times the recording is played");
REXCVAR_DEFINE_STRING(mgrr_replay_probes, "", "MGRR",
                      "With mgrr_replay_steps: after these draws of that frame (comma separated), save the color target "
                      "0 as probe<N>.png (a resolve of it to a scratch texture)");
REXCVAR_DEFINE_STRING(mgrr_replay_plain, "", "MGRR",
                      "With mgrr_replay_steps: these draws of that frame (comma separated) go without depth and stencil "
                      "test, blending and alpha test, to see whether they land anywhere");
REXCVAR_DEFINE_INT32(mgrr_replay_plain_constant, -1, "MGRR",
                     "With mgrr_replay_plain: also set this vertex shader constant to 1, 1, 1, 1 for those draws");
REXCVAR_DEFINE_STRING(mgrr_replay_plain_texture, "", "MGRR",
                      "With mgrr_replay_plain: texture fetch constant 0 of those draws points at this address (hex)");
REXCVAR_DEFINE_INT32(mgrr_replay_dump, -1, "MGRR",
                     "With mgrr_replay: in the last pass, the renderer's frame dump of this frame (every resolved "
                     "texture at full precision, volcado_NN/ next to the NRO); not with mgrr_replay_steps; -1 = off");
REXCVAR_DEFINE_INT32(mgrr_replay_steps, -1, "MGRR",
                     "With mgrr_replay: in the last pass, save the image after each copy of this frame; -1 = off");

namespace mgrr::replay {
namespace {

constexpr size_t kPhysicalSize = 0x20000000;
constexpr size_t kPhysicalSlack = 0x01000000;  // reads that start near the end of physical memory

// --- PNG without compression (stored deflate blocks): no library needed, the PC side converts if it wants -----

uint32_t Crc32(uint32_t crc, const uint8_t* data, size_t size) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void PutBe32(std::vector<uint8_t>& out, uint32_t v) {
  const uint8_t b[] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
  out.insert(out.end(), b, b + 4);
}

void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  PutBe32(out, uint32_t(data.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  PutBe32(out, Crc32(0, out.data() + start, out.size() - start));
}

// R8 G8 B8 X8 rows (RawImage) as an RGB PNG.
bool WritePng(const std::filesystem::path& path, const rex::ui::RawImage& image) {
  std::vector<uint8_t> raw;
  raw.reserve(size_t(image.height) * (1 + size_t(image.width) * 3));
  for (uint32_t y = 0; y < image.height; ++y) {
    raw.push_back(0);  // filter: none
    const uint8_t* row = image.data.data() + size_t(y) * image.stride;
    for (uint32_t x = 0; x < image.width; ++x) raw.insert(raw.end(), row + x * 4, row + x * 4 + 3);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < raw.size(); ++i) {
    a = (a + raw[i]) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t pos = 0; pos < raw.size() || pos == 0;) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n));
    z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n));
    z.push_back(uint8_t(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (last) break;
  }
  PutBe32(z, (b << 16) | a);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> header;
  PutBe32(header, image.width);
  PutBe32(header, image.height);
  header.insert(header.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB
  Chunk(png, "IHDR", header);
  Chunk(png, "IDAT", z);
  Chunk(png, "IEND", {});
  FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  return std::fclose(f) == 0 && ok;
}

std::vector<std::string> SplitList(const std::string& text) {
  std::vector<std::string> items;
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = std::min(text.find(',', start), text.size());
    if (end > start) items.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return items;
}

// --- The replay ------------------------------------------------------------------------------------------------

class Replay final : public mgrr::pm4::Host {
 public:
  Replay(rex::system::IGraphicsSystem* graphics, rex::ui::WindowedAppContext* app_context)
      : graphics_(graphics), app_context_(app_context) {}

  void Run() {
    const std::filesystem::path trace = REXCVAR_GET(mgrr_replay);
    out_ = REXCVAR_GET(mgrr_replay_out).empty() ? std::filesystem::path(trace.string() + ".out")
                                                : std::filesystem::path(REXCVAR_GET(mgrr_replay_out));
    std::error_code error;
    std::filesystem::create_directories(out_, error);
    report_ = std::fopen((out_ / "report.txt").string().c_str(), "w");
    Line("replay of {}", trace.string());
    if (Setup(trace)) {
      const int passes = std::max(1, REXCVAR_GET(mgrr_replay_passes));
      for (pass_ = 1; pass_ <= passes; ++pass_) {
        if (!Pass(trace)) break;
        rex::thread::Sleep(std::chrono::milliseconds(500));  // background pipeline compiles
      }
    }
    if (REXCVAR_GET(mgrr_replay_dump) >= 0) {
      rex::thread::Sleep(std::chrono::seconds(5));  // the dump's files are written by a thread of the renderer
    }
    Line("replay done");
    if (report_) std::fclose(report_);
    destinos_.reset();  // its deko3d memory, before the app takes the device down
    if (app_context_) app_context_->RequestDeferredQuit();
  }

  // --- mgrr::pm4::Host ---
  void Interrupt(uint32_t, uint32_t) override {}
  bool WaitsForGame() const override { return false; }
  bool Active() const override { return true; }
  uint32_t Counter() const override { return counter_; }
  rex::x360::DestinosNativos* Renderer() override { return destinos_.get(); }

  // Step mode (--mgrr_replay_steps): each resolve of that frame, presented on its own, as step<NN>.png. Presenting
  // mid-frame is only for this: it also advances the renderer's frame.
  void Copied(const rex::x360::RegistrosCopia& copy, bool done) override {
    if (!StepFrame()) return;
    const uint32_t step = step_++;
    const bool resolve = (copy.rb_copy_control >> 20 & 0x3) != 3;  // copy_command 3 = null (a clear only)
    Line("step {}: copy control {:08X} dest {:08X} pitch {:08X} info {:08X} surface {:08X} color0 {:08X}{}", step,
         copy.rb_copy_control, copy.rb_copy_dest_base, copy.rb_copy_dest_pitch, copy.rb_copy_dest_info,
         copy.rb_surface_info, copy.rb_color_info[0], done ? "" : " REJECTED");
    if (!resolve || !done) return;
    rex::x360::TexturaSwap swap;
    swap.dword[1] = copy.rb_copy_dest_base;
    auto* presenter = graphics_->presenter();
    rex::ui::RawImage image;
    if (destinos_->Presentar(presenter, swap, 0, 0) && presenter->CaptureGuestOutput(image)) {
      WritePng(out_ / fmt::format("step{:03}.png", step), image);
    }
  }

  // Step mode: each draw of that frame, with what it had bound (report.txt), to read along the step images.
  // The last page of physical memory: the recordings seen read nothing that high.
  uint32_t ClearRectangle() override { return uint32_t(kPhysicalSize) - 0x1000; }

  void BeforeDraw(std::vector<uint32_t>& r) override {
    if (!StepFrame() || !plain_.count(draw_step_)) return;
    namespace g = rex::graphics;
    saved_ = {r[g::XE_GPU_REG_RB_DEPTHCONTROL], r[g::XE_GPU_REG_RB_BLENDCONTROL0], r[g::XE_GPU_REG_RB_COLORCONTROL]};
    r[g::XE_GPU_REG_RB_DEPTHCONTROL] = 0;
    r[g::XE_GPU_REG_RB_BLENDCONTROL0] = 0x00010001;
    r[g::XE_GPU_REG_RB_COLORCONTROL] &= ~uint32_t(0x8);
    restore_ = true;
    saved_fetch_ = r[g::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 1];
    if (!REXCVAR_GET(mgrr_replay_plain_texture).empty()) {
      const uint32_t address = uint32_t(std::stoul(REXCVAR_GET(mgrr_replay_plain_texture), nullptr, 16));
      r[g::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 1] = (saved_fetch_ & 0xFFF) | (address & ~0xFFFu);
      decoder_->Invalidate();
    }
    if (const int c = REXCVAR_GET(mgrr_replay_plain_constant); c >= 0 && c < 256) {
      const uint32_t at = g::XE_GPU_REG_SHADER_CONSTANT_000_X + uint32_t(c) * 4;
      std::copy_n(&r[at], 4, saved_constant_.begin());
      std::fill_n(&r[at], 4, 0x3F800000u);
      decoder_->Invalidate();
    }
  }

  void Drew(const rex::x360::EntradaShader* vs, const rex::x360::EntradaShader* ps, const char* outcome) override {
    if (!StepFrame()) return;
    namespace g = rex::graphics;
    if (restore_) {
      std::vector<uint32_t>& w = decoder_->registers();
      w[g::XE_GPU_REG_RB_DEPTHCONTROL] = saved_[0];
      w[g::XE_GPU_REG_RB_BLENDCONTROL0] = saved_[1];
      w[g::XE_GPU_REG_RB_COLORCONTROL] = saved_[2];
      w[g::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 1] = saved_fetch_;
      restore_ = false;
      if (const int c = REXCVAR_GET(mgrr_replay_plain_constant); c >= 0 && c < 256) {
        std::copy_n(saved_constant_.begin(), 4, &w[g::XE_GPU_REG_SHADER_CONSTANT_000_X + uint32_t(c) * 4]);
        decoder_->Invalidate();
      }
    }
    const std::vector<uint32_t>& r = decoder_->registers();
    mgrr::shaders::Shaders& library = mgrr::shaders::Library();
    const uint32_t initiator = r[g::XE_GPU_REG_VGT_DRAW_INITIATOR];
    std::string textures;
    for (const rex::x360::SamplerShader& sampler : ps ? ps->samplers : std::vector<rex::x360::SamplerShader>{}) {
      const uint32_t* f = &r[g::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + sampler.registro * 6u];
      const uint32_t base = ((f[1] >> 12) << 12) & 0x1FFFFFFF;
      const uint32_t width = (f[2] & 0x1FFF) + 1, height = ((f[2] >> 13) & 0x1FFF) + 1;
      // Whether the guest memory there holds anything (a texture never recorded or never written reads as zeros).
      const size_t scan = std::min<size_t>(size_t(width) * height * 4, 0x40000);
      const uint8_t* data = physical_.get() + base;
      const bool empty = base + scan > kPhysicalSize || std::all_of(data, data + scan, [](uint8_t b) { return !b; });
      textures += fmt::format(" t{}:{:08X}/{}/{}x{}{}", sampler.registro, base, f[1] & 0x3F, width, height,
                              empty ? "/ZERO" : "");
    }
    auto constant = [&](uint32_t index, uint32_t k) {
      float v;
      const uint32_t bits = r[g::XE_GPU_REG_SHADER_CONSTANT_000_X + index * 4 + k];
      std::memcpy(&v, &bits, 4);
      return v;
    };
    Line("  draw {}: vs {} ps {}{} prim {} n {} | surf {:08X} c0 {:08X} z {:08X} mask {:08X} blend {:08X} depth "
         "{:08X} mode {:08X} | ps c0 {:.3g} {:.3g} {:.3g} {:.3g} vs c0 {:.3g} {:.3g} {:.3g} {:.3g} c184 {:.3g} {:.3g} {:.3g} "
         "{:.3g} |{}",
         draw_step_++, vs ? library.Name(*vs) : "?", ps ? library.Name(*ps) : "?",
         *outcome ? fmt::format(" ({})", outcome) : std::string(),
         initiator & 0x3F, initiator >> 16, r[g::XE_GPU_REG_RB_SURFACE_INFO], r[g::XE_GPU_REG_RB_COLOR_INFO],
         r[g::XE_GPU_REG_RB_DEPTH_INFO], r[g::XE_GPU_REG_RB_COLOR_MASK], r[g::XE_GPU_REG_RB_BLENDCONTROL0],
         r[g::XE_GPU_REG_RB_DEPTHCONTROL], r[g::XE_GPU_REG_RB_MODECONTROL], constant(256, 0), constant(256, 1),
         constant(256, 2), constant(256, 3), constant(0, 0), constant(0, 1), constant(0, 2), constant(0, 3),
         constant(184, 0), constant(184, 1), constant(184, 2), constant(184, 3),
         textures);
    if (probes_.count(draw_step_ - 1)) {
      Probe(draw_step_ - 1);
      if (vs) Inspect(*vs);
      if (ps) {
        for (const rex::x360::SamplerShader& sampler : ps->samplers) {
          const uint32_t* f = &decoder_->registers()[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                                                      sampler.registro * 6u];
          Line("    texture fetch {} (type {}): {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", sampler.registro,
               sampler.tipo, f[0], f[1], f[2], f[3], f[4], f[5]);
        }
      }
    }
  }

  // The position stream of a probed draw: the first vertices and where VS c24-c27 (MGR's world-view-projection)
  // puts them, to tell a draw that lands nowhere.
  void Inspect(const rex::x360::EntradaShader& vs) {
    namespace g = rex::graphics;
    const std::vector<uint32_t>& r = decoder_->registers();
    const std::vector<uint32_t>& code = decoder_->vs_microcode();
    auto f32 = [](uint32_t bits) {
      float v;
      std::memcpy(&v, &bits, 4);
      return v;
    };
    float m[4][4];
    for (int i = 0; i < 4; ++i)
      for (int k = 0; k < 4; ++k) m[i][k] = f32(r[g::XE_GPU_REG_SHADER_CONSTANT_000_X + (24 + i) * 4 + k]);
    Line("    PA_SU_SC_MODE_CNTL {:08X} RB_COLORCONTROL {:08X} RB_ALPHA_REF {:.3g} PA_CL_CLIP_CNTL {:08X} PA_CL_VTE_CNTL {:08X} "
         "viewport scale {:.4g} {:.4g} {:.4g} offset {:.4g} {:.4g} {:.4g}",
         r[g::XE_GPU_REG_PA_SU_SC_MODE_CNTL], r[g::XE_GPU_REG_RB_COLORCONTROL], f32(r[g::XE_GPU_REG_RB_ALPHA_REF]),
         r[g::XE_GPU_REG_PA_CL_CLIP_CNTL], r[g::XE_GPU_REG_PA_CL_VTE_CNTL], f32(r[g::XE_GPU_REG_PA_CL_VPORT_XSCALE]),
         f32(r[g::XE_GPU_REG_PA_CL_VPORT_YSCALE]), f32(r[g::XE_GPU_REG_PA_CL_VPORT_ZSCALE]),
         f32(r[g::XE_GPU_REG_PA_CL_VPORT_XOFFSET]), f32(r[g::XE_GPU_REG_PA_CL_VPORT_YOFFSET]),
         f32(r[g::XE_GPU_REG_PA_CL_VPORT_ZOFFSET]));
    Line("    c24-27: [{:.3g} {:.3g} {:.3g} {:.3g}] [{:.3g} {:.3g} {:.3g} {:.3g}] [{:.3g} {:.3g} {:.3g} {:.3g}] [{:.3g} {:.3g} "
         "{:.3g} {:.3g}]",
         m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3], m[2][0], m[2][1], m[2][2], m[2][3],
         m[3][0], m[3][1], m[3][2], m[3][3]);
    // Words D3D changed outside the vertex fetches (exports it moved or dropped).
    std::string changed;
    for (size_t w = 0; w < code.size() && w < vs.microcodigo.size(); ++w) {
      bool fetch = false;
      for (const rex::x360::ElementoVertice& el : vs.elementos) fetch |= w / 3 == el.instruccion;
      if (!fetch && code[w] != vs.microcodigo[w]) {
        changed += fmt::format(" [{}.{}] {:08X}->{:08X}", w / 3, w % 3, vs.microcodigo[w], code[w]);
      }
    }
    Line("    VS words changed by D3D:{}", changed.empty() ? " none" : changed);
    for (const rex::x360::ElementoVertice& el : vs.elementos) {
      const size_t i = size_t(el.instruccion) * 3;
      if (i + 2 >= code.size()) continue;
      Line("    element usage {} index {} at instruction {}: patched {:08X} {:08X} {:08X}", el.uso, el.indice_uso,
           el.instruccion, code[i], code[i + 1], code[i + 2]);
      if (el.uso == 5) {  // D3DDECLUSAGE_TEXCOORD: its first values
        const uint32_t ts = ((code[i] >> 20) & 0x1F) * 3 + ((code[i] >> 25) & 0x3);
        const uint32_t* tf = &r[g::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (ts / 3) * 6 + (ts % 3) * 2];
        const uint32_t ta = tf[0] & 0x1FFFFFFC, tstride = (code[i + 2] & 0xFF) * 4;
        const int32_t toff = (int32_t(code[i + 2] << 1) >> 9) * 4;
        const uint32_t first = r[g::XE_GPU_REG_VGT_INDX_OFFSET] & 0xFFFFFF;
        std::string values;
        for (uint32_t v = 0; v < 4 && tstride; ++v) {
          const uint8_t* p = physical_.get() + ta + size_t(first + v) * tstride + toff;
          values += fmt::format(" ({:.4g} {:.4g})", f32(rex::memory::load_and_swap<uint32_t>(p)),
                                f32(rex::memory::load_and_swap<uint32_t>(p + 4)));
        }
        Line("    texcoord: slot {} buffer {:08X} stride {} offset {}:{}", ts, ta, tstride, toff, values);
      }
      if (el.uso != 0) continue;  // D3DDECLUSAGE_POSITION
      const uint32_t slot = ((code[i] >> 20) & 0x1F) * 3 + ((code[i] >> 25) & 0x3);
      const uint32_t* fetch = &r[g::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (slot / 3) * 6 + (slot % 3) * 2];
      const uint32_t address = fetch[0] & 0x1FFFFFFC, stride = (code[i + 2] & 0xFF) * 4;
      const int32_t offset = (int32_t(code[i + 2] << 1) >> 9) * 4;
      Line("    position: slot {} buffer {:08X} ({} bytes) stride {} offset {} format {}", slot, address,
           (fetch[1] >> 2 & 0xFFFFFF) * 4, stride, offset, (code[i + 1] >> 16) & 0x3F);
      const uint32_t first = r[g::XE_GPU_REG_VGT_INDX_OFFSET] & 0xFFFFFF;  // auto-indexed draws start there
      Line("    first vertex {}", first);
      for (uint32_t v = 0; v < 4 && stride; ++v) {
        const uint8_t* p = physical_.get() + address + size_t(first + v) * stride + offset;
        float x[4] = {0, 0, 0, 1};
        for (int k = 0; k < 3; ++k) x[k] = f32(rex::memory::load_and_swap<uint32_t>(p + k * 4));
        float c[4];
        for (int row = 0; row < 4; ++row) c[row] = m[row][0] * x[0] + m[row][1] * x[1] + m[row][2] * x[2] + m[row][3] * x[3];
        Line("    v{}: ({:.4g} {:.4g} {:.4g}) -> clip ({:.4g} {:.4g} {:.4g} {:.4g}) ndc ({:.3g} {:.3g} {:.3g})", v, x[0], x[1],
             x[2], c[0], c[1], c[2], c[3], c[0] / c[3], c[1] / c[3], c[2] / c[3]);
      }
    }
  }

  // The current color target 0, resolved (as a 1280x720 8-bit copy) to a scratch address in the last 16 MB of
  // physical memory, then presented and saved.
  void Probe(uint32_t draw) {
    rex::x360::RegistrosCopia copy = rex::x360::LeerRegistrosCopia(decoder_->registers().data());
    const uint32_t rectangle = ClearRectangle();
    const float w = float(copy.rb_surface_info & 0x3FFF), h = 720.0f;
    const float rect[6] = {0.0f, 0.0f, w, 0.0f, 0.0f, h};
    std::memcpy(physical_.get() + rectangle, rect, sizeof(rect));
    copy.fetch_vertices[0] = rectangle | uint32_t(rex::graphics::xenos::FetchConstantType::kVertex);
    copy.fetch_vertices[1] = (6u << 2) | uint32_t(rex::graphics::xenos::Endian::kNone);
    copy.pa_sc_window_offset = 0;
    copy.pa_sc_window_scissor_tl = 0x80000000u;
    copy.pa_sc_window_scissor_br = 0x3FFF3FFFu;
    copy.pa_su_sc_mode_cntl &= ~(1u << 16);
    copy.rb_copy_control = 0x00100000;  // convert, from color target 0
    copy.rb_copy_dest_base = kProbeBase;
    copy.rb_copy_dest_pitch = (720u << 16) | uint32_t(w);
    copy.rb_copy_dest_info = 0x01000302;  // as the game's own 1280x720 resolves
    if (!destinos_->Copiar(copy)) {
      Line("probe after draw {}: the copy was rejected", draw);
      return;
    }
    rex::x360::TexturaSwap swap;
    swap.dword[1] = kProbeBase;
    auto* presenter = graphics_->presenter();
    rex::ui::RawImage image;
    if (destinos_->Presentar(presenter, swap, 0, 0) && presenter->CaptureGuestOutput(image)) {
      WritePng(out_ / fmt::format("probe{:03}.png", draw), image);
      Line("probe after draw {}: probe{:03}.png", draw, draw);
    }
  }

  bool StepFrame() const {
    return pass_ == std::max(1, REXCVAR_GET(mgrr_replay_passes)) && int(frame_) == REXCVAR_GET(mgrr_replay_steps);
  }

  void Swap(uint32_t width, uint32_t height) override {
    ++counter_;
    auto* presenter = graphics_->presenter();
    const bool presented =
        destinos_->Presentar(presenter, rex::x360::LeerTexturaSwap(decoder_->registers().data()), width, height);
    const rex::x360::EstadisticasDibujos s = destinos_->EstadisticasDeDibujos();
    std::string causes;
    for (size_t i = 0; i < s.causas.size() && i < 8; ++i) {
      causes += fmt::format(" {}x{}", s.causas[i].first, s.causas[i].second);
    }
    const mgrr::pm4::Stats& d = decoder_->stats();
    Line("pass {} frame {}: {} draws ({} without shaders), renderer drew {} rejected {} (totals, cause x count:{}), "
         "pipelines {}, textures {}; shader loads (totals): vs {} (by address {}, microcode {}, unknown {}), "
         "ps {} (unknown {}){}",
         pass_, frame_, d.draws - frame_draws_, d.draws_without_shaders - frame_without_shaders_, s.dibujados,
         s.rechazados, causes, s.pipelines, s.texturas, d.vertex_loads, d.vertex_by_address, d.vertex_by_microcode,
         d.vertex_unknown, d.pixel_loads, d.pixel_unknown, presented ? "" : ", NOT PRESENTED");
    frame_draws_ = d.draws;
    frame_without_shaders_ = d.draws_without_shaders;
    rex::ui::RawImage image;
    if (presenter && presenter->CaptureGuestOutput(image)) {
      const auto png = out_ / fmt::format("pass{}_frame{}.png", pass_, frame_);
      if (!WritePng(png, image)) Line("could not write {}", png.string());
    } else {
      Line("pass {} frame {}: no capture", pass_, frame_);
    }
    ++frame_;
    // The renderer starts a dump at the Presentar after the request and ends it at the next one: asked here, one
    // frame ahead, it covers frame mgrr_replay_dump.
    if (pass_ == std::max(1, REXCVAR_GET(mgrr_replay_passes)) && int(frame_) + 1 == REXCVAR_GET(mgrr_replay_dump)) {
      destinos_->PedirVolcado();
    }
  }

 private:
  template <typename... Args>
  void Line(fmt::format_string<Args...> format, Args&&... args) {
    const std::string text = fmt::format(format, std::forward<Args>(args)...);
    REXLOG_INFO("[replay] {}", text);
    if (report_) {
      std::fprintf(report_, "%s\n", text.c_str());
      std::fflush(report_);
    }
  }

  bool Setup(const std::filesystem::path& trace) {
    if (!graphics_ || !graphics_->provider() || !graphics_->presenter()) {
      Line("no deko3d presenter");
      return false;
    }
    physical_.reset(new (std::nothrow) uint8_t[kPhysicalSize + kPhysicalSlack]);
    if (!physical_) {
      Line("cannot allocate {} MB of physical memory", (kPhysicalSize + kPhysicalSlack) >> 20);
      return false;
    }
    std::memset(physical_.get(), 0, kPhysicalSize + kPhysicalSlack);
    memory_ = std::make_unique<rex::memory::Memory>();
    memory_->InitializePhysicalOnly(physical_.get());
    destinos_ = rex::x360::CrearDestinosDeko3d(
        static_cast<rex::deko3d::ProveedorDeko3d*>(graphics_->provider()), memory_.get());
    if (!destinos_) {
      Line("no renderer");
      return false;
    }
    decoder_ = std::make_unique<mgrr::pm4::Decoder>(memory_.get(), *this);
    mgrr::shaders::Shaders& library = mgrr::shaders::Library();
    if (!library.loaded()) {
      Line("no shader library");
      return false;
    }
    // "physical bytes container_hash name", hex (mgrr_shader_hooks.h WriteCopies).
    const std::string table = trace.string() + ".shaders";
    FILE* f = std::fopen(table.c_str(), "r");
    uint32_t copies = 0, unknown = 0;
    if (f) {
      char line[512];
      while (std::fgets(line, sizeof(line), f)) {
        unsigned physical = 0, bytes = 0;
        unsigned long long hash = 0;
        if (line[0] == '#' || std::sscanf(line, "%x %x %llx", &physical, &bytes, &hash) != 3) continue;
        const rex::x360::EntradaShader* entry = library.FindContainerHash(hash);
        if (entry) {
          mgrr::shaders::AddCopy(physical, bytes, entry);
          ++copies;
        } else {
          ++unknown;
        }
      }
      std::fclose(f);
    }
    Line("shader table {}: {} copies, {} not in the library", table, copies, unknown);
    return true;
  }

  // One pass over the recording. Blobs are found again by their file offset, so memory holds only physical_.
  bool Pass(const std::filesystem::path& trace) {
    FILE* f = std::fopen(trace.string().c_str(), "rb");
    if (!f) {
      Line("cannot open {}", trace.string());
      return false;
    }
    char magic[8];
    uint32_t version = 0;
    if (std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, "GPUTRACE", 8) || std::fread(&version, 4, 1, f) != 1 ||
        version != 1) {
      Line("not a version 1 GPU recording");
      std::fclose(f);
      return false;
    }
    frame_ = 0;
    step_ = 0;
    draw_step_ = 0;
    probes_.clear();
    for (const std::string& item : SplitList(REXCVAR_GET(mgrr_replay_probes))) probes_.insert(std::stoul(item));
    plain_.clear();
    for (const std::string& item : SplitList(REXCVAR_GET(mgrr_replay_plain))) plain_.insert(std::stoul(item));
    std::vector<uint8_t> words;
    bool ended = false;
    for (int tag; !ended && (tag = std::fgetc(f)) != EOF;) {
      switch (tag) {
        case 'R': {
          uint32_t count = 0;
          if (std::fread(&count, 4, 1, f) != 1) return Truncated(f);
          std::vector<uint32_t> values(count);
          if (std::fread(values.data(), 4, count, f) != count) return Truncated(f);
          decoder_->LoadRegisters(values.data(), count);
          break;
        }
        case 'B': {
          uint64_t hash = 0;
          uint32_t size = 0;
          if (std::fread(&hash, 8, 1, f) != 1 || std::fread(&size, 4, 1, f) != 1) return Truncated(f);
          blobs_[hash] = {std::ftell(f), size};
          std::fseek(f, long(size), SEEK_CUR);
          break;
        }
        case 'M': {
          uint32_t address = 0, size = 0;
          uint64_t hash = 0;
          if (std::fread(&address, 4, 1, f) != 1 || std::fread(&size, 4, 1, f) != 1 ||
              std::fread(&hash, 8, 1, f) != 1) {
            return Truncated(f);
          }
          const auto it = blobs_.find(hash);
          if (it == blobs_.end() || address + uint64_t(size) > kPhysicalSize + kPhysicalSlack) break;
          const long back = std::ftell(f);
          std::fseek(f, it->second.offset, SEEK_SET);
          const size_t read = std::fread(physical_.get() + address, 1, std::min(size, it->second.size), f);
          std::fseek(f, back, SEEK_SET);
          if (read != std::min(size, it->second.size)) return Truncated(f);
          break;
        }
        case 'P': {
          uint32_t count = 0;
          if (std::fread(&count, 4, 1, f) != 1) return Truncated(f);
          words.resize(size_t(count) * 4);
          if (std::fread(words.data(), 4, count, f) != count) return Truncated(f);
          mgrr::pm4::Reader reader;
          reader.base = words.data();
          reader.end = count;
          while (reader.Pending() && decoder_->Packet(reader)) {
          }
          break;
        }
        case 'S': {
          uint32_t index = 0;
          if (std::fread(&index, 4, 1, f) != 1) return Truncated(f);
          break;
        }
        case 'E':
          ended = true;
          break;
        default:
          Line("unknown record {:02X} at {}", tag, std::ftell(f) - 1);
          std::fclose(f);
          return false;
      }
    }
    std::fclose(f);
    if (!ended) Line("pass {}: no end record (truncated recording)", pass_);
    return true;
  }

  bool Truncated(FILE* f) {
    Line("truncated recording at {}", std::ftell(f));
    std::fclose(f);
    return false;
  }

  struct Blob {
    long offset;
    uint32_t size;
  };

  rex::system::IGraphicsSystem* graphics_;
  rex::ui::WindowedAppContext* app_context_;
  std::filesystem::path out_;
  FILE* report_ = nullptr;
  std::unique_ptr<uint8_t[]> physical_;
  std::unique_ptr<rex::memory::Memory> memory_;
  std::unique_ptr<rex::x360::DestinosNativos> destinos_;
  std::unique_ptr<mgrr::pm4::Decoder> decoder_;
  std::unordered_map<uint64_t, Blob> blobs_;
  uint32_t counter_ = 0;
  int pass_ = 0;
  uint32_t frame_ = 0;
  uint32_t step_ = 0;
  uint32_t draw_step_ = 0;
  std::set<uint32_t> probes_;
  std::set<uint32_t> plain_;  // mgrr_replay_plain
  std::array<uint32_t, 3> saved_{};
  std::array<uint32_t, 4> saved_constant_{};
  uint32_t saved_fetch_ = 0;
  bool restore_ = false;
  static constexpr uint32_t kProbeBase = 0x1F000000;
  uint64_t frame_draws_ = 0, frame_without_shaders_ = 0;
};

}  // namespace

bool Enabled() { return !REXCVAR_GET(mgrr_replay).empty(); }

void Start(rex::system::IGraphicsSystem* graphics, rex::ui::WindowedAppContext* app_context) {
  static std::unique_ptr<Replay> replay;
  static std::unique_ptr<rex::thread::Thread> thread;
  replay = std::make_unique<Replay>(graphics, app_context);
  rex::thread::Thread::CreationParameters params;
  params.stack_size = 1024 * 1024;
  thread = rex::thread::Thread::Create(params, [] { replay->Run(); });
}

}  // namespace mgrr::replay
