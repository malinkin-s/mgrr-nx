#!/usr/bin/env python3
"""Jump-table detector for Xbox 360 images (XenonRecomp switch_table TOML).

XenonAnalyse only knows a handful of rigid instruction sequences. MGR's compiler
reorders them (lis / rlwinm / addi / lhzx / lis / addi / nop / add ...), so this
tool instead walks back from every `mtctr; bctr`, finds the bounding
`cmplwi crN, rI, N; bgt/ble crN`, and symbolically executes the straight-line
code between them. A table is accepted only if the target register is
`const + table[idx] << k` (or `table[idx] << k`) and idx is still intact at bctr.

usage: find_jumptables.py IMAGE.bin OUT.toml [--base 0x82000000]
           [--text 0x82270000:0xF8D75C] [-v]
"""
import argparse
import struct
import sys

M32 = 0xFFFFFFFF


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def rol32(v, n):
    n &= 31
    return ((v << n) | (v >> (32 - n))) & M32 if n else v


def mask32(mb, me):
    # PowerPC MASK(mb, me) over 32-bit, bit 0 = MSB
    if mb <= me:
        return (M32 >> mb) & (M32 << (31 - me)) & M32
    return ((M32 >> mb) | (M32 << (31 - me))) & M32


class Image:
    def __init__(self, path, base):
        self.data = open(path, 'rb').read()
        self.base = base

    def u(self, addr, size):
        o = addr - self.base
        if o < 0 or o + size > len(self.data):
            raise IndexError(hex(addr))
        return int.from_bytes(self.data[o:o + size], 'big')


# abstract values
#   ('c', v)                      constant
#   ('i', s)                      idx << s
#   ('l', T, size, post)          table_T[idx] << post
#   ('t', B, T, size, post)       B + (table_T[idx] << post)
def analyse(img, text_lo, text_hi, verbose=False):
    words = {}
    n = (text_hi - text_lo) // 4
    blob = img.data[text_lo - img.base:text_hi - img.base]
    arr = struct.unpack('>%dI' % n, blob)

    def W(i):
        return arr[i]

    out = []
    unresolved = []
    stats = {'bctr': 0, 'mtctr': 0, 'bound': 0, 'resolved': 0}

    for i in range(2, n):
        if W(i) != 0x4E800420:
            continue
        stats['bctr'] += 1
        addr = text_lo + i * 4
        mt = W(i - 1)
        if (mt & 0xFC1FFFFF) != 0x7C0903A6:
            continue
        stats['mtctr'] += 1
        rctr = (mt >> 21) & 31

        # find nearest bgt/ble crN then cmplwi crN
        bi = None
        for j in range(i - 2, max(i - 48, 0), -1):
            w = W(j)
            op = w >> 26
            if op == 16 and not (w & 1) and not (w & 2):          # bc, no link, no abs
                BO, BI = (w >> 21) & 31, (w >> 16) & 31
                if BI & 3 == 1 and BO in (12, 4):
                    bi = (j, BO, BI, s16(w & 0xFFFC) if not (w & 0x8000) else (w & 0xFFFC) - 0x10000)
                    break
                # other conditional branch: keep scanning (matches XenonAnalyse)
            elif op == 19 and ((w >> 1) & 0x3FF) == 16:             # bclr (bgtlr/blelr)
                BO, BI = (w >> 21) & 31, (w >> 16) & 31
                if BI & 3 == 1 and BO in (12, 4):
                    bi = (j, BO, BI, None)
                    break
            elif op == 18 or w == 0x4E800020 or w == 0x4E800420:    # b / blr / bctr -> block boundary
                break
        if bi is None:
            continue
        jb, BO, BI, disp = bi
        crf = BI >> 2
        # cmplwi crf, rA, N within 6 insns before the branch
        jc = None
        for j in range(jb - 1, max(jb - 7, 0), -1):
            w = W(j)
            if (w >> 26) == 10 and ((w >> 23) & 7) == crf and not ((w >> 21) & 1):
                jc = j
                break
        if jc is None:
            continue
        stats['bound'] += 1
        w = W(jc)
        ridx = (w >> 16) & 31
        count = (w & 0xFFFF) + 1
        if disp is None:
            default = 0
        elif BO == 12:                       # bgt -> default at target
            default = text_lo + jb * 4 + disp
        else:                                # ble -> falls through to default
            default = text_lo + (jb + 1) * 4

        # symbolic execution jc+1 .. i-2 (mtctr at i-1)
        R = [None] * 32
        R[ridx] = ('i', 0)
        ok = True
        for j in range(jc + 1, i - 1):
            if j == jb:
                continue
            w = W(j)
            op = w >> 26
            rd, ra, rb = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
            xo = (w >> 1) & 0x3FF
            if op in (16, 18) or (op == 19):
                ok = False                   # control flow inside the dispatch sequence
                break
            if op == 15:                     # addis / lis
                imm = (w & 0xFFFF) << 16
                if ra == 0:
                    R[rd] = ('c', imm & M32)
                elif R[ra] and R[ra][0] == 'c':
                    R[rd] = ('c', (R[ra][1] + imm) & M32)
                else:
                    R[rd] = None
            elif op == 14:                   # addi
                imm = s16(w & 0xFFFF)
                if ra == 0:
                    R[rd] = ('c', imm & M32)
                elif R[ra] and R[ra][0] == 'c':
                    R[rd] = ('c', (R[ra][1] + imm) & M32)
                else:
                    R[rd] = None
            elif op == 24:                   # ori rA(=bits11-15), rS(=bits6-10)
                rs, rA = rd, ra
                imm = w & 0xFFFF
                if imm == 0:
                    R[rA] = R[rs]
                elif R[rs] and R[rs][0] == 'c':
                    R[rA] = ('c', R[rs][1] | imm)
                else:
                    R[rA] = None
            elif op == 21:                   # rlwinm rA, rS, SH, MB, ME  (Rc ignored)
                rs, rA = rd, ra
                sh, mb, me = (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31
                v = R[rs]
                nv = None
                if v:
                    if v[0] == 'c':
                        nv = ('c', rol32(v[1], sh) & mask32(mb, me))
                    elif mb == 0 and me == 31 - sh:     # pure left shift
                        if v[0] == 'i':
                            nv = ('i', v[1] + sh)
                        elif v[0] == 'l':
                            nv = ('l', v[1], v[2], v[3] + sh)
                R[rA] = nv
            elif op == 31 and xo in (87, 279, 23):       # lbzx lhzx lwzx
                size = {87: 1, 279: 2, 23: 4}[xo]
                sa, sb = R[ra] if ra else ('c', 0), R[rb]
                nv = None
                lg = size.bit_length() - 1
                for c_, i_ in ((sa, sb), (sb, sa)):
                    if c_ and i_ and c_[0] == 'c' and i_[0] == 'i' and i_[1] == lg:
                        nv = ('l', c_[1], size, 0)
                        break
                R[rd] = nv
            elif op == 31 and xo == 266 and not (w & 0x400):   # add
                a, b = R[ra], R[rb]
                nv = None
                for x, y in ((a, b), (b, a)):
                    if x and y and x[0] == 'c' and y[0] == 'l':
                        nv = ('t', x[1], y[1], y[2], y[3])
                        break
                    if x and y and x[0] == 'c' and y[0] == 'c':
                        nv = ('c', (x[1] + y[1]) & M32)
                        break
                R[rd] = nv
            elif op == 31 and xo == 444:                 # or / mr rA, rS, rB
                rs, rA = rd, ra
                R[rA] = R[rs] if rs == rb else None
            else:
                # unknown instruction: conservatively clobber both possible dest fields,
                # except for stores / compares / nops which do not write GPR rd/ra
                if op in (36, 37, 38, 39, 44, 45, 11, 10, 52, 53, 54, 55, 47, 46):
                    continue
                if op == 31 and xo in (0, 32, 150, 151, 215, 247, 407, 439, 662, 150):
                    continue
                R[rd] = None
                R[ra] = None
        if not ok:
            unresolved.append((addr, 'control flow'))
            continue
        v = R[rctr]
        if R[ridx] != ('i', 0):
            unresolved.append((addr, 'index clobbered'))
            continue
        if not v or v[0] not in ('t', 'l'):
            unresolved.append((addr, 'ctr not a table target: %r' % (v,)))
            continue
        if v[0] == 't':
            _, B, T, size, post = v
        else:
            B, (_, T, size, post) = 0, v
        labels = []
        try:
            for k in range(count):
                e = img.u(T + k * size, size)
                labels.append((B + (e << post)) & M32)
        except IndexError:
            unresolved.append((addr, 'table out of image'))
            continue
        bad = [l for l in labels if l % 4 or not (text_lo <= l < text_hi)]
        if bad:
            unresolved.append((addr, 'bad labels %s' % ' '.join(hex(x) for x in bad[:3])))
            continue
        stats['resolved'] += 1
        out.append(dict(base=addr, r=ridx, default=default, labels=labels, table=T, size=size, post=post))
    return out, unresolved, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('out')
    ap.add_argument('--base', default='0x82000000')
    ap.add_argument('--text', default='0x82270000:0xF8D75C')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    base = int(a.base, 0)
    lo, size = (int(x, 0) for x in a.text.split(':'))
    img = Image(a.image, base)
    tabs, unres, st = analyse(img, lo, lo + size, a.v)
    with open(a.out, 'w') as f:
        f.write('# Generated by find_jumptables.py\n')
        for t in tabs:
            f.write('# table = 0x%X entsize = %d count = %d\n' % (t['table'], t['size'], len(t['labels'])))
            f.write('[[switch]]\nbase = 0x%X\nr = %d\ndefault = 0x%X\nlabels = [\n' % (t['base'], t['r'], t['default']))
            for l in t['labels']:
                f.write('    0x%X,\n' % l)
            f.write(']\n\n')
    print('bctr=%(bctr)d with-mtctr=%(mtctr)d bounded=%(bound)d resolved=%(resolved)d' % st, file=sys.stderr)
    from collections import Counter
    print('table element kinds (size,post):', dict(Counter((t['size'], t['post']) for t in tabs)), file=sys.stderr)
    if unres:
        print('unresolved bounded dispatches: %d' % len(unres), file=sys.stderr)
        for addr, why in unres[:40 if a.v else 12]:
            print('  %08X  %s' % (addr, why), file=sys.stderr)


if __name__ == '__main__':
    main()
