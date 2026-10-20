#!/usr/bin/env python3
"""PC side: list functions in an address window of MGRR.exe with caller counts, D3D vtable offsets used, and
histogram of constant args at call sites (push imm / mov ecx|edx,imm before `call func`).
usage: pc_args.py pc.asm LO HI [min_callers]"""
import collections, re, sys
asm, LO, HI = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
minc = int(sys.argv[4]) if len(sys.argv) > 4 else 1
rx = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')
ins = []
with open(asm, errors='replace') as f:
    for line in f:
        m = rx.match(line)
        if m: ins.append((int(m.group(1), 16), m.group(2).strip()))
idx = {a: i for i, (a, _) in enumerate(ins)}
sites = collections.defaultdict(list)
for i, (a, t) in enumerate(ins):
    m = re.match(r'call\s+0x([0-9a-f]+)$', t)
    if m and LO <= int(m.group(1), 16) < HI:
        sites[int(m.group(1), 16)].append(i)
vt = re.compile(r'call\s+DWORD PTR \[e[a-z]{2}\+0x([0-9a-f]+)\]')
def args(i):
    pushes = []
    regs = {}
    for k in range(i - 1, max(i - 14, 0), -1):
        t = ins[k][1]
        if t.startswith(('call', 'jmp', 'j', 'ret')): break
        m = re.match(r'push\s+(0x[0-9a-f]+|\d+)$', t)
        if m: pushes.append(int(m.group(1), 0))
        elif t.startswith('push'): pushes.append(None)
        m = re.match(r'mov\s+(ecx|edx),(0x[0-9a-f]+|\d+)$', t)
        if m and m.group(1) not in regs: regs[m.group(1)] = int(m.group(2), 0)
        if re.match(r'xor\s+(ecx|edx),(ecx|edx)$', t) and t.split()[1].split(',')[0] not in regs: regs[t.split()[1].split(',')[0]] = 0
    return pushes, regs
res = []
for fn, lst in sorted(sites.items()):
    if len(lst) < minc: continue
    # function body (until next ret) to see d3d vtable use
    i0 = idx.get(fn); offs = []
    if i0 is not None:
        for k in range(i0, min(i0 + 60, len(ins))):
            m = vt.search(ins[k][1])
            if m: offs.append(int(m.group(1), 16))
            if ins[k][1].startswith('ret'): break
    hist = collections.defaultdict(collections.Counter)
    for i in lst:
        p, r = args(i)
        for n, v in enumerate(p[:4]):
            if v is not None: hist['p%d' % n][v] += 1
        for rr, v in r.items(): hist[rr][v] += 1
    res.append((fn, len(lst), [hex(o) for o in offs if o >= 0x90], {k: dict(c.most_common(5)) for k, c in hist.items()}))
for fn, n, offs, h in sorted(res, key=lambda r: -r[1]):
    print('%x callers=%-4d vt=%s %s' % (fn, n, offs, h))
