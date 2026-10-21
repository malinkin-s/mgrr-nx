// See mgrr_shader_library.h.

#include "mgrr_shader_library.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

// xxHash with memcpy reads (XXH_FORCE_MEMORY_ACCESS 0): GCC may otherwise hoist its pointer reads above the
// writes of the data being hashed (strict aliasing; xbox360-nx docs/toolchain.md). Inline, private to this file.
#undef XXH_FORCE_MEMORY_ACCESS
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include <xxhash.h>

namespace mgrr::shaders {
namespace {

constexpr char kMagic[8] = {'M', 'G', 'R', 'S', 'H', 'L', 'I', 'B'};
constexpr size_t kMaxShaders = 8192;
constexpr size_t kMaxName = 256;
constexpr size_t kMaxContainer = 256 * 1024;
constexpr size_t kMaxSpirvWords = 4 * 1024 * 1024 / 4;

void Require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}

uint32_t BigEndian32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// The 2008 container (XenosRecomp shader.h ShaderContainer): signature, virtual size, physical size; the
// physical part (microcode) follows the virtual one and ends the container.
void ValidateShader(const LibraryShader& s) {
  Require(!s.name.empty() && s.name.size() <= kMaxName, "shader name length out of range");
  Require(s.container.size() >= 36 && s.container.size() <= kMaxContainer, "container length out of range");
  const uint32_t flags = BigEndian32(s.container.data());
  Require((flags & ~1u) == 0x102A1100u, "unknown container signature");
  const uint64_t virtual_size = BigEndian32(s.container.data() + 4);
  const uint64_t physical_size = BigEndian32(s.container.data() + 8);
  Require(physical_size && physical_size % 4 == 0 && virtual_size + physical_size == s.container.size(),
          "container sizes do not add up");
  Require(s.spirv.size() >= 5 && s.spirv.size() <= kMaxSpirvWords, "SPIR-V length out of range");
  Require(s.spirv[0] == 0x07230203u, "bad SPIR-V magic");
}

struct Reader {
  std::span<const uint8_t> data;
  size_t pos = 0;
  std::span<const uint8_t> Take(size_t n) {
    Require(n <= data.size() - pos, "shader library truncated");
    auto r = data.subspan(pos, n);
    pos += n;
    return r;
  }
  uint32_t U32() {
    auto p = Take(4);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
  }
};

void PutU32(std::vector<uint8_t>& d, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (i * 8)));
}

}  // namespace

uint64_t HashBytes(std::span<const uint8_t> bytes) {
  return XXH3_64bits(bytes.data(), bytes.size());
}

std::vector<uint8_t> PackLibrary(const std::vector<LibraryShader>& shaders) {
  Require(shaders.size() <= kMaxShaders, "too many shaders");
  std::vector<uint8_t> d(kMagic, kMagic + sizeof(kMagic));
  PutU32(d, kLibraryVersion);
  PutU32(d, uint32_t(shaders.size()));
  for (const LibraryShader& s : shaders) {
    ValidateShader(s);
    PutU32(d, uint32_t(s.name.size()));
    d.insert(d.end(), s.name.begin(), s.name.end());
    PutU32(d, uint32_t(s.container.size()));
    d.insert(d.end(), s.container.begin(), s.container.end());
    PutU32(d, uint32_t(s.spirv.size()));
    for (uint32_t w : s.spirv) PutU32(d, w);
  }
  const uint64_t h = HashBytes(d);
  PutU32(d, uint32_t(h));
  PutU32(d, uint32_t(h >> 32));
  return d;
}

std::vector<LibraryShader> LoadLibrary(std::span<const uint8_t> file) {
  Require(file.size() >= sizeof(kMagic) + 16, "shader library too short");
  Require(std::memcmp(file.data(), kMagic, sizeof(kMagic)) == 0, "not a shader library");
  Reader tail{file.subspan(file.size() - 8)};
  const uint64_t stored = tail.U32() | uint64_t(tail.U32()) << 32;
  Require(stored == HashBytes(file.first(file.size() - 8)), "shader library checksum mismatch");

  Reader r{file.first(file.size() - 8), sizeof(kMagic)};
  Require(r.U32() == kLibraryVersion, "shader library version mismatch");
  const uint32_t count = r.U32();
  Require(count <= kMaxShaders, "too many shaders");
  std::vector<LibraryShader> shaders(count);
  for (LibraryShader& s : shaders) {
    const uint32_t name_bytes = r.U32();
    Require(name_bytes <= kMaxName, "shader name length out of range");
    auto name = r.Take(name_bytes);
    s.name.assign(name.begin(), name.end());
    const uint32_t container_bytes = r.U32();
    Require(container_bytes <= kMaxContainer, "container length out of range");
    auto container = r.Take(container_bytes);
    s.container.assign(container.begin(), container.end());
    const uint32_t words = r.U32();
    Require(words <= kMaxSpirvWords, "SPIR-V length out of range");
    s.spirv.resize(words);
    for (uint32_t& w : s.spirv) w = r.U32();
    ValidateShader(s);
    s.vertex = (BigEndian32(s.container.data()) & 1) != 0;
    s.container_hash = HashBytes(s.container);
  }
  Require(r.pos == r.data.size(), "trailing bytes in shader library");
  return shaders;
}

std::vector<LibraryShader> LoadLibrary(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  Require(bool(f), "cannot open shader library");
  const std::vector<uint8_t> data(std::istreambuf_iterator<char>(f), {});
  return LoadLibrary(data);
}

}  // namespace mgrr::shaders
