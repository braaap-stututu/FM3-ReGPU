"""Render passes of a captured frame (session 19, LOD research): the draws between two resolves, with the surface pitch,
the draw count and the resolve that ends the pass (destination size), so two captures can be compared pass by pass.

  python -I tools/analysis/passes.py CAPTURE.log [OTHER.log]

CAPTURE.log: the fm3.log lines of a `Fm3 "capture"` (draw lines `draw #N ... pitch P`, resolve lines `resolve: ... rect`).
With two files the passes are lined up in order and the draw counts compared (passes that differ are marked).
"""
import re
import sys

DRAW = re.compile(r"\] draw #\d+ .*?pitch (\d+)")
RESOLVE = re.compile(r"\] resolve: control (\w+) dest (\w+) pitch \w+ info (\w+) rect (\S+)")


def passes(path):
    out = []
    draws, pitch = 0, None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = DRAW.search(line)
        if m:
            draws += 1
            pitch = int(m.group(1))
            continue
        m = RESOLVE.search(line)
        if m:
            info = int(m.group(3), 16)
            out.append((draws, pitch, m.group(4), (info >> 7) & 0x3F))
            draws = 0
    if draws:
        out.append((draws, pitch, "(no resolve)", 0))
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    a = passes(sys.argv[1])
    if len(sys.argv) == 2:
        for i, (n, pitch, rect, fmt) in enumerate(a):
            print(f"{i:3d} {n:5d} draws  pitch {pitch}  -> {rect} fmt {fmt}")
        print("total", sum(p[0] for p in a), "draws in", len(a), "passes")
        return
    b = passes(sys.argv[2])
    for i in range(max(len(a), len(b))):
        pa = a[i] if i < len(a) else (0, None, "-", 0)
        pb = b[i] if i < len(b) else (0, None, "-", 0)
        mark = "  <--" if pa[0] != pb[0] or pa[2] != pb[2] else ""
        print(f"{i:3d} {pa[0]:5d} | {pb[0]:5d}  {pa[2]:>18s} | {pb[2]:<18s} pitch {pa[1]}/{pb[1]}{mark}")
    print("totals", sum(p[0] for p in a), "|", sum(p[0] for p in b))


if __name__ == "__main__":
    main()
