# Draw calls, render targets and Resolve (Xbox 360 D3D in MGR:R)

## Draw calls — both start with the same state flush (see RENDER_STATE_MAP.md / FETCH_AND_SHADERS.md), then emit a PM4 `DRAW_INDX`

| function | role | args |
|---|---|---|
| **0x826BC928** | `DrawVertices` | (dev, primType r4, startVertex r5, vertexCount r6) |
| **0x826BCD40** | `DrawIndexedVertices` | (dev, primType r4, baseVertex r5, startIndex r6, indexCount r7); index buffer = `[dev+12812]` (SetIndices 0x826B8790) |

* **primType** = the Xenos/D3D value: 1 point list, 2 line list, 3 line strip, 4 triangle list, 5 triangle fan, 6 triangle strip, **8 rect list**, **13 quad list**.
  Table at `0x8218C968` (index primType*8): `{advance, overlap}` = (1,0) (2,0) (1,1) (3,0) (1,2) (1,2) … rect list (3,0), quad list (4,0).
* **Start vertex / base vertex** is written as a type-0 packet to `VGT_INDX_OFFSET` (0x2102, header word `0x00002102`).
* **DrawVertices** packet: `0xC0012201` (type 3, 2 data words, opcode 0x22 `DRAW_INDX`, predicate bit) + `[viz/bin word]` +
  `initiator = (count<<16) | 0x80 (auto-index source) | primType&63`.
* **DrawIndexedVertices** packet: `0xC0032201` (4 data words) + `[viz/bin word]` + `initiator = (count<<16) | primType | (0x800 if 32-bit indices)` +
  `DMA_BASE` = physical address of the index buffer (+ startIndex × 2 or × 4; the usual physical-window fix-up `+512 & 0x1000`) +
  `DMA_SIZE` = index count (+ top bits: endian/swap mode taken from the index buffer header).
* **Index buffer object**: word0 bit 31 = 32-bit indices, bits 30–29 = endian swap mode (feeds `VGT_DMA_SIZE`); base pointer at +24.
* **Splitting**: counts above 65 535 are split into chunks `= (65535/advance) & ~1 × advance`; each next chunk starts `advance-overlap` further. A *predicated* variant (`[dev+11068] & 1`: PM4 `COND_EXEC`
  0xC0006000 wrapper, used for tiled/predicated rendering) emits the same draw twice with extra packets — irrelevant for a non-tiled renderer.
* For the port: topology map 1→POINT_LIST, 2→LINE_LIST, 3→LINE_STRIP, 4→TRIANGLE_LIST, 5→TRIANGLE_FAN (no native fan on some Vulkan implementations: convert), 6→TRIANGLE_STRIP,
  8 (rect list: 3 vertices per rectangle, 4th corner implied) and 13 (quad list) need expansion in the vertex stage or via an index buffer. Indices must be byte-swapped (8in16 for 16-bit, 8in32 for 32-bit; verify on data).

## Render targets / surfaces
* `SetRenderTarget` = **0x826B8ED8** `(dev, index r4, surface r5)`: surface → `[dev+12816+4*index]`, surface+28 → shadow `RB_COLOR_INFO` (+EDRAM base, format bits 16–19), dirty bit set; first call also triggers 0x826B8E48 (default viewport/scissor from the surface size).
* `SetDepthStencilSurface` = **0x826B9268** `(dev, ds)`: `[dev+12832]`, ds+28 → `RB_DEPTH_INFO` shadow (dev+10376), ds+32 → dev+10560.
* Surface object: +24 = size/MSAA word (MSAA type = bits 16–17: 0 none, 1 = 2×, 2 = 4×), +28 = colour/depth info, +32 = HiZ/extra.
* **Formats the engine creates** (engine enum → D3DFORMAT word, function 0x8278DAE8): 0 → **0x18280186** A8R8G8B8 (fmt 6, 8in32), 1 → **0x2DA2ABA4** R32F (fmt 36), 2 → **0x1A215586** (8888 class, differs in upper bits — unidentified variant);
  depth **0x1A220197 = 24_8_FLOAT (D24FS8)**. Other values near the D3D core (0x28280086 X8R8G8B8, 0x28000058 16-bit, …) are format-table entries, not engine requests.
  No 16-bit-float HDR targets requested by the engine code. The creator is 0x826AED70 `(dev?, width, height, formatWord, msaa, out)`; engine wrapper 0x82792740 picks sizes from a runtime table (1280×720, 1024×768, 64…256 squares).
* MSAA 2×/4× is used (EDRAM tiling is handled by the D3D lib and not needed on desktop-class GPUs).

## Resolve (EDRAM → texture copy)
* Core: **0x826B9F78** (1 109 instructions). Thin wrapper **0x826BB0D0**: `(dev, Flags r4, pSourceRect r5, pDestTexture r6, pDestPoint r7, destLevel r8, destSliceOrFace r9, pClearColor r10, clearZ f1, clearStencil / pParameters on the stack)` — the XDK `Resolve` order.
* **Flags**: bits 0–2 = source (0–3 render target index, **4 = depth/stencil**), bits 4–6 = fragment (sample) selector (0 = all/auto: filled from the surface's MSAA type to 0x10 / 0x50 / 0x70),
  bit 14 (0x4000) and bit 15 (0x8000) = clear colour / clear depth-stencil after the copy (checked against two runtime switches).
* How it works on hardware: flush all pending state (every group), build a screen-aligned **RECT_LIST** draw with a built-in 17-dword vertex program (`IM_LOAD_IMM` 0xC0102B00), set `RB_MODECONTROL` (0x2208) to copy mode and program
  `RB_COPY_CONTROL` 0x2318, `RB_COPY_DEST_BASE` 0x2319, `RB_COPY_DEST_INFO` 0x231B (+ pitch/offset), vertex fetch constants for the rect (0x4800…), `EVENT_WRITE`, `WAIT_REG_MEM` for completion. Destination format/endian come from the destination texture's fetch constant (+28…+48).
* **Engine use** (6 call sites): (1) the frame function 0x82791828: `Resolve(RT0 → front-buffer texture)` then `Swap`; (2) 0x8278E0A0 / 0x8278E288 / 0x82791560 / 0x827941B8: copy helpers; **0x8278E0A0 resolves the depth buffer once per MSAA sample**
  (Flags 0x14, 0x24, 0x34, 0x44 = depth + fragment 1…4, destination points 0/1024/… in a 1024-wide texture) — the game reads individual depth samples.
* For the port: `Resolve` = copy/blit the source image (colour or depth, optionally a single MSAA sample) into the destination texture, with optional clear; no EDRAM emulation is needed. Render directly into Vulkan images; keep MSAA optional (depth-sample resolve becomes a plain depth copy when MSAA is off).

## Not done / uncertain
* Exact bit meaning of fragment codes 2/3/4 beyond what the engine passes; the in-flag semantics of bits 14/15 should be confirmed from a trace.
* The unidentified third render-target format word 0x1A215586.
* Index endian modes and the 32-bit index flag were read from code, not yet checked against a real index buffer.
