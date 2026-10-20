#!/usr/bin/env python3
"""Render a WMB to a PNG (front / side / top orthographic, flat shaded, coloured per mesh) to eyeball parsing correctness.
usage: wmb_preview.py MODEL.wmb OUT.png   (needs numpy + pillow)"""
import sys, colorsys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import numpy as np
from PIL import Image, ImageDraw
import wmb
m = wmb.WMB(open(sys.argv[1], 'rb').read())
mesh_of = {b: k for k, me in enumerate(m.meshes) for b in me['batches']}
tris = []     # (verts[3,3], normal[3,3], mesh)
attr = {g: m.attributes(g) for g in range(len(m.groups))}
for bi, b in enumerate(m.batches):
    a = attr[b['group']]; t = m.triangles(bi)
    if not len(t): continue
    vs = b['vertex_start']
    P = a['position'][vs + t]; N = a['normal'][vs + t]
    for k in range(len(t)): tris.append((P[k], N[k], mesh_of.get(bi, 0)))
allp = np.concatenate([t[0] for t in tris]); lo, hi = allp.min(0), allp.max(0)
size = 520; ctr = (lo + hi) / 2; ext = (hi - lo).max() / 2 * 1.05
views = [(0, 1, 2, 'front'), (2, 1, 0, 'side'), (0, 2, 1, 'top')]
img = Image.new('RGB', (size * 3, size), (20, 20, 24))
L = np.array([0.4, 0.6, 0.7]); L /= np.linalg.norm(L)
for vi, (ax, ay, az, nm) in enumerate(views):
    dr = ImageDraw.Draw(img); order = sorted(tris, key=lambda t: t[0][:, az].mean())
    for P, N, me in order:
        n = N.mean(0); n /= (np.linalg.norm(n) + 1e-9)
        sh = 0.35 + 0.65 * max(0.0, float(n @ L))
        r, g, bl = colorsys.hsv_to_rgb((me * 0.13) % 1.0, 0.55, sh)
        pts = [(vi * size + (p[ax] - ctr[ax]) / ext * size / 2 + size / 2, size - ((p[ay] - ctr[ay]) / ext * size / 2 + size / 2)) for p in P]
        dr.polygon(pts, fill=(int(r * 255), int(g * 255), int(bl * 255)))
img.save(sys.argv[2]); print('triangles', len(tris), 'saved', sys.argv[2])
