// mgrr_pack_shaders: MGR's shader containers + their SPIR-V -> the shader library, checked the way the game reads it.
//
//   mgrr_pack_shaders OUT.mgsl SPIRV_DIR [--xsh FILE] CONTAINER...
//
// CONTAINER is <dat>/<file>.vso|.pso (tools/pgdat.py layout); its SPIR-V is SPIRV_DIR/<v|p>_<dat>.<file>.spv
// (mgrr_hlsl + DXC). A container without SPIR-V (translation or DXC failed) is listed and left out.
// After writing, the library is loaded back with game/src/mgrr_shaders.cpp: every entry must parse, and every
// container must be found by its bytes.
// --xsh: Xenia's shader storage of a PC run (~/.local/share/mgrr/cache/shaders/shareable/4B4E080A.xsh), the
// microcode exactly as the game loaded it. Each one is looked up with Shaders::Identify, as the ring reader will.

#include "mgrr_shader_library.h"
#include "mgrr_shaders.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mgrr::shaders;

namespace {

std::vector<uint8_t> ReadAll(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Xenia's shader storage (vulkan pipeline_cache.cpp): "XESH", version, then records of u64 hash,
// u32 (dword count : 31, type : 1; 0 vertex, 1 pixel) and the microcode as guest big-endian dwords.
void CheckXsh(Shaders& shaders, const fs::path& path) {
  const std::vector<uint8_t> x = ReadAll(path);
  size_t p = 8;
  std::map<std::string, uint32_t> counts;
  while (p + 12 <= x.size()) {
    uint32_t cw = 0;
    for (int i = 0; i < 4; ++i) cw |= uint32_t(x[p + 8 + i]) << (8 * i);
    p += 12;
    const uint32_t n = cw & 0x7FFFFFFF;
    const bool vertex = (cw >> 31) == 0;
    if (p + size_t(n) * 4 > x.size()) break;
    std::vector<uint32_t> code(n);
    for (uint32_t i = 0; i < n; ++i, p += 4)
      code[i] = uint32_t(x[p]) << 24 | uint32_t(x[p + 1]) << 16 | uint32_t(x[p + 2]) << 8 | x[p + 3];
    const auto* e = shaders.Identify(vertex, code);
    counts[std::string(vertex ? "vertex" : "pixel") + (e ? " found" : " not found")]++;
  }
  std::printf("xsh %s:", path.filename().string().c_str());
  for (const auto& [k, v] : counts) std::printf(" %s %u;", k.c_str(), v);
  std::printf(" ambiguous %llu\n", (unsigned long long)shaders.stats().microcode_ambiguous);
}

int Run(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: mgrr_pack_shaders OUT.mgsl SPIRV_DIR [--xsh FILE] CONTAINER...\n");
    return 2;
  }
  const fs::path out = argv[1], spirv_dir = argv[2];
  fs::path xsh;
  std::vector<fs::path> inputs;
  for (int i = 3; i < argc; ++i) {
    if (std::string(argv[i]) == "--xsh" && i + 1 < argc) xsh = argv[++i];
    else inputs.emplace_back(argv[i]);
  }

  std::vector<LibraryShader> library;
  size_t missing = 0;
  for (const fs::path& in : inputs) {
    LibraryShader s;
    s.name = in.parent_path().filename().string() + "." + in.stem().string();
    s.container = ReadAll(in);
    const bool vertex = s.container.size() >= 4 && (s.container[3] & 1);
    const std::vector<uint8_t> spv = ReadAll(spirv_dir / ((vertex ? "v_" : "p_") + s.name + ".spv"));
    if (spv.empty() || spv.size() % 4) {
      std::printf("no SPIR-V: %s\n", s.name.c_str());
      ++missing;
      continue;
    }
    s.spirv.resize(spv.size() / 4);
    for (size_t i = 0; i < s.spirv.size(); ++i)
      s.spirv[i] = uint32_t(spv[i * 4]) | uint32_t(spv[i * 4 + 1]) << 8 | uint32_t(spv[i * 4 + 2]) << 16 |
                   uint32_t(spv[i * 4 + 3]) << 24;
    library.push_back(std::move(s));
  }
  const std::vector<uint8_t> packed = PackLibrary(library);
  std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(packed.data()), std::streamsize(packed.size()));
  std::printf("%zu containers: %zu packed, %zu without SPIR-V; %s %zu bytes\n", inputs.size(), library.size(),
              missing, out.filename().string().c_str(), packed.size());

  Shaders shaders;
  if (!shaders.Load(out) || shaders.size() != library.size()) {
    std::printf("FAILED: %zu of %zu entries parsed\n", shaders.size(), library.size());
    return 1;
  }
  size_t found = 0;
  for (const LibraryShader& s : library) found += shaders.FindContainer(s.container) != nullptr;
  std::printf("containers found by their bytes: %zu of %zu\n", found, library.size());
  if (!xsh.empty()) CheckXsh(shaders, xsh);
  return found == library.size() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return Run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
