"""Mean and detail (mean |horizontal step|) of luma in two boxes, for several shots."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png

D = r"G:\claude code projects\FM3-ReGPU recomp\out\shots" + "\\"
BOXES = {"smooth": (868, 628, 884, 644), "grainy": (982, 612, 998, 628)}

def stats(name, box):
    w, h, bpp, rows = read_png(D + name)
    x0, y0, x1, y1 = box
    s = n = tot = m = 0
    for y in range(y0, y1):
        r = rows[y]; prev = None
        for x in range(x0, x1):
            i = x * bpp
            l = 0.299 * r[i] + 0.587 * r[i + 1] + 0.114 * r[i + 2]
            tot += l; m += 1
            if prev is not None:
                s += abs(l - prev); n += 1
            prev = l
    # also the raw RGB of the box centre
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    i = cx * bpp
    rgb = tuple(rows[cy][i:i + 3])
    return tot / m, s / n, rgb

for name in sys.argv[1:]:
    parts = [name]
    for k, b in BOXES.items():
        mean, det, rgb = stats(name, b)
        parts.append(f"{k}: mean {mean:6.1f} detail {det:5.2f} rgb {rgb}")
    print("  ".join(parts))
