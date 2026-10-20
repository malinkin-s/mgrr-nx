#!/usr/bin/env python3
"""Decode the Xbox 360 D3D 'state setter' leaf functions into (Xenos register, bit field).

Mechanics (read from the flush code in DrawVertices 0x826BC928 and helper 0x826CA450):
 the device keeps 64-bit dirty masks at dev+0, +8, +16, +24, +32. A setter writes a word of a shadow register array
 and sets the dirty bit that stands for that register. The flush walks the mask from the MSB with cntlzd, so a mask
 bit maps to  register = base + p,  shadow word = shadow + 4*p,  with p counted from the top of the field.

 dirty qword  bits (LSB numbering)   p            reg base  shadow
 dev+16  A    0..11                  11 - j       0x2200    10548
 dev+16  B    12..16                 16 - j       0x2180    10528
 dev+16  C    42..57                 57 - j       0x2000    10368
 dev+16  D    21..41                 41 - j       0x2100    10444
 dev+24  E    34..54                 54 - j       0x2280    10596
 dev+32  F    38..45                 45 - j       0x2380    10832
 dev+32  G    0..37                  37 - j       0x2300    10680
usage: decode_setters.py [addr ...]
"""
import re
import sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import asm as A

GROUPS = {  # qword offset -> list of (lo, hi, top, regbase, shadow)
    16: [(0, 11, 11, 0x2200, 10548), (12, 16, 16, 0x2180, 10528), (42, 57, 57, 0x2000, 10368), (21, 41, 41, 0x2100, 10444)],
    24: [(34, 54, 54, 0x2280, 10596)],
    32: [(38, 45, 45, 0x2380, 10832), (0, 37, 37, 0x2300, 10680)],
}
# Xenos registers we are fairly sure about (names as in public Xenos/Xenia register tables; layouts from memory)
REG = {0x2200: 'RB_DEPTHCONTROL', 0x2201: 'RB_BLENDCONTROL_0', 0x2202: 'RB_COLORCONTROL', 0x2203: 'RB_HIZCONTROL',
       0x2204: 'PA_CL_CLIP_CNTL', 0x2205: 'PA_SU_SC_MODE_CNTL', 0x2206: 'PA_CL_VTE_CNTL',
       0x2100: 'VGT_MAX_VTX_INDX', 0x2101: 'VGT_MIN_VTX_INDX', 0x2102: 'VGT_INDX_OFFSET', 0x2103: 'VGT_MULTI_PRIM_IB_RESET_INDX',
       0x2104: 'RB_COLOR_MASK', 0x2105: 'RB_BLENDRED', 0x2106: 'RB_BLENDGREEN', 0x2107: 'RB_BLENDBLUE', 0x2108: 'RB_BLENDALPHA',
       0x210C: 'RB_STENCILREFMASK_BF', 0x210D: 'RB_STENCILREFMASK', 0x210E: 'RB_ALPHA_REF',
       0x210F: 'PA_CL_VPORT_XSCALE', 0x2110: 'PA_CL_VPORT_XOFFSET', 0x2111: 'PA_CL_VPORT_YSCALE', 0x2112: 'PA_CL_VPORT_YOFFSET',
       0x2113: 'PA_CL_VPORT_ZSCALE', 0x2114: 'PA_CL_VPORT_ZOFFSET',
       0x2280: 'PA_SU_POINT_SIZE', 0x2281: 'PA_SU_POINT_MINMAX', 0x2282: 'PA_SU_LINE_CNTL'}


def reg_of(qoff, bit):
    for lo, hi, top, base, sh in GROUPS.get(qoff, []):
        if lo <= bit <= hi:
            p = top - bit
            return base + p, sh + 4 * p
    return None, None


def decode(addr, idx=None):
    L = [l.strip() for l in (A.asm(addr, idx) or []) if not l.strip().startswith('loc_')]
    info = dict(addr=addr, n=len(L), dirty=[], words=[], fields=[], raw=L)
    regs = {}                      # register -> value-tracking name
    ldq = {}
    consts = {}                    # register -> constant bit value (li rX,1 ; rldicr rY,rX,N,63)
    for t in L:
        m = re.match(r'li (r\d+),1$', t)
        if m: consts[m.group(1)] = 1; continue
        m = re.match(r'rldicr (r\d+),(r\d+),(\d+),63$', t)
        if m and consts.get(m.group(2)) == 1:
            consts[m.group(1)] = 1 << int(m.group(3)); continue
        m = re.match(r'or (r\d+),(r\d+),(r\d+)$', t)
        if m:
            for a_, b_ in ((m.group(2), m.group(3)), (m.group(3), m.group(2))):
                if a_ in ldq and b_ in consts and consts[b_] >= 1 << 32:
                    q = ldq[a_]
                    for b in range(32, 64):
                        if consts[b_] >> b & 1: info['dirty'].append((q, b))
                    ldq[m.group(1)] = q
            continue
        m = re.match(r'ld (r\d+),(\d+)\(r3\)$', t)
        if m: ldq[m.group(1)] = int(m.group(2)); continue
        m = re.match(r'(ori|oris) (r\d+),(r\d+),(\d+)$', t)
        if m and m.group(3) in ldq:
            q = ldq[m.group(3)]
            v = int(m.group(4)) << (16 if m.group(1) == 'oris' else 0)
            ldq[m.group(2)] = q
            for b in range(32):
                if v >> b & 1: info['dirty'].append((q, b))
            continue
        m = re.match(r'stw (r\d+),(\d+)\(r3\)$', t)
        if m: info['words'].append(int(m.group(2)))
        m = re.match(r'rlwimi (r\d+),(r\d+),(\d+),(\d+),(\d+)$', t)
        if m:
            sh, mb, me = int(m.group(3)), int(m.group(4)), int(m.group(5))
            info['fields'].append((31 - me, 31 - mb, sh))   # LSB range of the inserted field, rotate amount
    regs_out = []
    for q, b in info['dirty']:
        r, w = reg_of(q, b)
        regs_out.append((q, b, r, w))
    info['regs'] = regs_out
    return info


def fmt(info):
    out = []
    for q, b, r, w in info['regs']:
        if r is None:
            out.append('dev+%d bit%d (group unknown)' % (q, b))
        else:
            out.append('dev+%d bit%d -> reg 0x%04X %s (shadow dev+%d)' % (q, b, r, REG.get(r, ''), w))
    f = ', '.join('field[%d..%d]%s' % (lo, hi, '' if True else '') for lo, hi, sh in info['fields'])
    return '%X insns=%d  %s  | %s' % (info['addr'], info['n'], '; '.join(out) if out else 'no dirty bits found', f)


if __name__ == '__main__':
    idx = A.load()
    for x in sys.argv[1:]:
        print(fmt(decode(int(x, 16), idx)))
