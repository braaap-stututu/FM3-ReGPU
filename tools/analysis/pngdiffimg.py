"""Writes a picture of where two same-size screenshots differ (session 23): A dimmed to 30% in grey, every pixel
that differs by more than the threshold in red, optionally cropped and enlarged.

  python -I tools/analysis/pngdiffimg.py A.png B.png OUT.png [threshold] [x0 y0 x1 y1 [scale]]
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png  # noqa: E402


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(t, body):
        return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    a, b = read_png(sys.argv[1]), read_png(sys.argv[2])
    thr = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    w, h, ca, pa = a
    cb, pb = b[2], b[3]
    x0, y0, x1, y1 = (int(v) for v in sys.argv[5:9]) if len(sys.argv) > 8 else (0, 0, w, h)
    scale = int(sys.argv[9]) if len(sys.argv) > 9 else 1
    rows = []
    for y in range(y0, y1):
        r = bytearray()
        for x in range(x0, x1):
            i, j = x * ca, x * cb
            d = max(abs(pa[y][i + k] - pb[y][j + k]) for k in range(3))
            if d > thr:
                px = (255, 0, 0)
            else:
                g = (pa[y][i] + pa[y][i + 1] + pa[y][i + 2]) // 10
                px = (g, g, g)
            r += bytes(px) * scale
        for _ in range(scale):
            rows.append(r)
    write_png(sys.argv[3], (x1 - x0) * scale, (y1 - y0) * scale, rows)
    print(sys.argv[3])


main()
