import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pngstat import read_png
import dumpdecode as dd

src, dst, x0, y0, x1, y1, k = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:8])
w, h, bpp, rows = read_png(src)
cw, ch = (x1 - x0) * k, (y1 - y0) * k
out = bytearray(cw * ch * 4)
for y in range(ch):
    r = rows[y0 + y // k]
    for x in range(cw):
        i = (x0 + x // k) * bpp
        o = (y * cw + x) * 4
        out[o:o + 3] = r[i:i + 3]
        out[o + 3] = 255
dd.write_png(dst, cw, ch, out)
print(dst, cw, ch)
