// mgrr_hlsl: Xbox 360 shader containers of MGR:R -> HLSL, with the vendored XenosRecomp (xenosrecomp/).
//
// MGR's containers are the 2008 layout XenosRecomp reads as is (signature 0x102A1100 pixel, 0x102A1101 vertex),
// so unlike NFS's driver there is no format conversion. Each input is named <dat>/<file>.vso|.pso (the layout
// tools/pgdat.py writes); the output is OUT/v_<dat>.<file>.hlsl or OUT/p_<dat>.<file>.hlsl, the name the library
// packer (mgrr_pack_shaders) expects next to its .spv.
//
//   mgrr_hlsl OUT_DIR shader_common.h CONTAINER...
//   exit code 0 only if every container translated; failures are listed on stdout

#include "xenosrecomp/shader_recompiler.h"

#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> ReadAll(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// A texture fetch from a sampler register the constant table does not name: XenosRecomp calls it sN and declares
// nothing, so DXC fails (MGR: filtershadergather and filtershadergathernoise read s1, and both are on the title
// screen). The game binds a texture there anyway, so it gets the deko3d declaration every named 2D register gets
// (printDeko3dConstants): texture and sampler at binding N, the shadow pair at 16 + N, 1/size in the shared block.
// Only 2D fetches of registers 0-15, the ones deko3d has bindings for. Returns how many were added.
int DeclareUnnamedSamplers(std::string& hlsl) {
  const std::string anchor = "\n#ifndef __spirv__\n[shader(";
  const size_t at = hlsl.find(anchor);
  if (at == std::string::npos) return 0;
  std::string decl;
  int added = 0;
  for (int reg = 0; reg < 16; ++reg) {
    const std::string use = "s" + std::to_string(reg) + "_Texture2DDescriptorIndex";
    const size_t first = hlsl.find(use);
    if (first == std::string::npos || hlsl.find("#define " + use) != std::string::npos) continue;
    // Whole identifier only (s1_ must not match gs1_...).
    if (first > 0 && (std::isalnum(uint8_t(hlsl[first - 1])) || hlsl[first - 1] == '_')) continue;
    const std::string n = "s" + std::to_string(reg), r = std::to_string(reg);
    const uint32_t inv = 360 + reg * 8;
    decl += "[[vk::binding(" + r + ", 0)]] Texture2D<float4> " + n + "_Textura;\n";
    decl += "[[vk::binding(" + r + ", 1)]] SamplerState " + n + "_Muestreo;\n";
    decl += "#define " + n + "_Texture2DDescriptorIndex " + n + "_Textura\n";
    decl += "#define " + n + "_SamplerDescriptorIndex " + n + "_Muestreo\n";
    decl += "[[vk::binding(" + std::to_string(16 + reg) + ", 0)]] Texture2D<float4> " + n + "_Pareja;\n";
    decl += "#define " + n + "_Texture3DDescriptorIndex " + n + "_Pareja\n";
    decl += "#define " + n + "_InvTamano float2(NFSMW_COMPARTIDA_FLOAT(" + std::to_string(inv) +
            "), NFSMW_COMPARTIDA_FLOAT(" + std::to_string(inv + 4) + "))\n";
    ++added;
  }
  if (added) hlsl.insert(at, "\n// mgrr_hlsl: sampler registers missing from the constant table\n#ifdef NFSMW_DEKO3D\n" +
                                 decl + "#endif\n");
  return added;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: mgrr_hlsl OUT_DIR shader_common.h CONTAINER...\n");
    return 2;
  }
  const fs::path out = argv[1];
  fs::create_directories(out);
  const std::vector<uint8_t> common_bytes = ReadAll(argv[2]);
  if (common_bytes.empty()) {
    std::fprintf(stderr, "cannot read %s\n", argv[2]);
    return 2;
  }
  const std::string common(common_bytes.begin(), common_bytes.end());

  size_t ok = 0, failed = 0;
  for (int i = 3; i < argc; ++i) {
    const fs::path in = argv[i];
    const std::string name = in.parent_path().filename().string() + "." + in.stem().string();
    const std::vector<uint8_t> data = ReadAll(in);
    const uint32_t flags = data.size() >= 4 ? uint32_t(data[0]) << 24 | uint32_t(data[1]) << 16 |
                                                  uint32_t(data[2]) << 8 | data[3]
                                            : 0;
    if ((flags & ~1u) != 0x102A1100u) {
      std::printf("%s: not a 2008 shader container (0x%08X)\n", name.c_str(), flags);
      ++failed;
      continue;
    }
    ShaderRecompiler recompiler;
    try {
      recompiler.recompile(data.data(), common);
    } catch (const std::exception& e) {
      std::printf("%s: %s\n", name.c_str(), e.what());
      ++failed;
      continue;
    }
    if (recompiler.out.empty()) {
      std::printf("%s: no output\n", name.c_str());
      ++failed;
      continue;
    }
    if (const int added = DeclareUnnamedSamplers(recompiler.out))
      std::printf("%s: declared %d sampler register(s) missing from the constant table\n", name.c_str(), added);
    std::ofstream(out / ((flags & 1 ? "v_" : "p_") + name + ".hlsl"), std::ios::binary) << recompiler.out;
    ++ok;
  }
  std::printf("%zu containers: %zu translated, %zu failed\n", ok + failed, ok, failed);
  return failed ? 1 : 0;
}
