#!/usr/bin/env python3
"""Helpers for poking at the decrypted MGR:R Xbox 360 image (x360_work/mgrr_image.bin).

    from x360img import Img
    im = Img()
    im.strings(b'D3D')              # [(addr, text)]
    im.xrefs(addr)                  # code addresses that materialise `addr` (lis+addi/ori, or lis+load/store disp)
    im.func_of(addr)                # (start, size) from .pdata, or None
"""
import bisect
import re
import struct

BASE = 0x82000000
TEXT_LO, TEXT_SZ = 0x82270000, 0xF8D75C
TEXT_HI = TEXT_LO + TEXT_SZ
PDATA, PDATA_N = 0x821F3A00, 0x6F608 // 8


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


class Img:
    def __init__(self, path='/home/lucidfear/Projects/mgr-nx/x360_work/mgrr_image.bin'):
        self.d = open(path, 'rb').read()
        n = TEXT_SZ // 4
        self.W = struct.unpack('>%dI' % n, self.d[TEXT_LO - BASE:TEXT_LO - BASE + n * 4])
        fn = []
        for i in range(PDATA_N):
            a, f = struct.unpack_from('>II', self.d, PDATA - BASE + i * 8)
            fn.append((a, ((f >> 8) & 0x3FFFFF) * 4))
        fn.sort()
        self.fns = fn
        self._fs = [a for a, _ in fn]
        self._xr = None

    # --- raw access ---
    def u32(self, a):
        return struct.unpack_from('>I', self.d, a - BASE)[0]

    def insn(self, a):
        return self.W[(a - TEXT_LO) >> 2]

    def cstr(self, a, maxlen=200):
        o = a - BASE
        e = self.d.find(b'\0', o, o + maxlen)
        return self.d[o:e if e >= 0 else o + maxlen].decode('latin-1')

    def func_of(self, a):
        k = bisect.bisect_right(self._fs, a) - 1
        if k >= 0 and self.fns[k][0] <= a < self.fns[k][0] + self.fns[k][1]:
            return self.fns[k]
        return None

    # --- strings ---
    def strings(self, pat, lo=0x82000400, hi=0x821F3A00, minlen=6):
        pat = re.compile(pat if isinstance(pat, bytes) else pat.encode())
        out = []
        blob = self.d[lo - BASE:hi - BASE]
        for m in re.finditer(rb'[\x20-\x7e\t\r\n]{%d,}' % minlen, blob):
            if pat.search(m.group()):
                out.append((lo + m.start(), m.group().decode('latin-1')))
        return out

    # --- xrefs ---
    def _build_xrefs(self):
        """map target address -> [code addr]. Tracks lis rX,hi  then addi/ori rY,rX,lo and lis+(l/st)* disp(rX)."""
        xr = {}
        lis = {}                                   # reg -> (hi<<16, code addr of lis)
        for i, w in enumerate(self.W):
            a = TEXT_LO + i * 4
            op = w >> 26
            rd, ra = (w >> 21) & 31, (w >> 16) & 31
            if op == 15 and ra == 0:               # lis
                lis[rd] = ((w & 0xFFFF) << 16, a)
                continue
            if ra in lis and op in (14, 24):       # addi / ori
                hi, la = lis[ra]
                if a - la <= 64:
                    lo = s16(w & 0xFFFF) if op == 14 else (w & 0xFFFF)
                    xr.setdefault((hi + lo) & 0xFFFFFFFF, []).append(a)
            elif ra in lis and (op in range(32, 56)):   # load/store with displacement
                hi, la = lis[ra]
                if a - la <= 64:
                    xr.setdefault((hi + s16(w & 0xFFFF)) & 0xFFFFFFFF, []).append(a)
            if op in (14, 24, 15) and rd in lis and not (op == 15 and ra == 0):
                # rd overwritten by something other than lis: forget it (lis can be re-used via ra==rd addi above)
                if not (op in (14, 24) and ra == rd):
                    lis.pop(rd, None)
        self._xr = xr

    def xrefs(self, addr):
        if self._xr is None:
            self._build_xrefs()
        return self._xr.get(addr, [])

    # --- calls ---
    def callers_of(self, target):
        """addresses of `bl target`"""
        out = []
        for i, w in enumerate(self.W):
            if (w >> 26) == 18 and (w & 3) == 1:
                li = w & 0x03FFFFFC
                if li & 0x02000000:
                    li -= 0x04000000
                a = TEXT_LO + i * 4
                if a + li == target:
                    out.append(a)
        return out
