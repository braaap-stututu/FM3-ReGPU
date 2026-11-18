"""Where a running process's memory is (session 43): walks its address space with VirtualQueryEx and sums committed
memory by type (private, mapped, image) and by allocation, largest first, plus the loaded modules' sizes.

    python -I tools/analysis/memmap.py [process name, default fm3.exe] [top N allocations, default 25]

Windows only, no third-party modules (ctypes). Read-only: opens the process for query and memory read rights.
"""
import ctypes
import ctypes.wintypes as w
import sys

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p), ("AllocationProtect", w.DWORD),
                ("PartitionId", w.WORD), ("RegionSize", ctypes.c_size_t), ("State", w.DWORD), ("Protect", w.DWORD), ("Type", w.DWORD)]


MEM_COMMIT, MEM_PRIVATE, MEM_MAPPED, MEM_IMAGE = 0x1000, 0x20000, 0x40000, 0x1000000
PAGE_NOACCESS = 0x01


def find_pid(name):
    arr = (w.DWORD * 4096)()
    needed = w.DWORD()
    psapi.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(needed))
    for pid in arr[: needed.value // 4]:
        h = k32.OpenProcess(0x0410, False, pid)
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(260)
        if psapi.GetModuleBaseNameW(h, None, buf, 260) and buf.value.lower() == name.lower():
            return pid, h
        k32.CloseHandle(h)
    return None, None


def mapped_name(h, addr):
    buf = ctypes.create_unicode_buffer(520)
    if psapi.GetMappedFileNameW(h, ctypes.c_void_p(addr), buf, 520):
        return buf.value.split("\\")[-1]
    return ""


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "fm3.exe"
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    pid, h = find_pid(name)
    if not h:
        sys.exit(f"{name} is not running")
    totals = {"private": 0, "mapped": 0, "image": 0}
    allocs = {}  # allocation base -> [type, committed bytes, protect]
    addr = 0
    mbi = MBI()
    while k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        size = mbi.RegionSize
        if mbi.State == MEM_COMMIT:
            kind = "private" if mbi.Type == MEM_PRIVATE else "mapped" if mbi.Type == MEM_MAPPED else "image"
            totals[kind] += size
            base = mbi.AllocationBase or 0
            entry = allocs.setdefault(base, [kind, 0, mbi.AllocationProtect])
            entry[1] += size
        addr = (mbi.BaseAddress or 0) + size
        if addr >= 1 << 47:
            break
    mb = 1 << 20
    print(f"{name} pid {pid}: committed private {totals['private'] / mb:.0f} MB, mapped {totals['mapped'] / mb:.0f} MB, "
          f"image {totals['image'] / mb:.0f} MB")
    # Size classes of private allocations: what kind of allocator made them.
    classes = {}
    for base, (kind, size, _) in allocs.items():
        if kind != "private":
            continue
        c = "<1 MB" if size < mb else "1-16 MB" if size < 16 * mb else "16-128 MB" if size < 128 * mb else ">=128 MB"
        n, s = classes.get(c, (0, 0))
        classes[c] = (n + 1, s + size)
    for c in ["<1 MB", "1-16 MB", "16-128 MB", ">=128 MB"]:
        if c in classes:
            print(f"  private allocations {c:>9}: {classes[c][0]:6d} of them, {classes[c][1] / mb:8.0f} MB")
    # Resident pages (the working set: what really takes RAM) per allocation, from QueryWorkingSet.
    resident = {}
    n = 1 << 21
    while True:
        buf = (ctypes.c_size_t * (n + 1))()
        if psapi.QueryWorkingSet(h, buf, ctypes.sizeof(buf)):
            break
        n *= 2
        if n > 1 << 26:
            break
    entries = buf[0]
    bases = sorted(allocs.keys())
    import bisect
    shared_ws = 0
    for i in range(1, min(entries, n) + 1):
        e = buf[i]
        page = e & ~0xFFF
        if (e >> 8) & 1:  # Shared bit
            shared_ws += 4096
        j = bisect.bisect_right(bases, page) - 1
        if j >= 0:
            resident[bases[j]] = resident.get(bases[j], 0) + 4096
    print(f"working set {entries * 4096 / mb:.0f} MB ({shared_ws / mb:.0f} MB of it shared pages)")
    print(f"largest {top} allocations by resident memory (committed in brackets):")
    for base, res in sorted(resident.items(), key=lambda kv: -kv[1])[:top]:
        kind, size, prot = allocs[base]
        extra = mapped_name(h, base) if kind != "private" else ""
        print(f"  {base:016X} {kind:7s} {res / mb:9.1f} MB  ({size / mb:7.1f})  protect {prot:#x} {extra}")
    k32.CloseHandle(h)


if __name__ == "__main__":
    main()
