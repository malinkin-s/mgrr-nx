#!/usr/bin/env python3
"""Match wrappers PC<->360 through identical sequences of constant args in functions that call several wrappers.
usage: sigmatch.py pc.asm OUT.json"""
import bisect, collections, json, pickle, re, sys
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import asm as A
from x360img import *
import call_args as C
PCW = (0x1317000, 0x131d000); XW = (0x8278e000, 0x82797000)
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
sidx = {s: i for i, s in enumerate(starts)}

def pc_arg0(i):
    # cdecl/stdcall: first arg is the LAST push before the call; fastcall/thiscall: ecx
    for k in range(i - 1, max(i - 8, 0), -1):
        t = ins[k][1]
        if t.startswith(('call', 'j', 'ret')): return None
        m = re.match(r'push\s+(0x[0-9a-f]+|\d+)$', t)
        if m: return int(m.group(1), 0)
        if t.startswith('push'): return None
        m = re.match(r'mov\s+ecx,(0x[0-9a-f]+|\d+)$', t)
        if m: return int(m.group(1), 0)
        if re.match(r'xor\s+ecx,ecx$', t): return 0
    return None

pcs = collections.defaultdict(list)
cur = None
for i, (a, t) in enumerate(ins):
    if a in sidx: cur = a
    m = re.match(r'call\s+0x([0-9a-f]+)$', t)
    if m and cur is not None:
        tg = int(m.group(1), 16)
        if PCW[0] <= tg < PCW[1]: pcs[cur].append((tg, pc_arg0(i)))

im = Img(); idx = A.load()
xs = collections.defaultdict(list)
fst = sorted(idx)
for i, w in enumerate(im.W):
    if (w >> 26) == 18 and (w & 1):
        li = w & 0x03FFFFFC
        if li & 0x02000000: li -= 0x04000000
        a = TEXT_LO + i * 4; tg = a + li
        if XW[0] <= tg < XW[1]:
            f = fst[bisect.bisect_right(fst, a) - 1]
            if XW[0] <= f < XW[1]: continue          # skip calls from inside the wrapper layer itself
            r = C.args_at(im, i).get(3)
            xs[f].append((tg, r))

def sig(lst): return tuple(c for _, c in lst)
def good(lst):
    s = sig(lst); return len(s) >= 4 and sum(1 for c in s if c is not None) >= 3
pcsig = collections.defaultdict(list); xsig = collections.defaultdict(list)
for f, l in pcs.items():
    if good(l): pcsig[sig(l)].append(f)
for f, l in xs.items():
    if good(l): xsig[sig(l)].append(f)
votes = collections.defaultdict(collections.Counter)
nm = 0
for s, pf in pcsig.items():
    xf = xsig.get(s)
    if xf and len(pf) == 1 and len(xf) == 1:
        nm += 1
        for (pw, _), (xw, _) in zip(pcs[pf[0]], xs[xf[0]]):
            votes[pw][xw] += 1
print('unique:', nm, 'pc good', sum(len(v) for v in pcsig.values()), 'x good', sum(len(v) for v in xsig.values()), 'common sigs', len(set(pcsig) & set(xsig)), file=sys.stderr)
res = []
for pw, c in votes.items():
    xw, n = c.most_common(1)[0]
    res.append(dict(pc='%x' % pw, x360='%x' % xw, votes=n, total=sum(c.values()), alt=[('%x' % k, v) for k, v in c.most_common(3)[1:]]))
res.sort(key=lambda r: -r['votes'])
json.dump(res, open(sys.argv[2], 'w'), indent=1)
for r in res[:50]: print(r)
