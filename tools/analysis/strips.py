import glob, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import dumpdecode as dd

d = r"G:\claude code projects\FM3-ReGPU recomp\out\strips"
meta = {"fmt": 18, "w": 32, "h": 512, "pitch": 4, "tiled": 1, "endian": 1}
rows = []
for f in sorted(glob.glob(os.path.join(d, "strip_*.bin"))):
    data = open(f, "rb").read()
    w, h, rgba = dd.decode_texture(meta, data)
    dd.write_png(f[:-4] + ".png", w, h, rgba)
    # detail: mean |luma difference| between vertical neighbours; mean luma
    def L(x, y):
        o = (y * w + x) * 4
        return 0.299 * rgba[o] + 0.587 * rgba[o + 1] + 0.114 * rgba[o + 2]
    s = n = tot = 0
    for y in range(h - 1):
        for x in range(w):
            a = L(x, y); s += abs(a - L(x, y + 1)); n += 1; tot += a
    zero = data.count(0) / len(data)
    rows.append((os.path.basename(f), s / n, tot / n, zero))
for name, e, m, z in rows:
    print(f"{name} detail {e:6.2f} mean {m:6.1f} zero-bytes {z:5.2f}")
