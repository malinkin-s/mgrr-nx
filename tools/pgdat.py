#!/usr/bin/env python3
"""PlatinumGames DAT archive reader (big-endian, Xbox 360).
usage: pgdat.py list FILE.dat | x FILE.dat OUTDIR [regex]"""
import os, re, struct, sys
def parse(d, be=True):
    e = '>' if be else '<'
    assert d[:4] == b'DAT\0'
    n, off_o, off_e, off_n, off_s, off_h = struct.unpack_from(e + '6I', d, 4)
    offs = struct.unpack_from(e + '%dI' % n, d, off_o)
    exts = [d[off_e + 4 * i: off_e + 4 * i + 4].rstrip(b'\0').decode('latin-1') for i in range(n)]
    nl = struct.unpack_from(e + 'I', d, off_n)[0]
    names = [d[off_n + 4 + nl * i: off_n + 4 + nl * (i + 1)].split(b'\0')[0].decode('latin-1', 'replace') for i in range(n)]
    sizes = struct.unpack_from(e + '%dI' % n, d, off_s)
    return [dict(name=names[i], ext=exts[i], off=offs[i], size=sizes[i]) for i in range(n)]
if __name__ == '__main__':
    d = open(sys.argv[2], 'rb').read(); ents = parse(d)
    if sys.argv[1] == 'list':
        for e in ents: print('%s %s %d 0x%x' % (e['name'], e['ext'], e['size'], e['off']))
    else:
        rx = re.compile(sys.argv[4]) if len(sys.argv) > 4 else None; n = 0
        for e in ents:
            if rx and not rx.search(e['name']): continue
            p = os.path.join(sys.argv[3], e['name']); os.makedirs(os.path.dirname(p) or '.', exist_ok=True)
            open(p, 'wb').write(d[e['off']:e['off'] + e['size']]); n += 1
        print('extracted', n, file=sys.stderr)
