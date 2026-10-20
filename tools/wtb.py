#!/usr/bin/env python3
"""Xbox 360 Platinum WTB texture bundle reader (big-endian) + Xenos fetch-constant decoder.

WTB: magic 00 42 54 57, ver, count, offsets[], sizes[], flags[], ids[], texinfo[] (52-byte D3DBaseTexture each).
texinfo: Common, RefCount, Fence, ReadFence, Identifier, BaseFlush, MipFlush, then 6-dword GPUTEXTURE_FETCH_CONSTANT.
"""
import struct, sys

FORMATS = {2: ('8', 1, 1, 1), 3: ('1_5_5_5', 1, 1, 2), 4: ('5_6_5', 1, 1, 2), 5: ('6_5_5', 1, 1, 2), 6: ('8_8_8_8', 1, 1, 4),
           7: ('2_10_10_10', 1, 1, 4), 8: ('8_A', 1, 1, 1), 9: ('8_B', 1, 1, 1), 10: ('8_8', 1, 1, 2),
           11: ('Cr_Y1_Cb_Y0', 2, 1, 4), 12: ('Y1_Cr_Y0_Cb', 2, 1, 4), 14: ('8_8_8_8_A', 1, 1, 4), 15: ('4_4_4_4', 1, 1, 2),
           16: ('10_11_11', 1, 1, 4), 17: ('11_11_10', 1, 1, 4), 18: ('DXT1', 4, 4, 8), 19: ('DXT2_3', 4, 4, 16), 20: ('DXT4_5', 4, 4, 16),
           22: ('24_8', 1, 1, 4), 23: ('24_8_FLOAT', 1, 1, 4), 24: ('16', 1, 1, 2), 25: ('16_16', 1, 1, 4), 26: ('16_16_16_16', 1, 1, 8),
           30: ('16_FLOAT', 1, 1, 2), 31: ('16_16_FLOAT', 1, 1, 4), 32: ('16_16_16_16_FLOAT', 1, 1, 8),
           33: ('32', 1, 1, 4), 34: ('32_32', 1, 1, 8), 35: ('32_32_32_32', 1, 1, 16), 36: ('32_FLOAT', 1, 1, 4),
           37: ('32_32_FLOAT', 1, 1, 8), 38: ('32_32_32_32_FLOAT', 1, 1, 16), 49: ('DXN', 4, 4, 16), 58: ('DXT3A', 4, 4, 8),
           59: ('DXT5A', 4, 4, 8), 60: ('CTX1', 4, 4, 8)}   # name, block w, block h, bytes per block
DIMS = {0: '1D', 1: '2D', 2: '3D', 3: 'cube'}
ENDIAN = {0: 'none', 1: '8in16', 2: '8in32', 3: '16in32'}

def fetch(d):
    d0, d1, d2, d3, d4, d5 = d
    fmt = d1 & 63
    t = dict(type=d0 & 3, pitch=(d0 >> 22) & 0x1ff, tiled=d0 >> 31, clamp=((d0 >> 10) & 7, (d0 >> 13) & 7, (d0 >> 16) & 7),
             format=fmt, endian=(d1 >> 6) & 3, base=(d1 >> 12) << 12, dim=(d5 >> 9) & 3, packed_mips=(d5 >> 11) & 1,
             mip_addr=(d5 >> 12) << 12, mip_min=(d4 >> 2) & 15, mip_max=(d4 >> 6) & 15,
             swizzle=((d3 >> 1) & 7, (d3 >> 4) & 7, (d3 >> 7) & 7, (d3 >> 10) & 7),
             mag=(d3 >> 19) & 3, min=(d3 >> 21) & 3, mip=(d3 >> 23) & 3)
    dim = t['dim']
    if dim == 1 or dim == 3:                    # 2D / cube
        t['width'] = (d2 & 0x1fff) + 1; t['height'] = ((d2 >> 13) & 0x1fff) + 1; t['depth'] = ((d2 >> 26) & 63) + 1
    elif dim == 2:                              # 3D
        t['width'] = (d2 & 0x7ff) + 1; t['height'] = ((d2 >> 11) & 0x7ff) + 1; t['depth'] = ((d2 >> 22) & 0x3ff) + 1
    else:
        t['width'] = (d2 & 0xffffff) + 1; t['height'] = 1; t['depth'] = 1
    nm = FORMATS.get(fmt)
    t['fmt_name'] = nm[0] if nm else 'fmt%d' % fmt
    return t

def parse(d):
    assert d[:4] == b'\0BTW', d[:4]
    ver, n, o_off, o_siz, o_flg, o_id, o_inf = struct.unpack_from('>7I', d, 4)
    offs = struct.unpack_from('>%dI' % n, d, o_off); sizes = struct.unpack_from('>%dI' % n, d, o_siz)
    flags = struct.unpack_from('>%dI' % n, d, o_flg); ids = struct.unpack_from('>%dI' % n, d, o_id)
    out = []
    for i in range(n):
        info = struct.unpack_from('>13I', d, o_inf + 52 * i) if o_inf else None
        t = fetch(info[7:13]) if info else {}
        t.update(off=offs[i], size=sizes[i], flags=flags[i], id=ids[i], common=info[0] if info else 0)
        out.append(t)
    return out

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    for t in parse(d): print(t)
