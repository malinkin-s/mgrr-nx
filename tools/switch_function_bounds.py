#!/usr/bin/env python3
"""Explicit function bounds for non-.pdata functions that contain jump tables.

XenonRecomp's own boundary analysis cuts leaf functions at their jump table
("Switch case ... is trying to jump outside function"). For every switch whose
bctr is not inside a .pdata function we:
  1. take the nearest known function start below the bctr (pdata starts + `bl` targets),
  2. flow-walk from there (conditional branches, `b` that is not a tail call to a known
     start, every switch label) until blr / ends,
  3. check the walk really reaches the bctr (otherwise the start guess is wrong),
  4. emit {address, size} lines for the recompiler config.

usage: switch_function_bounds.py IMAGE.bin switch_tables.toml OUT.toml
"""
import bisect
import re
import struct
import sys

img = open(sys.argv[1], 'rb').read()
B = 0x82000000
LO, SZ = 0x82270000, 0xF8D75C
HI = LO + SZ
n = SZ // 4
W = struct.unpack('>%dI' % n, img[LO - B:LO - B + n * 4])


def word(a):
    return W[(a - LO) >> 2]


PD, PN = 0x821F3A00 - B, 0x6F608 // 8
pd = []
for i in range(PN):
    a, f = struct.unpack_from('>II', img, PD + i * 8)
    pd.append((a, ((f >> 8) & 0x3FFFFF) * 4))
pd.sort()
pd_starts = [a for a, _ in pd]


def in_pdata(a):
    k = bisect.bisect_right(pd_starts, a) - 1
    return k >= 0 and pd[k][0] <= a < pd[k][0] + pd[k][1]


starts = set(pd_starts)
for i, w in enumerate(W):
    if (w >> 26) == 18 and (w & 3) == 1:                       # bl
        li = w & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        t = LO + i * 4 + li
        if LO <= t < HI:
            starts.add(t)
for i in range(1, n):
    if W[i] != 0 and W[i - 1] == 0:                              # first word after zero padding
        starts.add(LO + i * 4)
firm = sorted(starts)
soft = set()
for i in range(n - 1):
    w = W[i]
    if w in (0x4E800020, 0x4E800420) or ((w >> 26) == 18 and not (w & 1)):    # blr / bctr / unconditional b
        if W[i + 1] != 0:
            soft.add(LO + (i + 1) * 4)
starts = sorted(set(firm) | soft)

txt = open(sys.argv[2]).read()
tabs = {}
tabdata = {}
for m in re.finditer(r'# table = (0x[0-9A-F]+) entsize = (\d+) count = (\d+)\n\[\[switch\]\]\nbase = (0x[0-9A-F]+)\nr = \d+\ndefault = 0x[0-9A-F]+\nlabels = \[(.*?)\]', txt, re.S):
    b = int(m.group(4), 16)
    tabs[b] = [int(x, 16) for x in re.findall(r'0x[0-9A-F]+', m.group(5))]
    tabdata[b] = (int(m.group(1), 16), int(m.group(2)) * int(m.group(3)))


def walk(start):
    seen = set()
    todo = [start]
    hi = start
    while todo:
        a = todo.pop()
        while LO <= a < HI and a not in seen:
            seen.add(a)
            hi = max(hi, a + 4)
            w = word(a)
            op = w >> 26
            if op == 18:                                       # b / bl
                li = w & 0x03FFFFFC
                if li & 0x02000000:
                    li -= 0x04000000
                t = a + li if not (w & 2) else li
                if w & 1:                                       # bl: continues
                    a += 4
                    continue
                if t != start and (t in starts_set or abs(t - a) > 0x8000):   # tail call
                    break
                todo.append(t)
                break
            if op == 16:                                       # bc
                d = w & 0xFFFC
                if d & 0x8000:
                    d -= 0x10000
                if not (w & 1):
                    todo.append(a + d)
                a += 4
                continue
            if w == 0x4E800020:                                # blr
                break
            if w == 0x4E800420:                                # bctr
                for t in tabs.get(a, []):
                    todo.append(t)
                break
            if (w & 0xFC00FFFF) == 0x4C000020 and ((w >> 21) & 31) != 20:  # bclr conditional
                a += 4
                continue
            a += 4
    return seen, hi


starts_set = set(firm)
incoming = {}                                  # target -> list of source addrs (b / bc)
for i, w in enumerate(W):
    op = w >> 26
    a = LO + i * 4
    if op == 18 and not (w & 1):
        li = w & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        incoming.setdefault(a + li if not (w & 2) else li, []).append(a)
    elif op == 16 and not (w & 1):
        d = w & 0xFFFC
        if d & 0x8000:
            d -= 0x10000
        incoming.setdefault(a + d, []).append(a)


def entered_from_outside(s, seen, limit):
    # a branch from before s into the head of the function (s..limit) means s is mid-function
    for t in (x for x in seen if s < x < limit):
        for src in incoming.get(t, ()):
            if src < s and src >= s - 0x4000:
                return True
    return False
out, bad = [], []
for bctr, labels in sorted(tabs.items()):
    if in_pdata(bctr):
        continue
    k = bisect.bisect_right(starts, bctr) - 1
    good = None
    for kk in range(k, max(k - 40, -1), -1):
        s = starts[kk]
        seen, hi = walk(s)
        if hi - s <= 0x10000 and bctr in seen and all(l in seen for l in labels) and not entered_from_outside(s, seen, bctr):
            good = (s, hi)
            break
    if not good:
        bad.append((bctr, starts[k]))
        continue
    s, hi = good
    T, tsz = tabdata[bctr]
    if bctr < T < bctr + 0x400:
        hi = max(hi, (T + tsz + 3) & ~3)
    out.append((s, hi - s))
dedup = sorted(set(out))
with open(sys.argv[3], 'w') as f:
    f.write('functions = [\n')
    for s, sz in dedup:
        f.write('    { address = 0x%X, size = 0x%X },\n' % (s, sz))
    f.write(']\n')
print('non-pdata switches:', len(out) + len(bad), ' functions emitted:', len(dedup), ' failed:', len(bad))
for b, s in bad[:15]:
    print('  bctr %08X guessed start %08X' % (b, s))
