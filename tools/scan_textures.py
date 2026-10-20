#!/usr/bin/env python3
"""Index every texture (WTB) in data000/data001: archive, entry, DAT member, fetch-constant properties.
usage: scan_textures.py OUT.json"""
import json, sys, multiprocessing as mp
sys.path.insert(0, '/home/lucidfear/Projects/mgr-nx/tools')
import cpk, pgdat, wtb
ROOT = '/home/lucidfear/Projects/mgr-nx/x360_extracted/media/'
ARCH = ['data000.cpk', 'data001.cpk']
_c = {}
def get(a):
    if a not in _c: _c[a] = cpk.CPK(ROOT + a)
    return _c[a]
def work(job):
    a, i = job
    c = get(a); e = c.files[i]
    full = (e['dir'] + '/' if e['dir'] else '') + e['name']
    res = []
    try:
        d = c.read(e)
        if e['name'].endswith(('.wtb', '.wta')):
            items = [(full, d)]
        elif d[:4] == b'DAT\0':
            items = [(full + '::' + m['name'], d[m['off']:m['off'] + m['size']]) for m in pgdat.parse(d) if m['ext'] in ('wtb', 'wta')]
        else:
            items = []
        for name, blob in items:
            for k, t in enumerate(wtb.parse(blob)):
                t['src'] = a + ':' + name; t['idx'] = k; t['kind'] = 'wta' if name.endswith('.wta') else 'wtb'; res.append(t)
    except Exception as ex:
        res.append(dict(src=a + ':' + full, error=str(ex)[:80]))
    return res
if __name__ == '__main__':
    jobs = []
    for a in ARCH:
        c = get(a)
        for i, e in enumerate(c.files):
            if e['name'].endswith(('.dat', '.wtb', '.wta')): jobs.append((a, i))
    print('jobs', len(jobs), file=sys.stderr)
    out = []
    with mp.Pool(8) as p:
        for n, r in enumerate(p.imap_unordered(work, jobs, chunksize=4)):
            out += r
            if n % 200 == 0: print(n, len(out), file=sys.stderr, flush=True)
    json.dump(out, open(sys.argv[1], 'w'))
    print('textures', len(out), file=sys.stderr)
