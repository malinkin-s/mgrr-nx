#!/usr/bin/env python3
"""Export a WMB model as a scene package for tools/vkview (real Xenos shaders through XenosRecomp -> SPIR-V).

  export_scene.py em0200 PKGDIR [--yaw DEG] [--pitch DEG] [--size N] [--alpha-test]
Needs numpy/PIL (use the scratchpad venv).
"""
import argparse, math, os, struct, sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools'); sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools/shaders')
import numpy as np
from PIL import Image
import model_loader, shaderlib

# VkFormat ints
R8G8B8A8_UNORM, R8G8B8A8_UINT, R32G32B32A32_UINT, R32_UINT, R32G32_SFLOAT, R32G32B32_SFLOAT, R32G32B32A32_SFLOAT = 37, 41, 107, 98, 103, 106, 109
HIDE = ('nocut', 'cut-', 'danmen', '_dam1', '_dam2', 'dam1_', 'blood', 'broken', '_EFD')
ROLE_SLOT = {0: 0, 4: 2, 2: 3}            # material binding role -> PS fetch slot (Color_1, cubemap, normalmap) for the ois/cnm families
SAMPLER_ROLE = {'Color_1': 0, 'cubemap': 4, 'normalmap': 2}


def persp(fovy, aspect, n, f):
    t = 1 / math.tan(fovy / 2)
    return np.array([[t / aspect, 0, 0, 0], [0, t, 0, 0], [0, 0, f / (n - f), n * f / (n - f)], [0, 0, -1, 0]], np.float32)   # right-handed view, Vulkan/D3D depth 0..1


def look_at(eye, ctr, up=(0, 1, 0)):
    f = np.array(ctr, np.float32) - eye; f /= np.linalg.norm(f)
    s = np.cross(f, up); s /= np.linalg.norm(s); u = np.cross(s, f)
    m = np.eye(4, dtype=np.float32); m[0, :3] = s; m[1, :3] = u; m[2, :3] = -f
    m[:3, 3] = -m[:3, :3] @ eye
    return m


def pack_r11g11b10(v):
    v = np.clip(v, -1, 1)
    x = np.round(v[:, 0] * 1024).astype(np.int64) & 0x7FF
    y = np.round(v[:, 1] * 1024).astype(np.int64) & 0x7FF
    z = np.round(v[:, 2] * 512).astype(np.int64) & 0x3FF
    return (x | (y << 11) | (z << 22)).astype(np.uint32)


def mip_chain(img):
    im = Image.fromarray(np.ascontiguousarray(img)).convert('RGBA'); out = []; w, h = im.size
    while True:
        out.append(im.tobytes())
        if w == 1 and h == 1: break
        w, h = max(1, w // 2), max(1, h // 2); im = im.resize((w, h), Image.BOX)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model'); ap.add_argument('pkg'); ap.add_argument('--yaw', type=float, default=35); ap.add_argument('--pitch', type=float, default=14)
    ap.add_argument('--size', type=int, default=900); ap.add_argument('--alpha-test', action='store_true'); ap.add_argument('--all-meshes', action='store_true')
    ap.add_argument('--vs', default=None, help='force a vertex shader name'); ap.add_argument('--only-key', default=None)
    a = ap.parse_args()
    os.makedirs(a.pkg, exist_ok=True)
    model = model_loader.load(a.model); w = model.wmb
    man = ['size %d %d' % (a.size, a.size), 'clear 0.08 0.08 0.1 1']

    # --- default textures (heap 0 white, 1 flat normal, 2 grey)
    def put_tex(idx, wd, ht, chain, name):
        open(os.path.join(a.pkg, name), 'wb').write(b''.join(chain)); man.append('tex %d %d %d %d %s' % (idx, wd, ht, len(chain), name))
    put_tex(0, 1, 1, [bytes([255, 255, 255, 255])], 'tex_white.bin')
    put_tex(1, 1, 1, [bytes([128, 128, 255, 255])], 'tex_nrm.bin')
    put_tex(2, 1, 1, [bytes([110, 110, 110, 255])], 'tex_grey.bin')
    man.append('smp 0 1 1 0 8')
    heap = {}
    def tex_index(tid):
        if tid not in model.textures: return None
        if tid not in heap:
            img = model.textures[tid]; chain = mip_chain(img); heap[tid] = 3 + len(heap)
            put_tex(heap[tid], img.shape[1], img.shape[0], chain, 'tex_%d.bin' % tid)
        return heap[tid]

    # --- camera
    yaw, pitch = math.radians(a.yaw), math.radians(a.pitch)
    c = np.array(w.bbox[:3], np.float32); ext = float(np.linalg.norm(w.bbox[3:6]))
    eye = c + ext * 3.0 * np.array([math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)], np.float32)
    view = look_at(eye, c); proj = persp(math.radians(35), 1.0, ext * 0.05, ext * 8); vp = proj @ view
    vinv = np.linalg.inv(view).astype(np.float32)

    # --- group vertex data
    attrs = {}
    def group_attrs(g):
        if g not in attrs: attrs[g] = w.attributes(g)
        return attrs[g]

    # --- draws
    mesh_of = {b: m for m, me in enumerate(w.meshes) for b in me['batches']}
    mat_of = {}
    for me in w.meshes:
        for b in me['batches']:
            if me['materials']: mat_of[b] = me['materials'][0]
    vcache = {}; nd = 0; skipped = {}
    for bi, b in enumerate(w.batches):
        g = b['group']
        if w.groups[g]['vertex_count'] == 0: continue
        if w.meshes and bi not in mesh_of and not a.all_meshes: continue
        if bi in mesh_of and not a.all_meshes and any(h in w.meshes[mesh_of[bi]]['name'] for h in HIDE): continue
        tri = w.triangles(bi)
        if not len(tri): continue
        mt = b.get('material')
        if mt is None: mt = mat_of.get(bi)
        if mt is None or mt >= len(w.materials): continue
        mat = w.materials[mt]; key = mat['shader_key']
        if a.only_key and key != a.only_key: continue
        psn = shaderlib.ps_name_for_key(key)
        try: ps = shaderlib.interface(psn)
        except Exception as e:
            skipped[key] = str(e)[:80]; continue
        at = group_attrs(g)
        skinned = 'blendindices' in at
        avail = {('Position', 0), ('Normal', 0), ('Tangent', 0), ('TexCoord', 0)}
        if 'texcoord1' in at: avail.add(('TexCoord', 1))
        if 'color' in at: avail.add(('Color', 0))
        if skinned: avail |= {('BlendIndices', 0), ('BlendWeight', 0)}
        ck = (psn, frozenset(avail), skinned)
        if ck not in vcache: vcache[ck] = a.vs or shaderlib.choose_vs(ps, avail, skinned)
        vsn = vcache[ck]
        if vsn is None: skipped[key] = 'no compatible VS'; continue
        vs = shaderlib.interface(vsn)
        # interleaved vertex buffer for this VS (per batch, indices rebased to the batch vertex window)
        n = w.groups[g]['vertex_count']; cols = []; man_attr = []; off = 0
        for (u, i, ty, loc) in vs.inputs:
            if loc is None: continue
            if (u, i) == ('Position', 0): d = at['position'].astype('<f4'); f = R32G32B32_SFLOAT
            elif (u, i) == ('Normal', 0):               # spec bit0 clear: the shader reads the uint4 as float bits (the game's own R11G11B10 path is not needed)
                d = np.concatenate([at['normal'], np.zeros((n, 1), np.float32)], 1).astype('<f4').view('<u4'); f = R32G32B32A32_UINT
            elif (u, i) == ('Tangent', 0):              # the game feeds UBYTE4N (0..1) and the VS expands it with *2-1
                d = np.concatenate([(at['tangent'] + 1) / 2, ((at['tangent_w'] + 1) / 2)[:, None]], 1).astype('<f4').view('<u4'); f = R32G32B32A32_UINT
            elif (u, i) == ('TexCoord', 0): d = at['texcoord0'].astype('<f4'); f = R32G32_SFLOAT
            elif (u, i) == ('TexCoord', 1): d = at['texcoord1'].astype('<f4'); f = R32G32_SFLOAT
            elif (u, i) == ('Color', 0): d = (at['color'] * 255 + .5).astype(np.uint8); f = R8G8B8A8_UNORM
            elif (u, i) == ('BlendIndices', 0): d = at['blendindices'].astype(np.uint8); f = R8G8B8A8_UINT
            elif (u, i) == ('BlendWeight', 0): d = at['blendweight'].astype('<f4'); f = R32G32B32A32_SFLOAT
            else: raise RuntimeError('unhandled VS input %s%d' % (u, i))
            d = np.ascontiguousarray(d).reshape(n, -1); cols.append(d.view(np.uint8).reshape(n, -1)); man_attr.append((loc, off, f)); off += cols[-1].shape[1]
        vbuf = np.concatenate(cols, 1) if cols else np.zeros((n, 0), np.uint8)
        stride = off
        vfile = 'vb_%d_%d.bin' % (g, abs(hash(vsn)) % 100000)
        if not os.path.exists(os.path.join(a.pkg, vfile)): open(os.path.join(a.pkg, vfile), 'wb').write(vbuf.tobytes())
        ifile = 'ib_%d.bin' % bi
        open(os.path.join(a.pkg, ifile), 'wb').write((tri.astype(np.uint32) + b['vertex_start']).astype('<u4').tobytes())

        # --- constants
        vsc = np.zeros((256, 4), np.float32); psc = np.zeros((224, 4), np.float32); sh = np.zeros(128, np.uint32)
        def setm(buf, reg, m): buf[reg:reg + 4] = m
        setm(vsc, 0, view); setm(vsc, 4, proj); setm(vsc, 8, vp); setm(vsc, 12, vinv); setm(vsc, 16, np.eye(4, dtype=np.float32)); setm(vsc, 20, view)
        for k in range(72): vsc[40 + 3 * k:43 + 3 * k] = np.eye(4, dtype=np.float32)[:3]
        vsc[184] = (1, 1e6, 0, 0); vsc[195] = (0.55, 0.60, 0.70, 1); vsc[199] = (0.30, 0.27, 0.24, 1)
        # pixel constants
        setm(psc, 12, vinv)
        params = np.frombuffer(w.d, '>f4', 16, mat['params_ptr']).astype(np.float32)
        psc[40:44] = params.reshape(4, 4)
        psc[61] = (2.5, 2.4, 2.3, 1); psc[62] = (0.4, 0.7, 0.6, 0); psc[71] = (1, 1, 1, 1); psc[77] = (1, 1, 1, 1)
        # samplers
        binds = dict((r, t) for r, t in mat['bindings'])
        used = set(ps.samplers.values())
        for name, slot in ps.samplers.items():
            role = SAMPLER_ROLE.get(name); idx = None
            if role is not None and role in binds and binds[role] < len(w.textures): idx = tex_index(w.textures[binds[role]][1])
            if idx is None: idx = 1 if name == 'normalmap' else (2 if name == 'cubemap' else 0)
            sh[slot] = idx; sh[48 + slot] = 0
        sh[68] = np.float32(0.5).view(np.uint32)
        for fn, arr in (('vsc_%d.bin' % bi, vsc), ('psc_%d.bin' % bi, psc), ('shc_%d.bin' % bi, sh)): open(os.path.join(a.pkg, fn), 'wb').write(arr.astype('<f4' if arr.dtype == np.float32 else '<u4').tobytes())
        man += ['draw', 'vs ' + os.path.relpath(vs.spv, a.pkg), 'ps ' + os.path.relpath(ps.spv, a.pkg),
                'spec %d' % (2 if a.alpha_test else 0), 'vb %s %d %d' % (vfile, stride, n), 'ib %s %d' % (ifile, tri.size)]
        man += ['attr %d %d %d' % x for x in man_attr]
        man += ['vsconst vsc_%d.bin' % bi, 'psconst psc_%d.bin' % bi, 'shared shc_%d.bin' % bi, 'cull 0', 'blend 0', 'depthwrite 1']
        nd += 1
    open(os.path.join(a.pkg, 'manifest.txt'), 'w').write('\n'.join(man) + '\n')
    print('draws', nd, 'textures', len(heap), 'skipped', skipped)


if __name__ == '__main__':
    main()
