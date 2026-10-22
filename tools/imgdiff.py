#!/usr/bin/env python3
"""Compares a replayed frame with its reference (graphics test bench, PLAN.md T4). No image library needed.

usage: python3 -I tools/imgdiff.py REFERENCE.ppm REPLAY.png OUT.png
Prints one line: mean absolute difference (0-255), the share of pixels off by more than 32 in some channel, and
the share of reference pixels that are not black but black in the replay (something not drawn). OUT.png is
reference | replay | difference (amplified), side by side.
"""
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, 'rb').read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            pos = data.index(b'\n', pos)
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b'P6' or fields[3] != b'255':
        sys.exit('%s: not an 8-bit binary PPM' % path)
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('%s: not a PNG' % path)
    pos, idat = 8, b''
    while pos < len(data):
        size, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + size]
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            if depth != 8 or color not in (2, 6):
                sys.exit('%s: only 8-bit RGB/RGBA' % path)
        elif kind == b'IDAT':
            idat += body
        pos += 12 + size
    bpp = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xFF
            elif f == 2:
                line[x] = (line[x] + b) & 0xFF
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if bpp == 4:
        out = bytearray(b for i in range(0, len(out), 4) for b in out[i:i + 3])
    return w, h, bytes(out)


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    rw, rh, ref = read_ppm(sys.argv[1])
    pw, ph, rep = read_png(sys.argv[2])
    w, h = min(rw, pw), min(rh, ph)
    total = 0
    off = 0
    lit = 0
    missing = 0
    diff = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            i = (y * rw + x) * 3
            j = (y * pw + x) * 3
            r, p = ref[i:i + 3], rep[j:j + 3]
            d = [abs(r[k] - p[k]) for k in range(3)]
            total += sum(d)
            if max(d) > 32:
                off += 1
            if max(r) > 24:
                lit += 1
                if max(p) <= 8:
                    missing += 1
            o = (y * w + x) * 3
            diff[o:o + 3] = bytes(min(255, v * 4) for v in d)
    pixels = w * h
    size_note = '' if (rw, rh) == (pw, ph) else ' (sizes differ: reference %dx%d, replay %dx%d)' % (rw, rh, pw, ph)
    print('mean diff %.1f, %.1f%% of pixels off by >32, %.1f%% of the lit reference missing%s' %
          (total / (pixels * 3), 100.0 * off / pixels, 100.0 * missing / max(1, lit), size_note))
    side = bytearray()
    for y in range(h):
        side += ref[y * rw * 3:(y * rw + w) * 3] + rep[y * pw * 3:(y * pw + w) * 3] + diff[y * w * 3:(y + 1) * w * 3]
    write_png(sys.argv[3], w * 3, h, bytes(side))


if __name__ == '__main__':
    main()
