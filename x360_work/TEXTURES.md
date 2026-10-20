# Xbox 360 textures in MGR:R — formats, tiling, mips (verified on decoded images)

Source: **8 303 textures** with a fetch constant in data000/data001 (5 049 in `.wtb` + 3 254 in `.wta`; 1.9 GB; index `x360_work/textures_index_all.json`, built by `tools/scan_textures.py`).
Model textures use a split container: **`.wta` (headers, in the `.dat`) + `.wtp` (pixel data, in the `.dtt`)** — the WTA header is the same format as WTB (the offsets/sizes address the WTP; for em0200 the sizes sum to exactly the WTP size).
(An earlier version of this note counted only `.wtb` — 5 049 textures; the format set below is unchanged by adding `.wta`.)
Container: `WTB` (big-endian, magic `00 42 54 57`) = header + arrays of offset/size/flags/id/texinfo; **texinfo is the 52-byte `D3DBaseTexture`**
(Common, RefCount, Fence, ReadFence, Identifier, BaseFlush, MipFlush, then the 6-dword Xenos fetch constant at +28…+48 — exactly what `SetTexture` 0x826AEFD0 copies).
WTBs sit loose in CPK or inside `.dtt` (DAT) members. Parser: `tools/wtb.py`; decoder: `tools/xtex.py`.

## What the game uses (nothing else occurs)
| format | count | note |
|---|---|---|
| DXT1 (k_DXT1 = 18) | 4 323 | endian 8in16 |
| DXT4_5 (20) | 3 817 | endian 8in16 |
| 8_8_8_8 (6) | 70 | endian 8in32, swizzle (2,1,0,3) = BGRA |
| DXT2_3 (19) | 89 | endian 8in16 |
| 8 (A8) | 4 | **untiled**, swizzle (4,4,4,0) = (0,0,0,A), 1024², pitch = width |
2D: 8 297, cube: 6 (16×16 ×6 faces, 4 mips, DXT4_5/8888). No 1D, 3D, float, DXN, 16-bit formats in files. clamp always (0,0,0); all `packed_mips = 1`.
Typical sizes: 512² (1 766), 256² (1 219), 512×256 (739), 1024² (337). Maxwell (Tegra X1) samples BC1/BC2/BC3 natively → upload DXT1/2_3/4_5 as BC1/BC2/BC3 without recompression.
Runtime-only surfaces (render targets, resolve targets, EDRAM formats such as 2_10_10_10 / 16_16_16_16_FLOAT) do not come from files and need separate handling.

## Decoding pipeline (all steps verified visually: CRIWARE logo 8888, Raiden concept art DXT1, window atlas DXT5 + mip chain, DXT3 terrain with alpha, A8 text)
1. **Endian swap** by fetch-constant `endian` (dword1 bits 6–7): 1 = 8in16 (swap bytes in each 16 bit), 2 = 8in32 (reverse each 32 bit), 3 = 16in32.
2. **Untile** (dword0 bit 31 = tiled): work in texel *blocks* (4×4 for DXT, 1×1 else), `pitch_blocks = max(round_up(width_blocks, 32), pitch*32/block_w)`;
   `log2bpp = log2(bytes per block)`; offset = XGAddress2DTiledOffset(x, y, pitch_blocks, log2bpp) — implementation in `tools/xtex.py::tiled_offsets`.
3. **Block decode** (little-endian inside the block) → RGBA, or upload as BCn.
4. **Swizzle**: fetch-constant dword3 bits 1–12 (X,Y,Z,W selectors: 0..3 = channel, 4 = 0, 5 = 1) → `VkComponentMapping`.

## Mip chain
* Level 0 at offset 0; `mip_addr` (dword5 bits 12–31 ×4 KB) = start of level 1 = size of the aligned base level (e.g. 1024² DXT1 → 0x80000). Levels 1…n−1 follow back-to-back, each
  `round_up(wb,32) × round_up(hb,32) × bytes_per_block` (tiled, 32×32-block tiles). Verified by decoding levels 0–2 of a 512² DXT5.
* Levels whose larger dimension is ≤ 16 blocks are **packed into one 32×32-block tail tile** at the end of the chain (first packed level = max(0, ceil(log2(max(wb,hb))) − 4)).
  The in-tile offsets of the tail levels are not decoded yet; plan: upload levels 0…packed−1 and generate the remaining (≤ 64×64 texel) mips on the GPU, or port Xenia's packed-mip offset table.
* Blob size is rounded up beyond the data (allocation padding); non-mipped textures are exactly `round_up(wb,32) × round_up(hb,32) × bpb` (1 309 of 1 309 matched).

## Hook design implication
`SetTexture` gives the guest `D3DBaseTexture*`; its fetch constant fully describes the texture, so a renderer can convert/upload on first bind and cache by (base address, fetch-constant hash).
Textures written by the GPU (resolve) need invalidation when the guest writes the memory.
