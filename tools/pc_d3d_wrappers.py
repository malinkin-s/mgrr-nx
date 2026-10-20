#!/usr/bin/env python3
"""Find small PC-side engine wrappers around IDirect3DDevice9 methods in MGRR.exe (from objdump -M intel output).
usage: pc_d3d_wrappers.py pc.asm OUT.json"""
import collections, json, re, sys

asm = sys.argv[1]
ins = []          # (addr, text)
rx = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')
with open(asm, errors='replace') as f:
    for line in f:
        m = rx.match(line)
        if m:
            ins.append((int(m.group(1), 16), m.group(2).strip()))
print('instructions', len(ins), file=sys.stderr)

# function starts: direct call targets + instruction after (ret/int3 run)
starts = set()
for a, t in ins:
    m = re.match(r'call\s+0x([0-9a-f]+)$', t)
    if m:
        starts.add(int(m.group(1), 16))
prev = None
for a, t in ins:
    if prev is not None and prev[1] in ('int3', 'ret') or (prev is not None and prev[1].startswith('ret ')):
        if t not in ('int3', 'nop') and not t.startswith('xchg') and not t.startswith('lea    esi,[esi'):
            starts.add(a)
    prev = (a, t)
starts = sorted(starts)
import bisect
addr_index = {a: i for i, (a, _) in enumerate(ins)}

# callers count
callers = collections.Counter()
for a, t in ins:
    m = re.match(r'call\s+0x([0-9a-f]+)$', t)
    if m:
        callers[int(m.group(1), 16)] += 1
    else:
        m = re.match(r'jmp\s+0x([0-9a-f]+)$', t)

out = []
vt = re.compile(r'call\s+DWORD PTR \[(e[a-z]{2})\+0x([0-9a-f]+)\]')
for k, s in enumerate(starts):
    e = starts[k + 1] if k + 1 < len(starts) else s + 0x1000
    i0 = addr_index.get(s)
    if i0 is None:
        continue
    body = []
    j = i0
    while j < len(ins) and ins[j][0] < e and len(body) < 80:
        body.append(ins[j]); j += 1
    if len(body) > 60:
        continue
    calls = [(a, t) for a, t in body if t.startswith('call')]
    vcalls = [(a, vt.search(t)) for a, t in body if vt.search(t)]
    if len(vcalls) != 1:
        continue
    off = int(vcalls[0][1].group(2), 16)
    if off > 0x1d8 or off % 4:
        continue
    if len(calls) > 2:
        continue
    # constants pushed before the call (state ids etc.)
    pushes = []
    for a, t in body:
        if a == vcalls[0][0]:
            break
        m = re.match(r'push\s+(0x[0-9a-f]+|\d+)$', t)
        if m:
            pushes.append(int(m.group(1), 0))
    out.append(dict(addr=s, n=len(body), callers=callers.get(s, 0), vt=off, pushes=pushes,
                    ret=next((t for a, t in reversed(body) if t.startswith('ret')), ''),
                    body=[t for a, t in body]))
json.dump(out, open(sys.argv[2], 'w'))
print('wrappers', len(out), file=sys.stderr)
