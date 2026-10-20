#!/usr/bin/env python3
"""Cap functions that XenonRecomp's own analysis made overlap an explicit function.

usage: cap_overlapping_functions.py PPC_DIR RECOMP_LOG FUNCTION_BOUNDS.toml OUT.toml
Finds `ERROR: 0x... ` blocks in the generated C++, takes the enclosing sub_XXXX that is
not the explicit function itself, and gives it an explicit size ending at the explicit
function's start.
"""
import glob
import re
import sys

ppc, log, fb, out = sys.argv[1:5]
fns = [(int(a, 16), int(b, 16)) for a, b in re.findall(r'address = (0x[0-9A-F]+), size = (0x[0-9A-F]+)', open(fb).read())]
sites = {int(a, 16) for a in re.findall(r'Switch case at ([0-9A-F]+) is trying', open(log).read())}
caps = {}
for path in glob.glob(ppc + '/ppc_recomp.*.cpp'):
    name = None
    for line in open(path, errors='replace'):
        m = re.match(r'PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]+)\)', line)
        if m:
            name = int(m.group(1), 16)
        elif '// ERROR: 0x' in line and name is not None:
            caps.setdefault(name, 0)
res = []
for base in sorted(caps):
    # explicit function that starts after `base` and covers a failing site
    cand = [s for s, z in fns if s > base and any(s <= x < s + z for x in sites)]
    nxt = min((s for s in cand), default=None)
    if nxt is None or nxt - base > 0x4000:
        print('no cap for sub_%X' % base)
        continue
    res.append((base, nxt - base))
with open(out, 'w') as f:
    for b, z in res:
        f.write('    { address = 0x%X, size = 0x%X },\n' % (b, z))
print('caps:', len(res))
