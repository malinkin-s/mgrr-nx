#!/usr/bin/env python3
"""Summary of a GPU recording (sdk/include/rex/graphics/gpu_trace.h): frames, packets, memory reads, PM4 opcodes
of the primary ring and of the indirect buffers it calls.

usage: python3 -I tools/gputrace.py FILE.gtr
"""
import collections
import struct
import sys

PM4 = {0x10: 'NOP', 0x3F: 'INDIRECT_BUFFER', 0x3C: 'WAIT_REG_MEM', 0x22: 'DRAW_INDX', 0x36: 'DRAW_INDX_2',
       0x27: 'IM_LOAD', 0x2B: 'IM_LOAD_IMMEDIATE', 0x2D: 'SET_CONSTANT', 0x2F: 'LOAD_ALU_CONSTANT',
       0x3D: 'MEM_WRITE', 0x46: 'EVENT_WRITE', 0x58: 'EVENT_WRITE_SHD', 0x64: 'XE_SWAP', 0x54: 'INTERRUPT',
       0x3E: 'REG_TO_MEM', 0x21: 'REG_RMW', 0x45: 'COND_WRITE', 0x48: 'ME_INIT', 0x5B: 'EVENT_WRITE_EXT',
       0x26: 'INVALIDATE_STATE', 0x4F: 'EVENT_WRITE_ZPD', 0x2E: 'SET_SHADER_CONSTANTS', 0x34: 'LOAD_CONSTANT_CONTEXT',
       0x1A: 'SET_BIN_MASK_LO', 0x1B: 'SET_BIN_MASK_HI', 0x1C: 'SET_BIN_SELECT_LO', 0x1D: 'SET_BIN_SELECT_HI',
       0x60: 'VIZ_QUERY'}


def walk(words, ops, memory, depth=0):
    """Count the opcodes of a packet stream; follow INDIRECT_BUFFERs through the recorded memory."""
    i = 0
    while i < len(words):
        p = words[i]
        t = p >> 30
        if p == 0:
            i += 1
        elif t == 0:
            ops['type0 (registers)'] += 1
            i += 1 + ((p >> 16) & 0x3FFF) + 1
        elif t == 1:
            ops['type1'] += 1
            i += 3
        elif t == 2:
            i += 1
        else:
            op = (p >> 8) & 0x7F
            n = ((p >> 16) & 0x3FFF) + 1
            ops[PM4.get(op, 'op%02X' % op)] += 1
            if op == 0x3F and depth < 4 and i + 2 < len(words):
                ptr, length = words[i + 1] & 0x1FFFFFFF, words[i + 2] & 0xFFFFF
                data = memory.get(ptr)
                if data is not None and len(data) >= length * 4:
                    walk(list(struct.unpack('>%dI' % length, data[:length * 4])), ops, memory, depth + 1)
                else:
                    ops['INDIRECT_BUFFER not recorded'] += 1
            i += 1 + n


def main():
    f = open(sys.argv[1], 'rb')
    if f.read(8) != b'GPUTRACE':
        sys.exit('not a GPU recording')
    version, = struct.unpack('<I', f.read(4))
    blobs = {}
    memory = {}   # address -> bytes of the last read there (enough to follow indirect buffers)
    ops = collections.Counter()
    reads = collections.Counter()
    frames = []
    packets = 0
    blob_bytes = 0
    while True:
        tag = f.read(1)
        if not tag:
            print('(no end record: truncated)')
            break
        tag = tag.decode()
        if tag == 'R':
            count, = struct.unpack('<I', f.read(4))
            f.read(count * 4)
        elif tag == 'B':
            h, size = struct.unpack('<QI', f.read(12))
            blobs[h] = f.read(size)
            blob_bytes += size
        elif tag == 'M':
            address, size, h = struct.unpack('<IIQ', f.read(16))
            memory[address] = blobs[h]
            reads['reads'] += 1
            reads['bytes read'] += size
        elif tag == 'P':
            count, = struct.unpack('<I', f.read(4))
            words = list(struct.unpack('>%dI' % count, f.read(count * 4)))
            walk(words, ops, memory)
            packets += 1
        elif tag == 'S':
            frames.append(struct.unpack('<I', f.read(4))[0])
        elif tag == 'E':
            break
        else:
            sys.exit('unknown record %r at %d' % (tag, f.tell() - 1))
    print('version %d: %d frames, %d primary packets, %d distinct blobs (%.1f MB), %d reads (%.1f MB as read)'
          % (version, len(frames), packets, len(blobs), blob_bytes / 2**20, reads['reads'], reads['bytes read'] / 2**20))
    for name, n in ops.most_common():
        print('  %-28s %d' % (name, n))


if __name__ == '__main__':
    main()
