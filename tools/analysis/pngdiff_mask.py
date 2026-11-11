"""Pixel difference of two screenshots outside the areas that differ anyway (session 23).

  python -I tools/analysis/pngdiff_mask.py A.png B.png NOISE1.png NOISE2.png [threshold]

NOISE1/NOISE2: two shots of the same scene with nothing changed (e.g. the frozen test grid from two boots: the
grandstand crowd animates on its own clock, so it differs between boots). Every 40x40 block where they differ by
more than the threshold is left out; the rest of A vs B is compared as pngdiff.py does. Prints one line:
  masked: N of M pixels over T (P%), worst W, K of B blocks left out
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png  # noqa: E402

BLOCK = 40


def diff_rows(p, q, w, h, c1, c2, thr, fn):
    for y in range(h):
        ra, rb = p[y], q[y]
        for x in range(w):
            i, j = x * c1, x * c2
            d = max(abs(ra[i] - rb[j]), abs(ra[i + 1] - rb[j + 1]), abs(ra[i + 2] - rb[j + 2]))
            if d > thr:
                fn(x, y, d)


def main():
    a, b, n1, n2 = (read_png(p) for p in sys.argv[1:5])
    thr = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    w, h = a[0], a[1]
    if any((x[0], x[1]) != (w, h) for x in (b, n1, n2)):
        sys.exit("sizes differ")
    bw, bh = (w + BLOCK - 1) // BLOCK, (h + BLOCK - 1) // BLOCK
    noisy = set()
    diff_rows(n1[3], n2[3], w, h, n1[2], n2[2], thr, lambda x, y, d: noisy.add((x // BLOCK, y // BLOCK)))
    stats = {"n": 0, "worst": 0}

    def hit(x, y, d):
        if (x // BLOCK, y // BLOCK) not in noisy:
            stats["n"] += 1
            stats["worst"] = max(stats["worst"], d)

    diff_rows(a[3], b[3], w, h, a[2], b[2], thr, hit)
    kept = w * h - len(noisy) * BLOCK * BLOCK
    print("masked: %d of %d pixels over %d (%.4f%%), worst %d, %d of %d blocks left out" % (
        stats["n"], kept, thr, 100.0 * stats["n"] / max(1, kept), stats["worst"], len(noisy), bw * bh))


main()
