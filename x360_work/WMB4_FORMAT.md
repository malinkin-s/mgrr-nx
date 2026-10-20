# WMB4 model format (MGR:R, Xbox 360, big-endian)

Parser: `tools/wmb.py` (class `WMB`, needs numpy), preview renderer `tools/wmb_preview.py`, validator `tools/wmb_validate.py`, bulk extraction `tools/extract_wmb.py`
(946 models extracted from the `.dat` archives into `x360_work/wmb/`; 945 are WMB4, one WMB2). Companion files in the same `.dat`: `.wta` (texture headers) — the pixel data is in the `.dtt` as `.wtp`; `.mot` (motions), `.hkx` (Havok), `.bxm`, `.eff`.

## Header (offsets are file offsets)
| offset | field |
|---|---|
| 0x00 | `WMB4` |
| 0x04 | 0xFFFFFFFF |
| 0x08 | **vertex-format flags** (0x10337, 0x10137, 0x10307, 0x137, 0x107, 0x10107, …) |
| 0x0C | flags2 (0xFFFF, 0x1FFFF, 0x10000, 0, 1 — unknown) |
| 0x10 | bounding box: **centre xyz, half-extent xyz** (6 × f32) |
| 0x28 / 0x2C | vertex groups: offset, count |
| 0x30 / 0x34 | batches: offset, count |
| 0x38 | offset of a `{offset,count}` pair that points at the batch-info records |
| 0x3C / 0x40 | bones: offset, count |
| 0x44 / 0x48 | bone index translate table: offset, size in bytes |
| 0x4C / 0x50 | bone sets: offset, count |
| 0x54 / 0x58 | materials: offset, count |
| 0x5C / 0x60 | textures: offset, count |
| 0x64 / 0x68 | meshes: offset, count |

## Tables
* **Vertex group** (28 bytes): `vertexOffset, extraOffset, 0, 0, vertexCount, indexOffset, indexCount`. The main vertex block starts at `vertexOffset`, the optional second stream at `extraOffset`
  (0 = none), indices (u16) at `indexOffset`; blocks are packed back to back, the next group starts where the previous index block ends (16-byte aligned).
  Strides come from the flags (below); `(extraOffset-vertexOffset)/vertexCount` gives them too when alignment padding is ignored.
* **Batch** (20 bytes): `vertexGroup, vertexStart, indexStart, vertexCount, indexCount` — prefix sums over the group (verified: sums equal the group totals). A batch is one draw call.
* **Batch info** (16 bytes each, `{offset,count}` header at 0x38 → records): `batchIndex, meshIndex, [u16 **materialIndex**, u16 boneSetIndex], 0x00010000 (or 0)`. The material index is the *hi* half of the third word (verified: 6 189/6 189 batches have it inside their mesh's material list, none out of range).
  Some models describe only part of the batches; **batches that no mesh lists** (and have no info) are extra/low-detail geometry and should not be drawn (em0010: batches 53–77 are 25 such shells that overlap the real body).
* **Indices**: u16 big-endian, **local to the batch** (add `vertexStart`). **Primitive type comes from `flags2` (hdr[3]) bit 16**: set → **triangle strips with 0xFFFF restart**, clear → **triangle list** (verified: all 455 models with flags2 ∈ {0xFFFF, 0, 1} contain no restarts and lengths divisible by 3; all models with bit 16 set are strips). Vertex winding is Direct3D **clockwise**.
* **Bone** (32 bytes): `id u16, id2 u16, parent s16, flags u16, local position vec3, world position vec3` (world = parent world + local; verified).
* **Bone index translate table** (u16 array): 3-level lookup keyed by the nibbles of a 12-bit bone id `n1 = id>>8, n2 = (id>>4)&15, n3 = id&15`: `t[ t[ t[n1] + n2 ] + n3 ]`, offsets in **u16 elements**, 0xFFFF = absent.
  Verified: translate(bone.id) = bone index for 145/145 bones of the test model.
* **Bone set** (8 bytes): pointer to a list of **u8 bone indices**, count in the top byte. A skinned vertex's BLENDINDICES index into the batch's bone set; verified: no vertex with weight>0 exceeds the set size.
* **Material** (24 bytes): `shaderKeyPtr (string like "ois00_xbceX", "css00_sbXxv", "siv20_sxxxx"), bindingsPtr, 0, paramsPtr, u16 ?, u16 nBindings, u32 flags (0x10/0xC/0x14)`.
  Bindings = **exactly nBindings** pairs `(role, textureIndex)` — `(0, 0)` is a valid binding (an earlier version of the parser dropped it) — where `textureIndex` indexes the model's texture table; roles seen: 0, 1, 2, 3, 4, 7, 8, 9, 10 (role 0 = the material's own texture, 2 = a per-material second texture; 4/7/9 usually shared lookup textures). Params = float block (count = (next material's pointers) …), meaning unknown.
* **Texture** (8 bytes): `flags, textureId` — the id equals the `id` in the WTA header (e.g. em0200: 0x0492875B = 76711771 = first WTA texture).
* **Mesh** (68 bytes): `namePtr, bbox 6×f32 (centre, half-extent), batchListPtr, nBatches, 6 zeros, materialListPtr, nMaterials` — lists are u16 BE. The material list is the **set of materials the mesh uses** (not parallel to the batch list; each batch's own material is in the batch info).
  Mesh names show state variants that the game enables selectively: `nocut_*` (intact limbs), `cut-*` / `danmen-*` (dismembered), `*_dam1/_dam2`, `*-blood_dam*` (damage), `saya` (sheath), `gurd`, … — a viewer must not draw them all at once.

## Vertex format (main stream, big-endian)
| offset | attribute | encoding |
|---|---|---|
| 0 | POSITION | 3 × f32 |
| 12 | TEXCOORD0 | 2 × f16 |
| 16 | NORMAL | 32-bit big-endian word, signed normalized: **X = bits 10..0 (11), Y = bits 21..11 (11), Z = bits 31..22 (10)** (Xenos 10_11_11: widths listed from the MSB) |
| 20 | TANGENT | u8×4, **memory order [W, Z, Y, X]** (xyz = v/255*2-1) |
| 24 | BLENDINDICES u8×4 *and* 28 BLENDWEIGHT u8×4n | when `flags & 0x30` (skinned); weights sum to 255 |
| 24 | COLOR u8×4n (stride ≥ 28) and 28 TEXCOORD1 2×f16 (stride 32) | when not skinned |

**Strides come from the flags, not from offsets** (blocks are padded to 16 bytes, so small groups give wrong strides if derived from offsets): skinned (`flags & 0x30`) → main 32; otherwise main 24 (+4 COLOR if `flags&0x10000 and flags&0x100`, +4 TEXCOORD1 if `flags&0x10000 and flags&0x200`);
extra stream (`extraOffset != 0`) = 4 × (0x100) + 4 × (0x200) bytes (only effective with 0x10000 — except 0x337, which still has 8). All 2 223 groups satisfy `block + padding(<16) = next block` with this rule. The "26-byte" class seen earlier was just padding.
Flag classes seen (groups, first estimate): `0x10307` stride 32 (no skin: colour+UV1 in the main stream) 742; `0x10137` 32+4 (skin; extra stream = COLOR) 575; `0x10337` 32+8 (skin; extra = COLOR + TEXCOORD1) 421;
`0x137` 32 (skin) 204; `0x107` 24 (no skin, no colour) 157; `0x10107` 28 (COLOR @24) 89; (the former 26-byte class is padding).
Extra stream: 8 bytes = `COLOR u8x4` + `TEXCOORD1 f16x2`; 4 bytes = `COLOR`. These correspond to the declarations in VERTEX_FORMATS.md (e.g. flags 0x10337 ⇒ decl 0x821DE6C0).

## Validation (tools/wmb_validate.py over all models)
* 945 WMB4 files: 792 pass every invariant, 9 are empty stubs (≤ 256 bytes, no geometry), 144 are fine except that the batch-info table covers only some batches. Totals: 4.52 M vertices, 4.39 M triangles (the earlier 6.93 M figure expanded list batches as strips).
* **Normal/winding/primitive-type check on 156 sampled models**: the decoded vertex normal agrees (>0.3) with the clockwise geometric face normal for a **median 100%** of triangles (mean 98.6%, 10th percentile 97.2%). This is what fixed the axis order (the earlier length-only test could not).
* Checked: batch ranges inside groups, batch-local indices < batch vertex count, mesh batch lists in range / no batch in two meshes, material and texture indices in range, bone translate table for every bone, bone parents in range,
  skin indices vs bone-set size, finite positions. Normals unit length (0.999 ± 0.000) on 2 000 test vertices.
* Visual (viewer, textured): `x360_work/tex/view_pl0100.png` (Raiden: face, yellow shoulders, blue chest, shin guards), `view_em0010.png` (soldier), `view_em0200.png` (the bladed enemy); wireframe-style previews: `em0200_preview.png` (a large enemy: legs, tail, bladed arms, per-mesh colours coherent) and `ba0020_preview.png` (a palm tree; a few long thin triangles near the trunk are unexplained).

## Unknown / not done
flags2 and the `u16` high half in batch info; vertex-group fields 2–3 (always 0); bone `id2`/`flags`; material float params and the meaning of each binding role (needs the shader-key → shader mapping); the 26-byte stride class;
WMB2 (1 file); animations (`.mot`) and the skeleton-to-pose math; 0xFFFF in lists; whether positions of skinned batches need the inverse-bind step (rendering in bind pose looked correct).
