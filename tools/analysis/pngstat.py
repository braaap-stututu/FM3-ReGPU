import struct, sys, zlib, glob, os

def read_png(path):
    data = open(path, 'rb').read()
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, t = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif t == b'IDAT':
            idat += body
        pos += 12 + n
    bpp = {2: 3, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    i = 0
    for y in range(h):
        f = raw[i]; line = bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line); prev = line
    return w, h, bpp, rows

def luma(rows, bpp, y0, y1, w):
    s = n = 0
    for y in range(y0, y1, 4):
        r = rows[y]
        for x in range(0, w * bpp, bpp * 4):
            s += 0.299 * r[x] + 0.587 * r[x + 1] + 0.114 * r[x + 2]; n += 1
    return s / n

def road(rows, bpp, x0=820, x1=1260, y0=560, y1=715):
    # mean absolute luma difference between horizontally adjacent pixels: streak/noise energy
    s = n = 0; tot = 0
    for y in range(y0, y1, 2):
        r = rows[y]; prev = None
        for x in range(x0, x1):
            i = x * bpp; l = 0.299 * r[i] + 0.587 * r[i + 1] + 0.114 * r[i + 2]; tot += l
            if prev is not None: s += abs(l - prev); n += 1
            prev = l
    return s / n, tot / (n + (y1 - y0) // 2)

if __name__ == "__main__":
    MODE = "road" if sys.argv[1] == "--road" else "rows"
    args = sys.argv[2:] if MODE == "road" else sys.argv[1:]
    for p in args:
        for f in sorted(glob.glob(p)):
            w, h, bpp, rows = read_png(f)
            if MODE == "road":
                e, m = road(rows, bpp); print(f"{os.path.basename(f):<44} road edge {e:6.2f}  road mean {m:6.1f}"); continue
            print(f"{os.path.basename(f):<44} all {luma(rows,bpp,0,h,w):6.1f}  rows<512 {luma(rows,bpp,0,512,w):6.1f}  "
                  f"rows 496-511 {luma(rows,bpp,496,512,w):6.1f}  rows 512-527 {luma(rows,bpp,512,528,w):6.1f}  rows>=512 {luma(rows,bpp,512,h,w):6.1f}")


