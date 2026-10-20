# Xbox 360 vertex formats in MGR:R

## How formats reach the GPU
* Formats are **not** stored in vertex buffers. The engine builds a D3D vertex declaration (`CreateVertexDeclaration` = **0x826AC938**, 12-byte elements `stream u16, offset u16, type u32, method u8, usage u8, index u8, pad`; object: +24 element count, +28 highest stream, +52 elements) and binds it with **SetVertexDeclaration = 0x826AC830** (`[dev+12240]`, sets dirty bit 19 of the qword at dev+16).
* At draw time D3D (0x826CB580) combines declaration + vertex shader and patches the shader's `vfetch` instructions (format/stride) — a renderer that translates shaders to SPIR-V must instead build the Vulkan vertex input from the declaration and the stream strides (SetStreamSource, stride byte at dev+12904+stream).
* XenosRecomp maps each `vfetch` to a (usage, index) taken from the shader container, so the join is **shader input semantic ↔ declaration element (usage, index)**.

## Element type words (all that occur in the 74 declarations; PC names obtained by aligning the 360 and PC tables element by element)
| type word | PC D3DDECLTYPE | decoded | Vulkan |
|---|---|---|---|
| 0x2C83A4 | FLOAT1 | {'format': '32_FLOAT', 'endian': 2, 'signed': 1, 'integer': 1, 'color_swizzle': 0, 'raw': '0x2c83a4'} | R32_SFLOAT (1 uses) |
| 0x2C23A5 | FLOAT2 | {'format': '32_32_FLOAT', 'endian': 2, 'signed': 1, 'integer': 1, 'color_swizzle': 0, 'raw': '0x2c23a5'} | R32G32_SFLOAT (11 uses) |
| 0x2A23B9 | FLOAT3 | {'format': '32_32_32_FLOAT', 'endian': 2, 'signed': 1, 'integer': 1, 'color_swizzle': 0, 'raw': '0x2a23b9'} | R32G32B32_SFLOAT (62 uses) |
| 0x1A23A6 | FLOAT4 | {'format': '32_32_32_32_FLOAT', 'endian': 2, 'signed': 1, 'integer': 1, 'color_swizzle': 0, 'raw': '0x1a23a6'} | R32G32B32A32_SFLOAT (11 uses) |
| 0x2C235F | FLOAT16_2 | {'format': '16_16_FLOAT', 'endian': 1, 'signed': 1, 'integer': 1, 'color_swizzle': 0, 'raw': '0x2c235f'} | R16G16_SFLOAT (49 uses) |
| 0x1A2360 | FLOAT16_4 | {'format': '16_16_16_16_FLOAT', 'endian': 1, 'signed': 1, 'integer': 1, 'color_swizzle': 0, 'raw': '0x1a2360'} | R16G16B16A16_SFLOAT (49 uses) |
| 0x1A2086 | UBYTE4N | {'format': '8_8_8_8', 'endian': 2, 'signed': 0, 'integer': 0, 'color_swizzle': 0, 'raw': '0x1a2086'} | R8G8B8A8_UNORM (57 uses) |
| 0x1A2286 | UBYTE4 | {'format': '8_8_8_8', 'endian': 2, 'signed': 0, 'integer': 1, 'color_swizzle': 0, 'raw': '0x1a2286'} | R8G8B8A8_UINT (16 uses) |
| 0x182886 | D3DCOLOR | {'format': '8_8_8_8', 'endian': 2, 'signed': 0, 'integer': 0, 'color_swizzle': 1, 'raw': '0x182886'} | B8G8R8A8_UNORM (bit 11 = colour swizzle; verify on data) (1 uses) |
| 0x2A2190 | (Xbox only) 10_11_11 signed-normalized | {'format': '10_11_11', 'endian': 2, 'signed': 1, 'integer': 0, 'color_swizzle': 0, 'raw': '0x2a2190'} | no native format: unpack in shader, X=bits 22..31 (10), Y=bits 11..21, Z=bits 0..10 of the big-endian dword (36 uses) |

Word layout: bits 0–5 Xenos vertex data format (57 = 32_32_32_FLOAT, 37 = 32_32_FLOAT, 38 = 32_32_32_32_FLOAT, 36 = 32_FLOAT, 31 = 16_16_FLOAT, 32 = 16_16_16_16_FLOAT, 6 = 8_8_8_8, 16 = 10_11_11), bits 6–7 endian (2 = 8in32 for 32-bit elements, 1 = 8in16 for halves), bit 8 signed, bit 9 integer (UBYTE4 vs UBYTE4N).

## Usage → type (360)
| usage | types |
|---|---|
| TEXCOORD | FLOAT16_2×49, FLOAT16_4×36, FLOAT2×11, FLOAT4×7, FLOAT3×4, FLOAT1×1 |
| POSITION | FLOAT3×58, FLOAT16_4×13, FLOAT4×3 |
| NORMAL | (Xbox only) 10_11_11 signed-normalized×36 |
| COLOR | UBYTE4N×26, FLOAT4×1, D3DCOLOR×1 |
| BLENDINDICES | UBYTE4×16 |
| BLENDWEIGHT | UBYTE4N×16 |
| TANGENT | UBYTE4N×15 |

## Verified on real data (em0200.wmb, 2000 vertices, stride 32)
Layout `POSITION float3 @0, TEXCOORD0 float16_2 @12, NORMAL 10_11_11 @16, TANGENT ubyte4n @20, BLENDINDICES ubyte4 @24, BLENDWEIGHT ubyte4n @28` (a 360 declaration present in the table).
* Positions are big-endian floats (character ~31 units tall); texcoords big-endian halves.
* **Normal**: reading the 4 bytes as a big-endian dword, fields 10/11/11 bits from the top: all 2 000 decoded normals have length 0.999 ± 0.000. **Correction (viewer phase):** the axis order is X = low 11 bits, Y = middle 11, Z = top 10 — settled by comparing with geometric face normals (median 100% agreement over 156 models; the length test used earlier could not distinguish permutations).
* **Tangent**: ubyte4n in memory order **[W, Z, Y, X]** (corrected; xyz length error ≤ 0.02, best orthogonality to the normal among all byte permutations).
* Bone indices/weights are in natural byte order: `11 00 00 00` / `FF 00 00 00` = bone 0x11, weight 1.0.

## Declaration catalog
74 declarations / 296-ish elements in `x360_work/x360_vertex_decls.json` (address, elements with stream/offset/type/usage/index) and the PC originals in `pc_vertex_decls.json` (73).
The 360 and PC tables share 29 identical layouts; the other layouts differ because the Xbox build packs normals/tangents as 4-byte elements instead of FLOAT16_4.
