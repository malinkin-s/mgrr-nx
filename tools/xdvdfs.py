#!/usr/bin/env python3
"""Read-only XDVDFS (Xbox 360 game disc) lister/extractor.

  xdvdfs.py IMAGE [OUTDIR] [--skip NAME ...]   (no OUTDIR = list only)

The image is untrusted input: entry names with path separators or '.'/'..' are refused.
"""
import os, struct, sys

SEC = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
BASES = (0, 0x2080000, 0x0FD90000, 0x18300000)   # plain, XGD3, XGD2, XGD1 volume offsets


def safe_name(name):
    if not name or name in ('.', '..') or '/' in name or '\\' in name or '\0' in name:
        sys.exit('refusing entry name %r' % name)
    return name


def main():
    args = sys.argv[1:]
    skip = set()
    if '--skip' in args:
        i = args.index('--skip')
        skip = set(args[i + 1:])
        args = args[:i]
    img = args[0]
    out = args[1] if len(args) > 1 else None
    f = open(img, 'rb')
    base = None
    for b in BASES:
        f.seek(b + 32 * SEC)
        if f.read(20) == MAGIC:
            base = b
            break
    if base is None:
        sys.exit('no XDVDFS volume found')
    f.seek(base + 32 * SEC + 0x14)
    root, rsize = struct.unpack('<II', f.read(8))
    print('layout base', hex(base), 'root sector', root, 'size', rsize, file=sys.stderr)

    def entries(sector, size):
        f.seek(base + sector * SEC)
        buf = f.read(size)
        seen, res = set(), []

        def node(off):                     # directory = binary tree of entries (offsets in dwords)
            if off in seen or off + 14 > len(buf):
                return
            seen.add(off)
            l, r, sec, sz, attr, nl = struct.unpack_from('<HHIIBB', buf, off)
            if l == 0xFFFF and r == 0xFFFF:
                return
            name = buf[off + 14:off + 14 + nl].decode('latin-1')
            if l and l != 0xFFFF:
                node(l * 4)
            res.append((name, attr, sec, sz))
            if r and r != 0xFFFF:
                node(r * 4)

        node(0)
        return res

    def rec(sector, size, path, depth):
        if depth > 32:
            sys.exit('directory tree too deep')
        for name, attr, sec, sz in entries(sector, size):
            name = safe_name(name)
            if not path and name in skip:
                continue
            p = os.path.join(path, name)
            if attr & 0x10:
                if out:
                    os.makedirs(os.path.join(out, p), exist_ok=True)
                print(p + '/')
                if sz:
                    rec(sec, sz, p, depth + 1)
            else:
                print(p, sz)
                if out:
                    dst = os.path.join(out, p)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    with open(dst, 'wb') as o:
                        f.seek(base + sec * SEC)
                        left = sz
                        while left:
                            d = f.read(min(left, 1 << 22))
                            if not d:
                                sys.exit('image truncated at ' + p)
                            o.write(d)
                            left -= len(d)

    rec(root, rsize, '', 0)


if __name__ == '__main__':
    main()
