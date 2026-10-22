#!/usr/bin/env python3
"""Reference images of a recorded frame's resolves (graphics test bench, PLAN.md T4). No libraries needed.

usage: python3 -I tools/resolve_ref.py RECORDING.gtr FRAME OUT_DIR

The PC build saves what each resolve of the recorded frames wrote when it runs with --readback_resolve=full
(RECORDING.gtr.res/, rex/graphics/gpu_trace.h). Each 8-bit RGBA one becomes OUT_DIR/ref<NNN>.png (color) and
ref<NNN>_alpha.png, NNN being the copy's number in the frame as the replay's step mode counts them (step<NNN>.png).
Other formats (depth, 32-bit float) are listed and skipped.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import imgdiff  # noqa: E402


def tiled_offset(x, y, pitch, log2bpp):
    """Byte offset of texel (x, y) in a 2D tiled surface (XGAddress2DTiledOffset, as tools/xtex.py)."""
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log2bpp
    off = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log2bpp)) + ((y & 1) << 4)
    return (((off & ~511) << 3) + ((y & 16) << 7) + ((off & 448) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) +
            (off & 63))


def swap32(data, endian):
    """The copy's endian mode on 32-bit words: 1 8in16, 2 8in32, 3 16in32."""
    b = bytearray(data[:len(data) & ~3])
    if endian == 2:
        b[0::4], b[1::4], b[2::4], b[3::4] = b[3::4], b[2::4], b[1::4], b[0::4]
    elif endian == 1:
        b[0::2], b[1::2] = b[1::2], b[0::2]
    elif endian == 3:
        b[0::4], b[1::4], b[2::4], b[3::4] = b[2::4], b[3::4], b[0::4], b[1::4]
    return bytes(b)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    folder = sys.argv[1] + '.res'
    frame = int(sys.argv[2])
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    for line in open(os.path.join(folder, 'index.txt')):
        f, copy, address, length, base, pitch, info, control = line.split()
        if int(f) != frame:
            continue
        # The recording counts a copy as its draw starts, before its resolve: the first one is 1.
        step = int(copy) - 1
        pitch, info, control = int(pitch, 16), int(info, 16), int(control, 16)
        width, height = pitch & 0x3FFF, (pitch >> 16) & 0x3FFF
        fmt = (info >> 7) & 0x3F
        depth = (control & 0x7) == 4
        if fmt != 6 or depth:
            print('step %03d: %dx%d format %d%s: skipped' % (step, width, height, fmt, ' (depth)' if depth else ''))
            continue
        data = swap32(open(os.path.join(folder, 'f%d_s%s.bin' % (frame, copy)), 'rb').read(), info & 0x7)
        # k_8_8_8_8 after the swap: R G B A; copy_dest_swap (bit 24) exchanges red and blue.
        red, blue = (2, 0) if (info >> 24) & 1 else (0, 2)
        aligned = (width + 31) & ~31
        rgb = bytearray(width * height * 3)
        alpha = bytearray(width * height * 3)
        for y in range(height):
            for x in range(width):
                o = tiled_offset(x, y, aligned, 2)
                if o + 4 > len(data):
                    continue
                i = (y * width + x) * 3
                rgb[i:i + 3] = bytes((data[o + red], data[o + 1], data[o + blue]))
                alpha[i:i + 3] = bytes((data[o + 3],) * 3)
        imgdiff.write_png(os.path.join(out, 'ref%03d.png' % step), width, height, bytes(rgb))
        imgdiff.write_png(os.path.join(out, 'ref%03d_alpha.png' % step), width, height, bytes(alpha))
        print('step %03d: %dx%d -> ref%03d.png' % (step, width, height, step))


if __name__ == '__main__':
    main()
