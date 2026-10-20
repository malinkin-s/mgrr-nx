# Fetch constants, sampler/stream binding and shaders (Xbox 360 D3D in MGR:R)

## Fetch-constant shadow (dev+1152, 32 slots × 6 dwords; flushed by 0x826CA690 to registers 0x4800+6*slot; dirty = low 32 bits of the qword at dev+24, slot s ↔ bit 31-s)
* **Textures** use slot = sampler index, vertex streams use the slots from the end: stream n owns the dword pair at dev+1912-8n / dev+1916-8n
  (vertex fetch constant index 95-n, slot = (95-n)/3 — see wrapper 0x82791308).
* **SetTexture** = `0x826AEFD0 (dev, sampler r4, texture r5, dirtyMask r6)`: copies the texture's embedded 6-dword fetch constant (texture object +28…+48),
  keeps the sampler-state bits already set (rlwimi with old values), stores the pointer at [dev+12920+4*sampler], clamps mip levels from byte arrays at dev+12356+s / 12382+s.
* **SetStreamSource** = `0x826B8670 (dev, stream r4, vb r5, offset r6, stride r7, dirtyMask r8)`: dword0 = ([vb+24]+offset) with the GPU-window fix-up, dword1 = [vb+28]-offset;
  the stride in dwords goes to byte dev+12904+stream; when it changed, bit 19 of the qword at dev+16 is set (D3D patches vfetch strides in the vertex shader microcode in 0x826CB580).
  Engine wrapper: `0x82791308 (index, streamObject)` — 198 call sites.
* **SetIndices** = `0x826B8790 (dev, indexBuffer)`: [dev+12812]; wrapper 0x827912B0 (8 call sites).
* **Sampler-state setters** (sampler r4, value r5), field in the fetch constant (names per Xenos `xe_gpu_texture_fetch_t`; bit positions in the code matched in every case):
  0x826B7B50 min_filter(+aniso/clamp policy), 0x826B7CF8 mag_filter, 0x826B7E98 mip_filter [dw3 23..24], 0x826B7F98 aniso_filter [dw3 25..27],
  0x826B80B8 lod_bias, 0x826B8158/0x826B81D8 mip min/max level, 0x826B8010 aniso_bias [dw5 5..8], 0x826B8258 border_color [dw5 0..1], 0x826B83B8 tri_clamp [dw5 3..4], 0x826B84C0 force_bcw_max,
  0x826B82C8/0x826B8318/0x826B8368 clamp_x/y/z (ADDRESSU/V/W) [dw0 10..18], 0x826B8410/0x826B8468 grad_exp_adjust h/v, 0x826B8518 clamp_policy [dw1 11].  (Data: x360_work/sampler_setters.json)

## Shaders
* **SetVertexShader** = `0x826AC618` ([dev+13072], clears bits in the vertex-constant mask dev+0); **SetPixelShader** = `0x826AC410` ([dev+13068], mask dev+8).
  Engine wrappers 0x8278EF10 / 0x8278EEB0 read the D3D shader pointer from engine-object+4.
* **CreateXShader** = `0x826ACC10` and `0x826ACDF8` (the pair is VS/PS; the type is in the blob magic).
  Blob = `[header: first [blob+4] bytes][microcode: [blob+8] bytes]`, magic **0x102A1101 = vertex, 0x102A1100 = pixel**, header holds the D3DX CTAB (constant names), program info, vfetch/interpolator data.
  Object: header copied to obj+40, microcode copied to a separate physical buffer, pointer at obj+24. IM_LOAD (0xC0012700) loads it: address = [obj+24] + [blk+40], size = [blk+44], blk = obj+[obj+64]; type 1 = pixel.
* **Shader data**: data000.cpk → `shader.dat` (1029: 265 vso + 764 pso), `shader2.dat` (83), `shadereff.dat` (428), plus `ShaderSign.dat`, `shadersetting.bxm`.
  CPK entries are CRILAYLA with the "CRILAYLA" magic blanked to zeros (header: u32 ulen-0x100, u32 hlen). DAT = Platinum "DAT\0" big-endian archive.
* **Conversion** (XenosRecomp 990d03b, unmodified): 1488/1492 → HLSL (4 particle VS crash: espshadermulti*particle*), 1482/1488 → SPIR-V with DXC (6 fail: CTAB without sampler entry,
  `sN_Texture2DDescriptorIndex` undeclared). Total 10/1492 (0.7%) unconverted. SPIR-V total 18 MB. Results: x360_work/shaders/{hlsl_failures,spirv_failures}.txt.
* **Lookup at runtime**: hash the blob passed to CreateXShader (UnleashedRecomp uses XXH3-64) and map to the precompiled SPIR-V.

## Implications for the Switch renderer
* Generated HLSL reads constants through `vk::RawBufferLoad` (buffer device address) and uses bindless descriptor arrays → needs Vulkan 1.2-class features
  (Autorun's mesa-switch exposes NVK Vulkan on Tegra X1; native NVN/deko3d would need SPIR-V→GLSL→DKSH instead of SPIR-V).
