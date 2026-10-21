"""Reads the `nxlink -s` output (stdin) of a run: text goes to the terminal and RUN_DIR/log.txt, screenshot parts
from game/src/mgrr_nxlink.cpp ("@@MGRR_SHOT <name> <part> <parts> <crc32> <base64>") become RUN_DIR/<name>.

    nxlink -s ... | python3 -I tools/nxlink_recv.py RUN_DIR
"""

import base64
import os
import signal
import sys
import zlib


class Shots:
    def __init__(self, run_dir, say):
        self.run_dir = run_dir
        self.say = say
        self.name = None
        self.parts = []
        self.bad = []

    def part(self, fields):
        try:
            name = os.path.basename(fields[1].decode("ascii"))
            index, count, crc = int(fields[2]), int(fields[3]), int(fields[4], 16)
            data = fields[5] if len(fields) > 5 else b""
        except (IndexError, ValueError, UnicodeDecodeError):
            self.say(b"[nxlink_recv] unreadable screenshot line\n")
            return
        if name != self.name:
            self.finish()
            self.name, self.parts, self.bad = name, [None] * count, []
        if index >= len(self.parts):
            self.bad.append(index)
        elif zlib.crc32(data) != crc:
            self.bad.append(index)
        else:
            self.parts[index] = data
        if index == len(self.parts) - 1:
            self.finish()

    def finish(self):
        if self.name is None:
            return
        missing = [i for i, p in enumerate(self.parts) if p is None]
        path = os.path.join(self.run_dir, self.name)
        if missing:
            self.say(b"[nxlink_recv] screenshot %s: %d of %d parts missing or damaged (%s)\n"
                     % (self.name.encode(), len(missing), len(self.parts), str(missing[:10]).encode()))
        else:
            jpeg = base64.b64decode(b"".join(self.parts))
            with open(path, "wb") as f:
                f.write(jpeg)
            self.say(b"[nxlink_recv] saved %s (%d KB)\n" % (path.encode(), len(jpeg) // 1024))
        self.name = None


def main():
    run_dir = sys.argv[1]
    os.makedirs(run_dir, exist_ok=True)
    out = sys.stdout.buffer
    with open(os.path.join(run_dir, "log.txt"), "ab") as log:
        def say(line):
            out.write(line)
            log.write(line)
            out.flush()
            log.flush()

        shots = Shots(run_dir, say)
        for line in sys.stdin.buffer:
            if line.startswith(b"@@MGRR_SHOT "):
                shots.part(line.split())
            elif line.strip():
                say(line)
        shots.finish()


if __name__ == "__main__":
    # Ctrl+C stops nxlink; this keeps reading until its output ends, so nothing it already received is lost.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    main()
