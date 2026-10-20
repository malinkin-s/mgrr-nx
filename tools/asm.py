#!/usr/bin/env python3
"""Print PPC assembly of recompiled functions, taken from the comments XenonRecomp leaves in the generated C++.
usage: asm.py 826CC010 [more addrs...]   (reads x360_work/ppc, builds an index on first use)"""
import glob, os, pickle, re, sys
ROOT = '/home/lucidfear/Projects/mgr-nx/x360_work'
IDX = '/tmp/claude-1000/-home-lucidfear-Projects-mgr-nx/828c1bb3-4343-4664-b577-9048fd98b786/scratchpad/asm_index.pkl'

def build():
    idx = {}
    for p in sorted(glob.glob(ROOT + '/ppc/ppc_recomp.*.cpp')):
        with open(p, errors='replace') as f:
            pos = 0
            for line in f:
                m = re.match(r'PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]+)\)', line)
                if m:
                    idx[int(m.group(1), 16)] = (p, pos)
                pos += len(line.encode('utf-8', 'replace'))
    pickle.dump(idx, open(IDX, 'wb'))
    return idx

def load():
    if os.path.exists(IDX):
        return pickle.load(open(IDX, 'rb'))
    return build()

def asm(addr, idx=None):
    idx = idx or load()
    if addr not in idx:
        return None
    p, pos = idx[addr]
    out = []
    with open(p, 'rb') as f:
        f.seek(pos)
        f.readline()
        a = addr
        for raw in f:
            l = raw.decode('utf-8', 'replace')
            if l.startswith('}'):
                break
            s = l.strip()
            if s.startswith('loc_'):
                out.append('%s' % s)
            elif s.startswith('// ') and not s.startswith('// ERROR'):
                out.append('  ' + s[3:])
    return out

if __name__ == '__main__':
    idx = load()
    for x in sys.argv[1:]:
        a = int(x, 16)
        print('==== sub_%X' % a)
        r = asm(a, idx)
        print('\n'.join(r) if r else '(not found)')
