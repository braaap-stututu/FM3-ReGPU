"""Pixel difference of two same-size screenshots (session 20): how many pixels differ, by how much, and where
(a coarse grid of the changed share), to check that a performance switch leaves the picture alone.

  python -I tools/analysis/pngdiff.py A.png B.png [threshold]

threshold: the per-channel difference that counts as changed (default 8; paused scenes still shimmer by 1-3).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png  # noqa: E402


def main():
    a, b = read_png(sys.argv[1]), read_png(sys.argv[2])
    thr = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    (w, h, ca, pa), (w2, h2, cb, pb) = a, b
    if (w, h) != (w2, h2):
        sys.exit("sizes differ")
    gx, gy = 16, 9
    grid = [[0] * gx for _ in range(gy)]
    changed = 0
    total = 0
    worst = 0
    for y in range(h):
        ra, rb = pa[y], pb[y]
        for x in range(w):
            ia, ib = x * ca, x * cb
            d = max(abs(ra[ia] - rb[ib]), abs(ra[ia + 1] - rb[ib + 1]), abs(ra[ia + 2] - rb[ib + 2]))
            total += d
            worst = max(worst, d)
            if d > thr:
                changed += 1
                grid[y * gy // h][x * gx // w] += 1
    cell = (w // gx) * (h // gy)
    print("%dx%d: %d pixels over %d (%.3f%%), mean difference %.3f, worst %d" % (w, h, changed, thr, 100.0 * changed / (w * h), total / (w * h), worst))
    for row in grid:
        print(" ".join("%5.1f" % (100.0 * c / cell) for c in row))


main()
