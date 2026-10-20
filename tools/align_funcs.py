#!/usr/bin/env python3
"""Match functions between PC MGRR.exe and the 360 image through shared string literals.
usage: align_funcs.py pc.asm OUT.json
A string counts as an anchor if it is referenced from exactly one function on each platform."""
import bisect, collections, json, re, sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import asm as A
from x360img import *

PC = '/home/lucidfear/Projects/mgr-nx/obb_extracted/main/MGRR/MGRR.exe'
pc = open(PC, 'rb').read()
# PE sections (VA, raw offset, size) for .rdata and .data
SECS = [(0x1804000, 0x1402600, 0x200212), (0x1a05000, 0x1602a00, 0x299000)]
def pc_strings():
    out = {}
    for va, raw, sz in SECS:
        blob = pc[raw:raw + sz]
        for m in re.finditer(rb'[\x20-\x7e]{8,}', blob):
            out[va + m.start()] = m.group().decode('latin-1')
    return out
pcs = pc_strings()

# 360 strings
im = Img()
xs = {}
for va, t in im.strings(rb'.', lo=0x82000400, hi=0x821F3A00, minlen=8):
    xs[va] = t
# also .data strings (0x83200000..0x839af200)
for va, t in im.strings(rb'.', lo=0x83200000, hi=0x839af200, minlen=8):
    xs[va] = t

# --- PC xrefs from asm text ---
rx = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')
ins = []
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        m = rx.match(line)
        if m: ins.append((int(m.group(1), 16), m.group(2).strip()))
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
def pc_fn(a):
    k = bisect.bisect_right(starts, a) - 1
    return starts[k] if k >= 0 else None
imm = re.compile(r'0x([0-9a-f]{6,8})\b')
pc_ref = collections.defaultdict(set)      # string text -> {fn}
for a, t in ins:
    if t.startswith(('call', 'jmp')): continue
    for m in imm.finditer(t):
        v = int(m.group(1), 16)
        s = pcs.get(v)
        if s: pc_ref[s].add(pc_fn(a))

# --- 360 xrefs ---
idx = A.load()
fstarts = sorted(idx)
def x_fn(a):
    k = bisect.bisect_right(fstarts, a) - 1
    return fstarts[k] if k >= 0 else None
x_ref = collections.defaultdict(set)
for va, t in xs.items():
    for r in im.xrefs(va):
        x_ref[t].add(x_fn(r))

anch = [s for s in pc_ref if s in x_ref and len(pc_ref[s]) == 1 and len(x_ref[s]) == 1]
print('PC strings referenced', len(pc_ref), '360 strings referenced', len(x_ref), 'shared unique anchors', len(anch), file=sys.stderr)
votes = collections.defaultdict(collections.Counter)
for s in anch:
    votes[next(iter(pc_ref[s]))][next(iter(x_ref[s]))] += 1
pairs = []
for p, c in votes.items():
    x, n = c.most_common(1)[0]
    pairs.append((p, x, n, sum(c.values())))
json.dump(pairs, open(sys.argv[2], 'w'))
print('function pairs', len(pairs), 'with >=2 shared strings', sum(1 for p in pairs if p[2] >= 2), file=sys.stderr)
