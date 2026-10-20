#!/usr/bin/env python3
"""Propagate function matches PC <-> 360 over the call graph.
usage: propagate.py pc.asm func_pairs.json OUT.json
Seeds: string-anchored pairs. Rule: for a matched pair (a,b) whose ordered direct-callee lists have equal length,
vote callee_i(a) <-> callee_i(b). Also votes from callers: if callers lists align at a matched call position.
"""
import bisect, collections, glob, json, pickle, re, sys, os
CACHE = '/tmp/claude-1000/-home-lucidfear-Projects-mgr-nx/828c1bb3-4343-4664-b577-9048fd98b786/scratchpad/calls360.pkl'
ROOT = '/home/lucidfear/Projects/mgr-nx/x360_work'

def calls360():
    if os.path.exists(CACHE): return pickle.load(open(CACHE, 'rb'))
    out = {}
    for p in glob.glob(ROOT + '/ppc/ppc_recomp.*.cpp'):
        cur = None
        for line in open(p, errors='replace'):
            m = re.match(r'PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]+)\)', line)
            if m:
                cur = int(m.group(1), 16); out[cur] = []; continue
            if cur is not None:
                m = re.match(r'\s*// bl 0x([0-9a-f]+)', line)
                if m: out[cur].append(int(m.group(1), 16))
    pickle.dump(out, open(CACHE, 'wb'))
    return out

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
pc_calls = collections.defaultdict(list)
fi = None
sidx = {s: i for i, s in enumerate(starts)}
cur = None
for a, t in ins:
    if a in sidx: cur = a
    m = re.match(r'call\s+0x([0-9a-f]+)$', t)
    if m and cur is not None: pc_calls[cur].append(int(m.group(1), 16))
x_calls = calls360()
print('pc funcs', len(pc_calls), '360 funcs', len(x_calls), file=sys.stderr)

seed = json.load(open(sys.argv[2]))
match = {}          # pc -> x
rev = {}
for p, x, n, tot in seed:
    if p not in match and x not in rev and (n >= 2 or tot == 1):
        match[p] = x; rev[x] = p
print('seeds', len(match), file=sys.stderr)

for rnd in range(8):
    votes = collections.defaultdict(collections.Counter)
    for p, x in list(match.items()):
        a, b = pc_calls.get(p, []), x_calls.get(x, [])
        if a and len(a) == len(b):
            for ca, cb in zip(a, b):
                votes[ca][cb] += 1
    new = 0
    for p, c in votes.items():
        if p in match: continue
        x, n = c.most_common(1)[0]
        if x in rev: continue
        if len(c) == 1 or n >= 2 * (c.most_common(2)[1][1] if len(c) > 1 else 0):
            match[p] = x; rev[x] = p; new += 1
    print('round', rnd, 'new', new, 'total', len(match), file=sys.stderr)
    if not new: break
json.dump(sorted(match.items()), open(sys.argv[3], 'w'))
