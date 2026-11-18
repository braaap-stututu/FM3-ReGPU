"""Find values in a running fm3.exe's guest memory (session 44: where the profile keeps its credits).

    python -I tools/analysis/memscan.py VALUE [NEAR_VALUE [WINDOW]]

Scans committed read/write memory of fm3.exe for VALUE as a big-endian 32-bit integer (the guest is big-endian)
and, with NEAR_VALUE, prints only hits that have NEAR_VALUE within WINDOW bytes (default 512). Prints host
addresses and, for hits inside the guest's 4 GB view (found as the largest reservation), the guest address.
Read-only (query + read rights), like memmap.py.
"""
import ctypes
import ctypes.wintypes as w
import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from memmap import MBI, find_pid, k32, MEM_COMMIT  # noqa: E402

PAGE_RW = {0x04, 0x08, 0x40, 0x80}


def main():
    value = int(sys.argv[1], 0)
    near = int(sys.argv[2], 0) if len(sys.argv) > 2 else None
    window = int(sys.argv[3], 0) if len(sys.argv) > 3 else 512
    pid, h = find_pid("fm3.exe")
    if not h:
        sys.exit("fm3.exe is not running")
    # The guest view: the allocation base with the most committed/reserved bytes in one allocation.
    addr, mbi, allocs = 0, MBI(), {}
    regions = []
    while k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        base = mbi.AllocationBase or 0
        if mbi.State != 0x10000:  # not free
            allocs[base] = allocs.get(base, 0) + mbi.RegionSize
        if mbi.State == MEM_COMMIT and (mbi.Protect & 0xFF) in PAGE_RW:
            regions.append((mbi.BaseAddress or 0, mbi.RegionSize, base))
        addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if addr >= 1 << 47:
            break
    guest = max(allocs.items(), key=lambda kv: kv[1])[0]
    needle = struct.pack(">I", value)
    near_b = struct.pack(">I", near) if near is not None else None
    hits = 0
    for start, size, base in regions:
        buf = (ctypes.c_char * size)()
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(h, ctypes.c_void_p(start), buf, size, ctypes.byref(got)):
            continue
        data = bytes(buf[: got.value])
        i = data.find(needle)
        while i >= 0:
            if i % 4 == 0:
                ok = True
                where = ""
                if near_b is not None:
                    lo, hi = max(0, i - window), min(len(data), i + window)
                    j = data.find(near_b, lo, hi)
                    ok = j >= 0
                    where = f" near value at {j - i:+d}" if ok else ""
                if ok:
                    host = start + i
                    g = f" guest {host - guest:08X}" if base == guest else ""
                    print(f"host {host:016X}{g}{where}")
                    hits += 1
                    if hits > 200:
                        return
            i = data.find(needle, i + 1)
    print(f"{hits} hits")


if __name__ == "__main__":
    main()
