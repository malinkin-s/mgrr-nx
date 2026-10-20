#!/usr/bin/env python3
"""Decode sampler-state setters (leaf functions that edit the 6-dword texture fetch constant at dev+1152+24*sampler)
to (dword index, bit field) using the Xenos texture fetch constant layout."""
import re, sys, json
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import asm as A
# xe_gpu_texture_fetch_t layout: (dword, lsb range) -> name
FIELDS = {
 (0, (0, 1)): 'type', (0, (2, 3)): 'sign_x', (0, (4, 5)): 'sign_y', (0, (6, 7)): 'sign_z', (0, (8, 9)): 'sign_w',
 (0, (10, 12)): 'clamp_x (ADDRESSU)', (0, (13, 15)): 'clamp_y (ADDRESSV)', (0, (16, 18)): 'clamp_z (ADDRESSW)',
 (1, (0, 5)): 'format', (3, (1, 12)): 'swizzle', (3, (13, 18)): 'exp_adjust',
 (3, (19, 20)): 'mag_filter', (3, (21, 22)): 'min_filter', (3, (23, 24)): 'mip_filter', (3, (25, 27)): 'aniso_filter',
 (4, (0, 0)): 'vol_mag_filter', (4, (1, 1)): 'vol_min_filter', (4, (2, 5)): 'mip_min_level', (4, (6, 9)): 'mip_max_level',
 (4, (12, 21)): 'lod_bias', (5, (0, 1)): 'border_color', (5, (2, 2)): 'force_bcw_max', (5, (3, 4)): 'tri_clamp', (5, (5, 8)): 'aniso_bias',
}
def decode(a, idx):
    L = [l.strip() for l in (A.asm(a, idx) or []) if not l.strip().startswith('loc_')]
    if not any(re.match(r'mulli r\d+,r\d+,24$', t) for t in L): return None
    if not any(re.match(r'std r\d+,24\(r3\)', t) for t in L): return None
    words = set(); fields = []
    for t in L:
        m = re.match(r'(lwz|stw) r\d+,(\d+)\(r\d+\)$', t)
        if m and int(m.group(2)) in (0, 4, 8, 12, 16, 20): words.add(int(m.group(2)) // 4)
        if re.match(r'(lwzx|stwx) ', t): words.add(0)
        m = re.match(r'rlwimi r\d+,r\d+,(\d+),(\d+),(\d+)$', t)
        if m: fields.append((31 - int(m.group(3)), 31 - int(m.group(2)), int(m.group(1))))
    return dict(addr=a, n=len(L), words=sorted(words), fields=fields)
if __name__ == '__main__':
    idx = A.load(); out = []
    for a in sorted(x for x in idx if 0x826b7000 <= x < 0x826b8700):
        d = decode(a, idx)
        if not d: continue
        names = []
        for lo, hi, sh in d['fields']:
            for w in (d['words'] or [None]):
                n = FIELDS.get((w, (lo, hi)))
                if n: names.append(n); break
            else: names.append('dword%s[%d..%d]' % (d['words'], lo, hi))
        d['names'] = names; out.append(d)
        print('%X n=%-3d dwords=%s fields=%s' % (a, d['n'], d['words'], names))
    json.dump(out, open('/home/lucidfear/Projects/mgr-nx/x360_work/sampler_setters.json', 'w'), indent=1)
