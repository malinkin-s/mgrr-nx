#!/usr/bin/env python3
"""Xbox 360 (Xenos) texture decoding: endian swap, 2D untiling, DXT/uncompressed -> RGBA, PNG writer.

Tiling follows the public XGAddress2DTiledOffset algorithm (as used by libxenon/Xenia):
    offsets work on texel *blocks* (4x4 for DXT, 1x1 otherwise); width is rounded up to 32 blocks; log2_bpp = log2(bytes per block).
"""
import struct, sys, zlib
import numpy as np
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import wtb

def swap(buf, mode):
    a = np.frombuffer(buf, dtype=np.uint8)
    n = len(a) - len(a) % 4
    a = a[:n].copy()
    if mode == 1:   a = a.reshape(-1, 2)[:, ::-1]
    elif mode == 2: a = a.reshape(-1, 4)[:, ::-1]
    elif mode == 3: a = a.reshape(-1, 4)[:, [2, 3, 0, 1]]
    return np.ascontiguousarray(a).reshape(-1)

def tiled_offsets(wb, hb, log2bpp, pitch_blocks):
    """array [hb, wb] of byte offsets of each block inside the tiled surface."""
    y, x = np.mgrid[0:hb, 0:wb].astype(np.int64)
    macro = ((x >> 5) + (y >> 5) * (pitch_blocks >> 5)) << (log2bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log2bpp
    off = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log2bpp)) + ((y & 1) << 4)
    return ((off & ~511) << 3) + ((y & 16) << 7) + ((off & 448) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (off & 63)

def untile(data, wb, hb, bpb, pitch_blocks):
    log2 = bpb.bit_length() - 1
    offs = tiled_offsets(wb, hb, log2, pitch_blocks)
    src = np.frombuffer(data, dtype=np.uint8)
    need = int(offs.max()) + bpb
    if len(src) < need: src = np.concatenate([src, np.zeros(need - len(src), np.uint8)])
    idx = offs[..., None] + np.arange(bpb)
    return src[idx].reshape(hb, wb * bpb)

def rgb565(c):
    r = ((c >> 11) & 31) * 255 // 31; g = ((c >> 5) & 63) * 255 // 63; b = (c & 31) * 255 // 31
    return r, g, b

def dxt_colors(blocks, has_alpha_mode):
    """blocks: [N,8] uint8 (little-endian inside the block). returns [N,16,4] RGBA."""
    c0 = blocks[:, 0].astype(np.uint16) | (blocks[:, 1].astype(np.uint16) << 8)
    c1 = blocks[:, 2].astype(np.uint16) | (blocks[:, 3].astype(np.uint16) << 8)
    r0, g0, b0 = [np.asarray(v, dtype=np.int32) for v in rgb565(c0)]
    r1, g1, b1 = [np.asarray(v, dtype=np.int32) for v in rgb565(c1)]
    pal = np.zeros((len(blocks), 4, 4), np.int32); pal[..., 3] = 255
    pal[:, 0, :3] = np.stack([r0, g0, b0], 1); pal[:, 1, :3] = np.stack([r1, g1, b1], 1)
    four = (c0 > c1) | has_alpha_mode
    p0 = pal[:, 0, :3]; p1 = pal[:, 1, :3]
    pal[:, 2, :3] = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    pal[:, 3, :3] = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
    pal[:, 3, 3] = np.where(four | has_alpha_mode, 255, 0)
    bits = blocks[:, 4].astype(np.uint32) | (blocks[:, 5].astype(np.uint32) << 8) | (blocks[:, 6].astype(np.uint32) << 16) | (blocks[:, 7].astype(np.uint32) << 24)
    out = np.zeros((len(blocks), 16, 4), np.uint8)
    for i in range(16):
        sel = ((bits >> (2 * i)) & 3).astype(np.int64)
        out[:, i] = pal[np.arange(len(blocks)), sel]
    return out

def dxt5_alpha(a8):
    a0 = a8[:, 0].astype(np.int32); a1 = a8[:, 1].astype(np.int32)
    tab = np.zeros((len(a8), 8), np.int32); tab[:, 0] = a0; tab[:, 1] = a1
    for k in range(1, 7):
        tab[:, k + 1] = np.where(a0 > a1, ((7 - k) * a0 + k * a1) // 7, np.where(k < 5, ((5 - k) * a0 + k * a1) // 5, 0))
    tab[:, 6] = np.where(a0 > a1, tab[:, 6], 0); tab[:, 7] = np.where(a0 > a1, tab[:, 7], 255)
    bits = np.zeros(len(a8), np.uint64)
    for i in range(6): bits |= a8[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    out = np.zeros((len(a8), 16), np.uint8)
    for i in range(16):
        out[:, i] = tab[np.arange(len(a8)), ((bits >> np.uint64(3 * i)) & np.uint64(7)).astype(np.int64)]
    return out

def decode(t, data):
    """t: dict from wtb.parse; data: raw bytes of the base level. returns HxWx4 uint8 or None"""
    fmt = t['format']; w = t['width']; h = t['height']
    name, bw, bh, bpb = wtb.FORMATS.get(fmt, ('?', 1, 1, 4))
    raw = swap(data, t['endian']).tobytes()
    wb = (w + bw - 1) // bw; hb = (h + bh - 1) // bh
    pitch = max(((wb + 31) // 32) * 32, t['pitch'] * 32 // bw)
    lin = untile(raw, wb, hb, bpb, pitch) if t['tiled'] else np.frombuffer(raw[:wb * hb * bpb], np.uint8).reshape(hb, wb * bpb)
    if name in ('DXT1', 'DXT2_3', 'DXT4_5'):
        blocks = lin.reshape(hb * wb, bpb)
        if name == 'DXT1':
            px = dxt_colors(blocks, np.zeros(len(blocks), bool))
        else:
            px = dxt_colors(blocks[:, 8:], np.ones(len(blocks), bool))
            if name == 'DXT2_3':
                al = np.zeros((len(blocks), 16), np.uint8)
                for i in range(16):
                    byte = blocks[:, i // 2]; al[:, i] = ((byte >> (4 * (i & 1))) & 15) * 17
            else:
                al = dxt5_alpha(blocks[:, :8])
            px[:, :, 3] = al
        px = px.reshape(hb, wb, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(hb * 4, wb * 4, 4)
        return px[:h, :w]
    if name in ('8_8_8_8', '8_8_8_8_A'):
        a = lin.reshape(hb, wb, 4)[:h, :w]
        sw = t['swizzle']            # xyzw selectors: 0=R? Xenos: 0=X(R) 1=Y 2=Z 3=W 4=0 5=1
        comp = [a[..., s] if s < 4 else np.full(a.shape[:2], 255 if s == 5 else 0, np.uint8) for s in sw]
        return np.stack(comp, -1)
    if name == '8' or name == '8_A' or name == '8_B':
        a = lin.reshape(hb, wb)[:h, :w]
        return np.stack([a, a, a, np.full_like(a, 255)], -1)
    if name == '5_6_5':
        a = lin.reshape(hb, wb, 2)[:h, :w].astype(np.uint16); v = (a[..., 0] << 8) | a[..., 1]
        r, g, b = rgb565(v)
        return np.stack([np.asarray(r, np.uint8), np.asarray(g, np.uint8), np.asarray(b, np.uint8), np.full(v.shape, 255, np.uint8)], -1)
    return None

def write_png(path, rgba):
    h, w = rgba.shape[:2]
    raw = b''.join(b'\0' + rgba[y].tobytes() for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read(); out = sys.argv[2]
    ts = wtb.parse(d)
    for k, t in enumerate(ts):
        img = decode(t, d[t['off']:t['off'] + t['size']])
        print(k, t.get('fmt_name'), t.get('width'), t.get('height'), 'decoded' if img is not None else 'UNSUPPORTED')
        if img is not None: write_png(out if len(ts) == 1 else out.replace('.png', '_%d.png' % k), img)

# ---- mip levels -----------------------------------------------------------------------------------------------
def level_geometry(t, level):
    name, bw, bh, bpb = wtb.FORMATS.get(t['format'], ('?', 1, 1, 4))
    w = max(1, t['width'] >> level); h = max(1, t['height'] >> level)
    wb = (w + bw - 1) // bw; hb = (h + bh - 1) // bh
    return w, h, wb, hb, bpb, ((wb + 31) // 32) * 32, ((hb + 31) // 32) * 32

def packed_level(t):
    """first mip level stored in the packed tail tile (levels with max block dimension <= 16)."""
    name, bw, bh, bpb = wtb.FORMATS.get(t['format'], ('?', 1, 1, 4))
    wb = (t['width'] + bw - 1) // bw; hb = (t['height'] + bh - 1) // bh
    m = max(wb, hb)
    lg = (m - 1).bit_length()
    return max(0, lg - 4)

def level_offset(t, level):
    """byte offset of a (non-tail) mip level inside the texture blob."""
    if level == 0: return 0
    off = t['mip_addr']
    for l in range(1, level):
        w, h, wb, hb, bpb, aw, ah = level_geometry(t, l)
        off += aw * ah * bpb
    return off

def decode_level(t, blob, level):
    w, h, wb, hb, bpb, aw, ah = level_geometry(t, level)
    off = level_offset(t, level)
    s = dict(t); s['width'] = w; s['height'] = h; s['pitch'] = aw * wtb.FORMATS.get(t['format'], ('?', 1, 1, 4))[1] // 32
    return decode(s, blob[off:off + aw * ah * bpb])
