#!/usr/bin/env python3
"""PlatinumGames WMB4 model reader (Xbox 360, big-endian) — MGR:R.

Layout (all offsets are file offsets, big-endian):
  0x00 'WMB4' | 0x04 ffffffff | 0x08 vertex-format flags | 0x0C flags2 | 0x10 bbox 6 floats (centre xyz, half-extent xyz)
  0x28 vertexGroups (offset, count)       records of 28 bytes: vertexOffset, extraOffset, 0, 0, vertexCount, indexOffset, indexCount
  0x30 batches      (offset, count)       records of 20 bytes: vertexGroup, vertexStart, indexStart, vertexCount, indexCount
  0x38 batchInfo    offset -> {offset,count}: records of 16 bytes: batchIndex, meshIndex, (hi16 materialIndex, lo16 boneSetIndex), 0x00010000
  0x3C bones        (offset, count)       records of 32 bytes: id u16, id2 u16, parent s16, flags u16, local pos vec3, world pos vec3
  0x44 boneIndexTranslateTable (offset, size in bytes)       3-level nibble lookup of u16 (offsets in u16 elements)
  0x4C boneSets     (offset, count)       records of 8 bytes: ptr to u8 bone-index list, count<<24
  0x54 materials    (offset, count)       records of 24 bytes: ptr shader-key string, ptr texture bindings (u32 pairs role,textureIndex),
                                           0, ptr float params, u16 ?, u16 nBindings, u32 flags
  0x5C textures     (offset, count)       records of 8 bytes: flags, texture id (matches WTA ids)
  0x64 meshes       (offset, count)       records of 68 bytes: ptr name, bbox 6 floats, ptr batchList, n, 6 zeros, ptr materialList, n
Indices are u16 BE, batch-local: triangle STRIPS with 0xFFFF restart when flags2 bit 16 is set, otherwise a triangle LIST.
Vertex main stream (stride 24/28/32 by flags): POSITION f32x3 @0, TEXCOORD0 f16x2 @12, NORMAL 10_11_11 @16 (X = low 11 bits, Y = middle 11, Z = top 10 bits), TANGENT ubyte4n, memory order [W, Z, Y, X] @20,
  then (flags&0x30: skinned) BLENDINDICES u8x4 @24, BLENDWEIGHT u8x4n @28  | else COLOR u8x4 @24 (stride>=28), TEXCOORD1 f16x2 @28 (stride 32).
Extra stream (flags 0x10337: 8 bytes COLOR u8x4 + TEXCOORD1 f16x2; flags 0x10137: 4 bytes COLOR).
"""
import struct
import sys
import numpy as np


class WMB:
    def __init__(self, data):
        d = self.d = data
        assert d[:4] == b'WMB4', d[:4]
        u32 = lambda o: struct.unpack_from('>I', d, o)[0]
        self.flags = u32(8)
        self.flags2 = u32(12)
        self.bbox = struct.unpack_from('>6f', d, 0x10)
        # --- vertex groups
        o, n = struct.unpack_from('>II', d, 0x28)
        self.groups = []
        for g in range(n):
            vo, eo, _, _, vc, io, ic = struct.unpack_from('>7I', d, o + 28 * g)
            main, extra = self.strides(eo != 0, vc, vo, eo, io)
            self.groups.append(dict(vertex_offset=vo, extra_offset=eo, vertex_count=vc, index_offset=io, index_count=ic,
                                    stride=main, extra_stride=extra))
        # --- batches
        o, n = struct.unpack_from('>II', d, 0x30)
        self.batches = [dict(zip(('group', 'vertex_start', 'index_start', 'vertex_count', 'index_count'), struct.unpack_from('>5I', d, o + 20 * k))) for k in range(n)]
        # --- batch info
        bo = u32(0x38)
        io_, n = struct.unpack_from('>II', d, bo) if bo and bo + 8 <= len(d) else (0, 0)
        for b_ in self.batches: b_.update(mesh=None, bone_set=None, material=None)       # some models describe only part of the batches
        for k in range(n):
            b, mesh, bs, _ = struct.unpack_from('>IIII', d, io_ + 16 * k)
            if b < len(self.batches): self.batches[b].update(mesh=mesh, bone_set=bs & 0xFFFF, material=bs >> 16)
        # --- bones
        o, n = struct.unpack_from('>II', d, 0x3C)
        self.bones = []
        for k in range(n):
            id1, id2, parent, fl = struct.unpack_from('>HHhH', d, o + 32 * k)
            lp = struct.unpack_from('>3f', d, o + 32 * k + 8); wp = struct.unpack_from('>3f', d, o + 32 * k + 20)
            self.bones.append(dict(id=id1, id2=id2, parent=parent, flags=fl, local=lp, world=wp))
        # --- bone index translate table
        o, sz = struct.unpack_from('>II', d, 0x44)
        self.translate = struct.unpack_from('>%dH' % (sz // 2), d, o)
        # --- bone sets
        o, n = struct.unpack_from('>II', d, 0x4C)
        self.bone_sets = []
        for k in range(n):
            p, c = struct.unpack_from('>II', d, o + 8 * k)
            self.bone_sets.append(list(d[p:p + (c >> 24)]))
        # --- materials
        o, n = struct.unpack_from('>II', d, 0x54)
        self.materials = []
        for k in range(n):
            p1, p2, _, p3, nb, x, fl = struct.unpack_from('>IIIIHHI', d, o + 24 * k)
            nm = d[p1:d.index(b'\0', p1)].decode('latin-1')
            nxt = struct.unpack_from('>I', d, o + 24 * (k + 1))[0] if k + 1 < n else None
            nbind = (p3 - p2) // 4
            binds = struct.unpack_from('>%dI' % nbind, d, p2)
            pairs = [(binds[i], binds[i + 1]) for i in range(0, min(2 * x, nbind - 1), 2)]       # x (2nd u16) = number of bindings; (0,0) is a real binding
            self.materials.append(dict(shader_key=nm, bindings=pairs, params_ptr=p3, unk=(nb, x, fl)))
        # --- textures
        o, n = struct.unpack_from('>II', d, 0x5C)
        self.textures = [struct.unpack_from('>II', d, o + 8 * k) for k in range(n)]     # (flags, id)
        # --- meshes
        o, n = struct.unpack_from('>II', d, 0x64)
        self.meshes = []
        for k in range(n):
            q = o + 68 * k
            np_ = struct.unpack_from('>I', d, q)[0]
            name = d[np_:d.index(b'\0', np_)].decode('latin-1')
            bb = struct.unpack_from('>6f', d, q + 4)
            pa, ca = struct.unpack_from('>II', d, q + 28)
            pb, cb = struct.unpack_from('>II', d, q + 60)
            self.meshes.append(dict(name=name, bbox=bb, batches=list(struct.unpack_from('>%dH' % ca, d, pa)),
                                    materials=list(struct.unpack_from('>%dH' % cb, d, pb))))

    def strides(self, has_extra, vc=0, vo=0, eo=0, io=0):
        """(main stride, extra stride). The flags give the rule; blocks are padded to 16 bytes so offsets only *confirm* it:
        skinned (flags&0x30): main 32 (pos, uv0, normal, tangent, blend indices+weights); else main 24, plus inline COLOR/UV1 (4 bytes each)
        when flags&0x10000 and bits 0x100 / 0x200. With an extra stream: COLOR (0x100) and UV1 (0x200), 4 bytes each (needs 0x10000, else inferred from sizes)."""
        f = self.flags
        eff = f & 0x10000
        col = 4 if (eff and f & 0x100) else 0; uv1 = 4 if (eff and f & 0x200) else 0
        if f & 0x30: main, extra = 32, ((col + uv1) if has_extra else 0)
        elif has_extra: main, extra = 24, col + uv1
        else: main, extra = 24 + col + uv1, 0
        if vc:                                           # confirm / repair using the block layout
            def fits(st, start, end): return 0 <= end - (start + st * vc) < 16
            nxt = eo or io
            if not fits(main, vo, nxt):
                for st in (24, 28, 32):
                    if fits(st, vo, nxt): main = st; break
            if has_extra and not fits(extra, eo, io):
                for st in (4, 8, 12, 16):
                    if fits(st, eo, io): extra = st; break
        return main, extra

    # ------------------------------------------------------------------ geometry
    def bone_for_id(self, bid):
        """translate a global bone id (12 bit) to a bone index through the 3-level table; None if absent"""
        t = self.translate
        o1 = t[(bid >> 8) & 15]                      # offsets are in u16 elements (verified: 145/145 bones)
        if o1 == 0xFFFF: return None
        o2 = t[o1 + ((bid >> 4) & 15)]
        if o2 == 0xFFFF: return None
        v = t[o2 + (bid & 15)]
        return None if v == 0xFFFF else v

    def attributes(self, group):
        """decode one vertex group into a dict of numpy arrays"""
        g = self.groups[group]; n = g['vertex_count']; s = g['stride']
        raw = np.frombuffer(self.d, np.uint8, n * s, g['vertex_offset']).reshape(n, s)
        out = dict(position=raw[:, 0:12].copy().view('>f4').reshape(n, 3).astype(np.float32),
                   texcoord0=raw[:, 12:16].copy().view('>f2').reshape(n, 2).astype(np.float32))
        w = raw[:, 16:20].copy().view('>u4').reshape(n).astype(np.int64)
        def snorm(v, bits):
            v = v & ((1 << bits) - 1); sv = np.where(v >= (1 << (bits - 1)), v - (1 << bits), v)
            return np.maximum(sv / float((1 << (bits - 1)) - 1), -1.0)
        out['normal'] = np.stack([snorm(w, 11), snorm(w >> 11, 11), snorm(w >> 22, 10)], 1).astype(np.float32)     # X = low 11 bits, Y = middle 11, Z = top 10 (verified against face normals)
        t = raw[:, 20:24].astype(np.float32)
        out['tangent'] = np.stack([t[:, 3], t[:, 2], t[:, 1]], 1) / 255 * 2 - 1      # bytes in memory [W, Z, Y, X]
        out['tangent_w'] = t[:, 0] / 255 * 2 - 1
        skinned = bool(self.flags & 0x30)
        if skinned and s >= 32:
            out['blendindices'] = raw[:, 24:28].copy()
            out['blendweight'] = raw[:, 28:32].astype(np.float32) / 255
        elif s >= 28:
            out['color'] = np.roll(raw[:, 24:28], -1, axis=1).astype(np.float32) / 255      # memory order A,R,G,B (D3DCOLOR big-endian)
            if s >= 32: out['texcoord1'] = raw[:, 28:32].copy().view('>f2').reshape(n, 2).astype(np.float32)
        if g['extra_stride'] and g['extra_offset']:
            ex = np.frombuffer(self.d, np.uint8, n * g['extra_stride'], g['extra_offset']).reshape(n, g['extra_stride'])
            out['color'] = np.roll(ex[:, 0:4], -1, axis=1).astype(np.float32) / 255
            if g['extra_stride'] >= 8: out['texcoord1'] = ex[:, 4:8].copy().view('>f2').reshape(n, 2).astype(np.float32)
        return out

    def indices(self, batch):
        """batch-local triangle-strip indices (with 0xFFFF restarts)"""
        b = self.batches[batch]; g = self.groups[b['group']]
        return np.frombuffer(self.d, '>u2', b['index_count'], g['index_offset'] + 2 * b['index_start']).astype(np.uint32)

    @property
    def strips(self):
        """flags2 bit 16 selects triangle STRIPS (with 0xFFFF restart); otherwise the indices are a triangle LIST (verified on 935 models)"""
        return bool(self.flags2 & 0x10000)

    def triangles(self, batch):
        """triangle list (batch-local indices); strips are expanded keeping the winding"""
        ix = self.indices(batch)
        if not self.strips:
            return ix[:len(ix) // 3 * 3].reshape(-1, 3)
        tris = []; run = []
        for v in list(ix) + [0xFFFF]:
            if v == 0xFFFF:
                for i in range(len(run) - 2):
                    a, b, c = run[i], run[i + 1], run[i + 2]
                    if a != b and b != c and a != c:
                        tris.append((a, c, b) if i & 1 else (a, b, c))
                run = []
            else:
                run.append(v)
        return np.array(tris, dtype=np.uint32).reshape(-1, 3)


if __name__ == '__main__':
    m = WMB(open(sys.argv[1], 'rb').read())
    print('flags 0x%x/0x%x' % (m.flags, m.flags2), 'bbox', [round(x, 2) for x in m.bbox])
    print('groups', m.groups); print('batches', len(m.batches), 'bones', len(m.bones), 'bonesets', len(m.bone_sets), 'materials', len(m.materials), 'textures', len(m.textures), 'meshes', len(m.meshes))
    for me in m.meshes[:5]: print(me['name'], me['batches'], me['materials'])
