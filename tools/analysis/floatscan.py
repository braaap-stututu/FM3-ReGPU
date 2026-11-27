"""Find a run of big-endian floats (within a tolerance) in a running fm3.exe's guest memory (2026-11-27: where the
game keeps a car's driver bone palette, the constants a capture shows rounded to 6 digits).
    python -I tools/analysis/floatscan.py V1 [V2 ...] [--tol T] [--stride S] [--max N]
Each hit is an address where V1, V2, ... follow each other (S bytes apart, default 4) to within T (default 1e-4).
Prints the guest address (inside the guest's 4 GB view) and the 16 floats from there. Read-only, like memscan.py.
"""
import ctypes
import struct
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from memmap import MBI, find_pid, k32, MEM_COMMIT  # noqa: E402

PAGE_RW = {0x04, 0x08, 0x40, 0x80}


def main():
    a = sys.argv[1:]
    tol, stride, mx = 1e-4, 4, 40
    vals = []
    i = 0
    while i < len(a):
        if a[i] == '--tol':
            tol = float(a[i + 1]); i += 2; continue
        if a[i] == '--stride':
            stride = int(a[i + 1]); i += 2; continue
        if a[i] == '--max':
            mx = int(a[i + 1]); i += 2; continue
        vals.append(float(a[i])); i += 1
    pid, h = find_pid("fm3.exe")
    if not h:
        sys.exit("fm3.exe is not running")
    addr, mbi, allocs = 0, MBI(), {}
    regions = []
    while k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        base = mbi.BaseAddress or 0
        allocs[mbi.AllocationBase or 0] = allocs.get(mbi.AllocationBase or 0, 0) + mbi.RegionSize
        if mbi.State == MEM_COMMIT and mbi.Protect in PAGE_RW:
            regions.append((base, mbi.RegionSize, mbi.AllocationBase or 0))
        addr = base + mbi.RegionSize
        if addr >= 1 << 47:
            break
    guest = max(allocs, key=allocs.get)
    hits = 0
    buf = ctypes.create_string_buffer(1 << 22)
    got = ctypes.c_size_t()
    for base, size, ab in regions:
        if ab != guest:
            continue
        off = 0
        while off < size:
            n = min(size - off, 1 << 22)
            if not k32.ReadProcessMemory(h, ctypes.c_void_p(base + off), buf, n, ctypes.byref(got)):
                off += n
                continue
            raw = np.frombuffer(buf.raw[:got.value - got.value % 4], '>f4')
            with np.errstate(invalid='ignore'):
                cand = np.nonzero(np.abs(raw - vals[0]) < tol)[0]
            for c in cand:
                ok = True
                for k, v in enumerate(vals[1:], 1):
                    j = c + k * stride // 4
                    if j >= len(raw) or not abs(float(raw[j]) - v) < tol:
                        ok = False
                        break
                if ok:
                    g = base + off + 4 * c - guest
                    print('%08X: %s' % (g, ' '.join('%.5g' % x for x in raw[c:c + 16])))
                    hits += 1
                    if hits >= mx:
                        return
            off += n
    print(f'{hits} hits')


if __name__ == '__main__':
    main()
