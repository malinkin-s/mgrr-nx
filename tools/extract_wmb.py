#!/usr/bin/env python3
"""Extract every .wmb (and its sibling .wta/.wtp/.mot names are ignored) from the .dat archives into x360_work/wmb/<archive>__<member>.wmb"""
import os, sys, multiprocessing as mp
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import cpk, pgdat
ROOT = '/home/lucidfear/Projects/mgr-nx/x360_extracted/media/'
OUT = '/home/lucidfear/Projects/mgr-nx/x360_work/wmb/'
_c = {}
def work(job):
    a, i = job
    c = _c.setdefault(a, cpk.CPK(ROOT + a)); e = c.files[i]
    n = 0
    try:
        d = c.read(e)
        if d[:4] != b'DAT\0': return 0
        base = os.path.basename(e['name'])[:-4]
        for m in pgdat.parse(d):
            if m['ext'] == 'wmb':
                open(OUT + '%s__%s' % (base, m['name']), 'wb').write(d[m['off']:m['off'] + m['size']]); n += 1
    except Exception as ex:
        print('ERR', a, e['name'], ex, file=sys.stderr)
    return n
if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    jobs = []
    for a in ('data000.cpk', 'data001.cpk'):
        c = cpk.CPK(ROOT + a); jobs += [(a, i) for i, e in enumerate(c.files) if e['name'].endswith('.dat')]
    with mp.Pool(8) as p: tot = sum(p.imap_unordered(work, jobs, chunksize=4))
    print('extracted', tot, file=sys.stderr)
