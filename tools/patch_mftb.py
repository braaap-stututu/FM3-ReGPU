#!/usr/bin/env python3
"""Route the recompiled code's timebase reads (mftb) through GuestTimebase().

  py -3 -I tools/patch_mftb.py out/ppc

XenonRecomp emits mftb as the host's raw __rdtsc() (GHz, counted since the PC booted). The console's
timebase is 49.875 MHz from boot, and titles do 64-bit tick arithmetic (ticks * 1,000,000) that
overflows with the raw counter. GuestTimebase() (src/kernel/runtime.cpp) returns 49.875 MHz ticks
since the process started; KeQueryPerformanceFrequency reports the same rate.

Idempotent (patched files are skipped); CMake runs it at configure time, so re-run CMake after
regenerating out/ppc. Same approach as the FM2/FM4 ports' patch_mftb.py; own code (FM3 project).
"""
import os
import re
import sys

DECL = "uint64_t GuestTimebase(); // (tools/patch_mftb.py)\n"

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = sys.argv[1]
changed = reads = 0
for name in sorted(os.listdir(root)):
    if not (name.startswith("ppc_recomp.") and name.endswith(".cpp")):
        continue
    path = os.path.join(root, name)
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    n = text.count("__rdtsc()")
    if n == 0:
        continue
    reads += n
    text = text.replace("__rdtsc()", "GuestTimebase()")
    if DECL not in text:
        m = re.search(r"^#include [^\n]*\n", text, re.M)
        text = text[:m.end()] + DECL + text[m.end():]
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    changed += 1
print(f"patched {reads} mftb reads in {changed} files")
