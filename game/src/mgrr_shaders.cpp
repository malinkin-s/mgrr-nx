// See mgrr_shaders.h. Structure after xbox360-nx nfsmw_pal/src/nfsmw_nativo_shaders.cpp (GPL-3), for the 2008
// container layout MGR uses (XenosRecomp shader.h).

#include "mgrr_shaders.h"

#include "mgrr_shader_library.h"

#include <algorithm>
#include <exception>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <rex/logging.h>

namespace mgrr::shaders {
namespace {

using rex::x360::ElementoVertice;
using rex::x360::EntradaShader;
using rex::x360::SamplerShader;

// Bits of a vertex fetch instruction D3D does not touch (XenosRecomp shader_code.h VertexFetchInstruction):
// opcode, source and destination registers, destination swizzle and predicate. The fetch constant, format,
// stride, offset and numeric modes come from the vertex declaration. Same as NFS.
constexpr uint32_t kFetchKeep[3] = {0x0007FFFF, 0x80000FFF, 0x80000000};

struct Bytes {
  std::span<const uint8_t> b;
  bool Has(size_t pos, size_t n) const { return pos <= b.size() && n <= b.size() - pos; }
  uint32_t U32(size_t p) const {
    return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 8 | b[p + 3];
  }
  uint16_t U16(size_t p) const { return uint16_t(uint32_t(b[p]) << 8 | b[p + 1]); }
};

uint64_t HashWords(std::span<const uint32_t> w) {
  return HashBytes({reinterpret_cast<const uint8_t*>(w.data()), w.size_bytes()});
}

// OpKill, OpTerminateInvocation and OpDemoteToHelperInvocation; 2 (the conservative answer) if the module cannot
// be walked. Every translated pixel shader has one for the alpha test; more means it really discards.
uint32_t CountKills(std::span<const uint32_t> spirv) {
  if (spirv.size() < 5 || spirv[0] != 0x07230203u) return 2;
  uint32_t kills = 0;
  for (size_t i = 5; i < spirv.size();) {
    const uint32_t words = spirv[i] >> 16, op = spirv[i] & 0xFFFF;
    if (!words || i + words > spirv.size()) return 2;
    kills += op == 252 || op == 4416 || op == 5380;
    i += words;
  }
  return kills;
}

struct RawKey {
  uint64_t hash;
  uint32_t words;
  bool vertex;
  bool operator==(const RawKey&) const = default;
};
struct RawKeyHash {
  size_t operator()(const RawKey& k) const { return size_t(k.hash ^ (uint64_t(k.words) << 1) ^ k.vertex); }
};

}  // namespace

const char* ReadContainer(std::span<const uint8_t> container, EntradaShader& e) {
  const Bytes c{container};
  if (!c.Has(0, 36)) return "container too short";
  const uint32_t flags = c.U32(0);
  const uint32_t virtual_size = c.U32(4);
  const uint32_t constant_table = c.U32(16);
  const uint32_t shader = c.U32(24);
  e.vertices = (flags & 1) != 0;
  if (!c.Has(shader, e.vertices ? 0x24 : 0x20) || shader + 0x20 > virtual_size)
    return "shader header outside the virtual part";

  // Microcode: Shader.physicalOffset/size (+0, +4), after the virtual part.
  const uint32_t code_offset = c.U32(shader), code_bytes = c.U32(shader + 4);
  if (!code_bytes || code_bytes % 12 || !c.Has(size_t(virtual_size) + code_offset, code_bytes))
    return "microcode outside the container";
  e.microcodigo.resize(code_bytes / 4);
  for (size_t i = 0; i < e.microcodigo.size(); ++i)
    e.microcodigo[i] = c.U32(size_t(virtual_size) + code_offset + i * 4);

  if (e.vertices) {
    // VertexShader: field18 (+0x18) words, then vertexElementCount (+0x1C) elements, from +0x24.
    const uint32_t skip = c.U32(shader + 0x18), count = c.U32(shader + 0x1C);
    const size_t first = size_t(shader) + 0x24 + size_t(skip) * 4;
    if (count > 64 || skip > 1024 || !c.Has(first, size_t(count) * 4) || first + count * 4 > virtual_size)
      return "vertex elements outside the virtual part";
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t v = c.U32(first + size_t(i) * 4);
      ElementoVertice el;
      el.instruccion = uint16_t(v & 0xFFF);
      el.uso = uint8_t((v >> 12) & 0xF);
      el.indice_uso = uint8_t((v >> 16) & 0xF);
      if ((size_t(el.instruccion) + 1) * 3 > e.microcodigo.size()) return "vertex element outside the microcode";
      e.elementos.push_back(el);
      for (size_t j = 0; j < 3; ++j) e.microcodigo[size_t(el.instruccion) * 3 + j] &= kFetchKeep[j];
    }
  } else {
    e.salidas = c.U32(shader + 0x1C);  // PixelShader.outputs
  }

  // Constant table (D3DX CTAB after a size word): float registers read and the samplers.
  if (!constant_table || !c.Has(constant_table + 4, 28)) return "no constant table";
  const size_t base = size_t(constant_table) + 4;
  const uint32_t constants = c.U32(base + 12), info = c.U32(base + 16);
  if (constants > 1024 || !c.Has(base + info, size_t(constants) * 20)) return "constant table outside the container";
  // The highest float register read, or the end of the buffer for an array with relative indexing
  // (XenosRecomp's tail: 256 registers in VS, 224 in PS).
  uint32_t floats = 0;
  for (uint32_t i = 0; i < constants; ++i) {
    const size_t p = base + info + size_t(i) * 20;
    if (c.U16(p + 4) != 2) continue;  // RegisterSet::Float4
    const uint32_t index = c.U16(p + 6), n = c.U16(p + 8);
    floats = std::max(floats, n > 1 ? (e.vertices ? 256u : 224u) : index + 1);
  }
  e.constantes_bytes = std::min<uint32_t>(std::max<uint32_t>(floats, 1), 256) * 16;
  for (uint32_t i = 0; i < constants; ++i) {
    const size_t p = base + info + size_t(i) * 20;
    if (c.U16(p + 4) != 3) continue;  // RegisterSet::Sampler
    SamplerShader s;
    s.registro = c.U16(p + 6);
    const uint32_t type = c.U32(p + 12);
    s.tipo = c.Has(base + type, 4) ? c.U16(base + type + 2) : 0;
    e.samplers.push_back(s);
  }
  e.huella = HashWords(e.microcodigo);
  return nullptr;
}

struct Shaders::Data {
  std::vector<LibraryShader> library;
  std::vector<EntradaShader> entries;           // same order as library, minus the ones that failed
  std::vector<uint32_t> entry_library;          // entries[i] comes from library[entry_library[i]]
  std::unordered_map<uint64_t, uint32_t> by_container;  // container hash -> entry
  std::map<std::pair<bool, uint32_t>, std::vector<uint32_t>> by_length;  // (vertex, words) -> entries
  std::mutex lock;  // cache and stats
  std::unordered_map<RawKey, const EntradaShader*, RawKeyHash> cache;
  std::vector<uint32_t> scratch;
  Stats stats;
  bool loaded = false;
};

Shaders::Shaders() : data_(std::make_unique<Data>()) {}
Shaders::~Shaders() = default;

bool Shaders::loaded() const { return data_->loaded; }
size_t Shaders::size() const { return data_->entries.size(); }

bool Shaders::Load(const std::filesystem::path& path) {
  Data& d = *data_;
  try {
    d.library = LoadLibrary(path);
  } catch (const std::exception& e) {
    REXLOG_WARN("[shaders] library not available ({}): {}", path.string(), e.what());
    return false;
  }
  d.entries.clear();
  d.entry_library.clear();
  d.entries.reserve(d.library.size());
  uint32_t vertex = 0, pixel = 0, discarding = 0, skipped = 0;
  for (uint32_t i = 0; i < d.library.size(); ++i) {
    const LibraryShader& s = d.library[i];
    EntradaShader e;
    if (const char* reason = ReadContainer(s.container, e)) {
      REXLOG_WARN("[shaders] {} skipped: {}", s.name, reason);
      ++skipped;
      continue;
    }
    e.spirv = s.spirv;
    e.juego = &s;
    e.numero = uint32_t(d.entries.size());
    e.huella_contenedor = s.container_hash;
    if (!e.vertices) {
      e.kills = CountKills(s.spirv);
      e.descarta = e.kills != 1;  // 1 = only the alpha test's
      discarding += e.descarta;
    }
    (e.vertices ? vertex : pixel) += 1;
    d.entries.push_back(std::move(e));
    d.entry_library.push_back(i);
  }
  d.by_container.clear();
  d.by_length.clear();
  for (uint32_t i = 0; i < d.entries.size(); ++i) {
    const EntradaShader& e = d.entries[i];
    d.by_container.emplace(e.huella_contenedor, i);
    d.by_length[{e.vertices, uint32_t(e.microcodigo.size())}].push_back(i);
  }
  d.cache.clear();
  d.loaded = !d.entries.empty();
  REXLOG_INFO("[shaders] library {}: {} shaders ({} vertex, {} pixel, {} of them discard pixels), {} skipped",
              path.filename().string(), d.entries.size(), vertex, pixel, discarding, skipped);
  return d.loaded;
}

const EntradaShader* Shaders::FindContainerHash(uint64_t hash) const {
  const Data& d = *data_;
  if (!d.loaded) return nullptr;
  const auto it = d.by_container.find(hash);
  return it != d.by_container.end() ? &d.entries[it->second] : nullptr;
}

const EntradaShader* Shaders::FindContainer(std::span<const uint8_t> container) {
  Data& d = *data_;
  if (!d.loaded) return nullptr;
  const auto it = d.by_container.find(HashBytes(container));
  const EntradaShader* e = nullptr;
  if (it != d.by_container.end()) {
    const LibraryShader& s = d.library[d.entry_library[it->second]];
    if (s.container.size() == container.size() && std::equal(container.begin(), container.end(), s.container.begin()))
      e = &d.entries[it->second];
  }
  std::lock_guard<std::mutex> guard(d.lock);
  ++d.stats.containers_seen;
  d.stats.containers_found += e != nullptr;
  return e;
}

const EntradaShader* Shaders::Identify(bool vertex, std::span<const uint32_t> microcode) {
  Data& d = *data_;
  if (!d.loaded) return nullptr;
  const RawKey key{HashWords(microcode), uint32_t(microcode.size()), vertex};
  std::lock_guard<std::mutex> guard(d.lock);
  if (auto it = d.cache.find(key); it != d.cache.end()) return it->second;
  ++d.stats.microcode_lookups;
  const EntradaShader* chosen = nullptr;
  bool ambiguous = false;
  if (auto c = d.by_length.find({vertex, uint32_t(microcode.size())}); c != d.by_length.end()) {
    for (uint32_t index : c->second) {
      const EntradaShader& e = d.entries[index];
      d.scratch.assign(microcode.begin(), microcode.end());
      for (const ElementoVertice& el : e.elementos)
        for (size_t j = 0; j < 3; ++j) d.scratch[size_t(el.instruccion) * 3 + j] &= kFetchKeep[j];
      if (d.scratch != e.microcodigo) continue;
      if (!chosen) {
        chosen = &e;
      } else if (chosen->spirv.size() != e.spirv.size() ||
                 !std::equal(e.spirv.begin(), e.spirv.end(), chosen->spirv.begin())) {
        ambiguous = true;
      }
    }
  }
  d.stats.microcode_found += chosen != nullptr;
  d.stats.microcode_ambiguous += ambiguous;
  d.cache.emplace(key, chosen);
  return chosen;
}

const char* Shaders::Name(const EntradaShader& entry) const {
  return static_cast<const LibraryShader*>(entry.juego)->name.c_str();
}

Stats Shaders::stats() const {
  std::lock_guard<std::mutex> guard(data_->lock);
  return data_->stats;
}

}  // namespace mgrr::shaders
