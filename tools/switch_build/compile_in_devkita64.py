#!/usr/bin/env python3
# usage: cc.py OPT FILE...   (inside devkita64 container): compile, report wall time, peak RSS, object size
import os, resource, subprocess, sys, time
opt, files = sys.argv[1], sys.argv[2:]
for f in files:
    b = os.path.basename(f)[:-4]
    out = '/obj/%s.%s.o' % (b, opt.strip('-'))
    cmd = ['aarch64-none-elf-g++', '-std=gnu++20', opt, '-mtune=cortex-a57', '-march=armv8-a+crc+crypto', '-fPIE',
           '-D__SWITCH__', '-include', '/compat.h', '-w', '-I/ppc', '-I/simde', '-I/opt/devkitpro/libnx/include', '-c', f, '-o', out]
    t = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    dt = time.time() - t
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss // 1024
    print('%s rc=%d %.1fs peakRSS=%dMB obj=%dKB' % (b, p.returncode, dt, rss, os.path.getsize(out) // 1024 if os.path.exists(out) else -1), flush=True)
    if p.returncode:
        print(p.stderr[:1500])
