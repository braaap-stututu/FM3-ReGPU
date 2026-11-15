"""Horizontal shift between two screenshots in boxes (session 38, VR stereo): which way and how far each box moved from
A to B (best match of a box's luminance in B over shifts -max..max pixels).

  python -I tools/analysis/stereoshift.py A.png B.png [x0,y0,x1,y1 ...] [--max N]

Boxes are in the screenshots' pixels. For a stereo pair (A left eye, B right eye) near objects move left from A to B
(negative shift), far ones barely move.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png  # noqa: E402


def luma_box(rows, bpp, x0, y0, x1, y1):
    out = []
    for y in range(y0, y1):
        r = rows[y]
        out.append([(r[x * bpp] * 3 + r[x * bpp + 1] * 6 + r[x * bpp + 2]) / 10.0 for x in range(x0, x1)])
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    mx = 24
    if '--max' in sys.argv:
        mx = int(sys.argv[sys.argv.index('--max') + 1])
        args = [a for a in args if a != str(mx)]
    a, b = args[0], args[1]
    boxes = [tuple(int(v) for v in s.split(',')) for s in args[2:]]
    wa, ha, bppa, ra = read_png(a)
    wb, hb, bppb, rb = read_png(b)
    for (x0, y0, x1, y1) in boxes:
        ref = luma_box(ra, bppa, x0, y0, x1, y1)
        best = None
        for d in range(-mx, mx + 1):
            if x0 + d < 0 or x1 + d > wb:
                continue
            cand = luma_box(rb, bppb, x0 + d, y0, x1 + d, y1)
            err = sum(abs(p - q) for ra_, rb_ in zip(ref, cand) for p, q in zip(ra_, rb_)) / ((x1 - x0) * (y1 - y0))
            if best is None or err < best[1]:
                best = (d, err)
        print(f'box {x0},{y0}-{x1},{y1}: shift {best[0]:+d} px (mean error {best[1]:.2f})')


main()
