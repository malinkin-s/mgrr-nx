// dksh_probe: does each shader of a SPIR-V folder reach deko3d?
//
// Each .spv (v_* vertex, p_* pixel; compiled by DXC with -DNFSMW_DEKO3D) goes the console's way:
// rex::deko3d::SpirvAGlsl (SPIRV-Cross, SDK) then uam::compile (devkitPro's deko3d compiler, vendored in the SDK).
// One TSV line per shader: status, name, compile ms, DKSH bytes, first line of the failure or warning.
//
//   dksh_probe OUT.tsv [--glsl DIR] [--dksh DIR] FILE.spv...
//   exit code 0 only if every shader compiled

#include <rex/ui/deko3d/glsl.h>

#include "uam_api.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// The value nearly every pipeline uses (shader_common.h): constants through UBOs and 1/size of the textures.
constexpr uint32_t kSpecBase = (1u << 8) | (1u << 9);

std::vector<uint32_t> ReadWords(const fs::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  std::vector<uint32_t> words(f ? size_t(f.tellg()) / 4 : 0);
  f.seekg(0);
  f.read(reinterpret_cast<char*>(words.data()), std::streamsize(words.size() * 4));
  return words;
}

std::string FirstLine(const std::string& s) {
  std::string line = s.substr(0, s.find('\n'));
  for (char& c : line)
    if (c == '\t') c = ' ';
  return line.substr(0, 200);
}

int Run(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: dksh_probe OUT.tsv [--glsl DIR] [--dksh DIR] FILE.spv...\n");
    return 2;
  }
  std::ofstream tsv(argv[1]);
  fs::path glsl_dir, dksh_dir;
  std::vector<fs::path> inputs;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--glsl" && i + 1 < argc) glsl_dir = argv[++i];
    else if (a == "--dksh" && i + 1 < argc) dksh_dir = argv[++i];
    else inputs.emplace_back(a);
  }
  uam::init(true);
  size_t ok = 0, glsl_fail = 0, uam_fail = 0, warned = 0;
  double total_ms = 0;
  for (const fs::path& in : inputs) {
    const std::string name = in.stem().string();
    const std::vector<uint32_t> spirv = ReadWords(in);
    const rex::deko3d::GlslDeko g = rex::deko3d::SpirvAGlsl(spirv, kSpecBase);
    if (g.glsl.empty()) {
      ++glsl_fail;
      tsv << "glsl\t" << name << "\t0\t0\t" << FirstLine(g.error) << "\n";
      continue;
    }
    if (!glsl_dir.empty()) std::ofstream(glsl_dir / (name + ".glsl")) << g.glsl;
    const auto t0 = std::chrono::steady_clock::now();
    const uam::Result r = uam::compile(g.vertices ? uam::Stage::Vertex : uam::Stage::Fragment, g.glsl.c_str());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    total_ms += ms;
    if (!r.ok) {
      ++uam_fail;
      tsv << "uam\t" << name << "\t" << ms << "\t0\t" << FirstLine(r.log) << "\n";
      continue;
    }
    ++ok;
    const bool warn = !g.avisos.empty() || !r.log.empty();
    warned += warn;
    if (!dksh_dir.empty())
      std::ofstream(dksh_dir / (name + ".dksh"), std::ios::binary)
          .write(reinterpret_cast<const char*>(r.dksh.data()), std::streamsize(r.dksh.size()));
    tsv << (warn ? "warn\t" : "ok\t") << name << "\t" << ms << "\t" << r.dksh.size() << "\t"
        << FirstLine(!g.avisos.empty() ? g.avisos.front() : r.log) << "\n";
  }
  uam::shutdown();
  std::printf("%zu shaders: %zu compiled (%zu with warnings), %zu failed in GLSL, %zu failed in uam; uam %.0f ms total\n",
              inputs.size(), ok, warned, glsl_fail, uam_fail, total_ms);
  return ok == inputs.size() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  // uam's Mesa front end and nv50_ir recurse deeply: run on a thread with a large stack, as on the console.
  int code = 1;
  std::thread worker([&] { code = Run(argc, argv); });
  worker.join();
  return code;
}
