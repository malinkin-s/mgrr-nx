#!/usr/bin/env python3
"""Minimal CRI CPK reader (list / extract, with CRILAYLA decompression).

usage:
  cpk.py list  FILE.cpk                 # dir/name size extract_size offset
  cpk.py x     FILE.cpk OUTDIR [regex]  # extract (optionally only names matching regex)
"""
import os
import re
import struct
import sys


class UTF:
    """@UTF table (big-endian)."""
    TYPES = {0: ('>B', 1), 1: ('>b', 1), 2: ('>H', 2), 3: ('>h', 2), 4: ('>I', 4), 5: ('>i', 4), 6: ('>Q', 8), 7: ('>q', 8), 8: ('>f', 4)}

    def __init__(self, data, off=0):
        assert data[off:off + 4] == b'@UTF', 'not an @UTF table'
        base = off + 8
        (self.size, rows_off, str_off, data_off, name_off, ncols, row_w, nrows) = struct.unpack_from('>IIIIIHHI', data, off + 4)
        self.data, self.base = data, base
        self.rows_off, self.str_off, self.data_off = base + rows_off, base + str_off, base + data_off
        self.name = self._str(name_off)
        p = off + 32
        cols = []
        for _ in range(ncols):
            flag = data[p]; p += 1
            nm = self._str(struct.unpack_from('>I', data, p)[0]); p += 4
            typ, store = flag & 0x0F, flag & 0xF0
            const = None
            if store == 0x30:                       # constant value stored in the column header
                const, p = self._val(typ, p)
            cols.append((nm, typ, store, const))
        self.cols = cols
        self.rows = []
        for r in range(nrows):
            q = self.rows_off + r * row_w
            row = {}
            for nm, typ, store, const in cols:
                if store == 0x50:
                    row[nm], q = self._val(typ, q)
                elif store == 0x30:
                    row[nm] = const
                else:
                    row[nm] = None
            self.rows.append(row)

    def _str(self, o):
        s = self.str_off + o
        e = self.data.index(b'\0', s)
        return self.data[s:e].decode('utf-8', 'replace')

    def _val(self, typ, p):
        if typ in self.TYPES:
            f, n = self.TYPES[typ]
            return struct.unpack_from(f, self.data, p)[0], p + n
        if typ == 0x0A:
            return self._str(struct.unpack_from('>I', self.data, p)[0]), p + 4
        if typ == 0x0B:
            o, n = struct.unpack_from('>II', self.data, p)
            return (self.data_off + o, n), p + 8
        raise ValueError('column type %x' % typ)


def crilayla(buf):
    """Decompress a CRILAYLA blob."""
    assert buf[:8] in (b'CRILAYLA', b'\0' * 8)      # MGR:R blanks the magic
    ulen, hlen = struct.unpack_from('<II', buf, 8)
    out = bytearray(ulen + 0x100)
    out[0:0x100] = buf[len(buf) - 0x100:]            # uncompressed 0x100-byte prefix kept at end of the stream
    src = 16 + hlen - 1 + 0                           # read backwards starting at end of compressed data
    src = 16 + hlen - 1
    outp = ulen + 0x100 - 1
    bitpool, bits = 0, 0

    def getbits(n):
        nonlocal src, bitpool, bits
        v = 0
        left = n
        while left:
            if bits == 0:
                bitpool = buf[src]; src -= 1; bits = 8
            take = min(bits, left)
            v = (v << take) | ((bitpool >> (bits - take)) & ((1 << take) - 1))
            bits -= take; left -= take
        return v
    lens = (2, 3, 5, 8)
    while outp >= 0x100:
        if getbits(1):
            off = getbits(13) + 3
            ln = 3
            for k, w in enumerate(lens):
                t = getbits(w)
                ln += t
                if t != (1 << w) - 1: break
            else:
                while True:
                    t = getbits(8); ln += t
                    if t != 255: break
            for _ in range(ln):
                out[outp] = out[outp + off]; outp -= 1
        else:
            out[outp] = getbits(8); outp -= 1
    return bytes(out[:ulen + 0x100])


class CPK:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        hdr = self.f.read(0x10 + 0x400)
        assert hdr[:4] == b'CPK '
        sz = struct.unpack_from('<Q', hdr, 8)[0]
        self.f.seek(0x10)
        t = UTF(self.f.read(sz))
        self.header = t.rows[0]
        toc_off = self.header['TocOffset']
        content = self.header.get('ContentOffset') or 0
        self.f.seek(toc_off)
        assert self.f.read(4) == b'TOC '
        self.f.seek(toc_off + 8)
        tsz = struct.unpack('<Q', self.f.read(8))[0]
        self.f.seek(toc_off + 16)
        toc = UTF(self.f.read(tsz))
        base = min(toc_off, content) if content else toc_off
        self.files = []
        for r in toc.rows:
            self.files.append(dict(dir=r.get('DirName') or '', name=r['FileName'], size=r['FileSize'], xsize=r['ExtractSize'],
                                   off=base + r['FileOffset'], id=r.get('ID')))

    def read(self, ent):
        self.f.seek(ent['off'])
        d = self.f.read(ent['size'])
        if ent['xsize'] != ent['size'] and d[:8] in (b'CRILAYLA', b'\0' * 8):
            ul = struct.unpack_from('<I', d, 8)[0]
            if ul + 0x100 == ent['xsize']:
                d = crilayla(d)
        return d


if __name__ == '__main__':
    cmd, path = sys.argv[1], sys.argv[2]
    c = CPK(path)
    if cmd == 'list':
        for e in c.files:
            print('%s/%s %d %d 0x%x' % (e['dir'], e['name'], e['size'], e['xsize'], e['off']))
    elif cmd == 'x':
        out = sys.argv[3]
        rx = re.compile(sys.argv[4]) if len(sys.argv) > 4 else None
        n = 0
        for e in c.files:
            full = (e['dir'] + '/' if e['dir'] else '') + e['name']
            if rx and not rx.search(full): continue
            p = os.path.join(out, full)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(c.read(e))
            n += 1
        print('extracted', n, file=sys.stderr)
