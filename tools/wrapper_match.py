#!/usr/bin/env python3
"""Co-occurrence of PC render wrappers and 360 render wrappers inside string-aligned function pairs.
usage: wrapper_match.py pc.asm func_pairs.json OUT.json"""
import bisect, collections, json, re, sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import asm as A
PCW = (0x1317000, 0x131d000)
XW = (0x8278e000, 0x82797000)
rx = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')
ins = []
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        m = rx.match(line)
        if m: ins.append((int(m.group(1), 16), m.group(2).strip()))
idxa = {a: i for i, (a, _) in enumerate(ins)}
starts = set()
for a, t in ins:
    m = re.match(r'call\s+0x([0-9a-f]+)$', t)
    if m: starts.add(int(m.group(1), 16))
prev = None
for a, t in ins:
    if prev and (prev[1] == 'int3' or prev[1].startswith('ret')) and not (t == 'int3' or t == 'nop' or t.startswith('xchg')):
        starts.add(a)
    prev = (a, t)
starts = sorted(starts)
def pc_calls(fn):
    k = bisect.bisect_right(starts, fn)
    end = starts[k] if k < len(starts) else fn + 0x2000
    i = idxa.get(fn); out = []
    while i is not None and i < len(ins) and ins[i][0] < end:
        m = re.match(r'call\s+0x([0-9a-f]+)$', ins[i][1])
        if m:
            t = int(m.group(1), 16)
            if PCW[0] <= t < PCW[1]: out.append(t)
        i += 1
    return out
idx = A.load()
def x_calls(fn):
    out = []
    for l in A.asm(fn, idx) or []:
        m = re.match(r'\s*bl 0x([0-9a-f]+)', l)
        if m:
            t = int(m.group(1), 16)
            if XW[0] <= t < XW[1]: out.append(t)
    return out
pairs = json.load(open(sys.argv[2]))
co = collections.defaultdict(collections.Counter)
pcn = collections.Counter(); xn = collections.Counter()
used = 0
for p, x, n, tot in pairs:
    a = set(pc_calls(p)); b = set(x_calls(x))
    if not a or not b: continue
    used += 1
    for P in a: pcn[P] += 1
    for W in b: xn[W] += 1
    for P in a:
        for W in b: co[P][W] += 1
print('pairs with wrapper calls on both sides:', used, file=sys.stderr)
res = []
for P, c in co.items():
    W, n = c.most_common(1)[0]
    second = c.most_common(2)[1][1] if len(c) > 1 else 0
    jac = n / (pcn[P] + xn[W] - n)
    res.append(dict(pc='%x' % P, x360='%x' % W, co=n, pc_n=pcn[P], x_n=xn[W], jaccard=round(jac, 2), second=second))
res.sort(key=lambda r: (-r['jaccard'], -r['co']))
json.dump(res, open(sys.argv[3], 'w'), indent=1)
for r in res[:60]: print(r)
