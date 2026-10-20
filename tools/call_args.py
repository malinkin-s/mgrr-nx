#!/usr/bin/env python3
"""Histogram of immediate arguments passed at call sites of given 360 functions.
360: looks back <=10 insns before `bl target` for `li rN,imm` (addi rN,0,imm) / lis+ori and returns {reg: imm}."""
import collections, sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
from x360img import *

def s16(x): return x - 0x10000 if x & 0x8000 else x

def call_sites(im, target):
    out = []
    for i, w in enumerate(im.W):
        if (w >> 26) == 18 and (w & 1):
            li = w & 0x03FFFFFC
            if li & 0x02000000: li -= 0x04000000
            a = TEXT_LO + i * 4
            if a + li == target:
                out.append(i)
    return out

def args_at(im, i, maxback=12):
    """{reg: imm} for r3..r10 set by li/lis+ori before call index i (stop at branches)."""
    regs = {}
    dead = set()
    for k in range(i - 1, max(i - maxback, 0), -1):
        w = im.W[k]; op = w >> 26
        if op in (16, 18) or w in (0x4E800020, 0x4E800420): break
        rd = (w >> 21) & 31; ra = (w >> 16) & 31
        if 3 <= rd <= 10 and rd not in regs and rd not in dead:
            if op == 14 and ra == 0: regs[rd] = s16(w & 0xFFFF)
            elif op == 15 and ra == 0: regs[rd] = (w & 0xFFFF) << 16
            elif op == 24 and ra == rd and rd in regs: pass
            elif op in (14, 15, 24, 32, 33, 34, 40, 21, 31) and not (op in (24,) and False):
                dead.add(rd)
    return regs

if __name__ == '__main__':
    im = Img()
    for t in sys.argv[1:]:
        t = int(t, 16)
        cs = call_sites(im, t)
        h = collections.defaultdict(collections.Counter)
        for i in cs:
            for r, v in args_at(im, i).items():
                h[r][v] += 1
        print('%x callsites=%d' % (t, len(cs)), {('r%d' % r): dict(c.most_common(6)) for r, c in sorted(h.items()) if r <= 5})
