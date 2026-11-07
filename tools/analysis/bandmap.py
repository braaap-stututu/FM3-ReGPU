"""Per-block detail (mean |horizontal luma step|) over the road area of several same-frame shots.
Band blocks: final detail much lower than the composed-texture-only shot. Then, for each intermediate,
its detail in band blocks vs in normal blocks."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png

D = r"G:\claude code projects\FM3-ReGPU recomp\out\shots" + "\\"
B = 16
X0, X1, Y0, Y1 = 330, 1264, 480, 640       # road below the horizon, right of the grass
CAR = (470, 400, 810, 650)                 # skip the car

def energy(name):
    w, h, bpp, rows = read_png(D + name)
    out = {}
    for by in range(Y0, Y1, B):
        for bx in range(X0, X1, B):
            if CAR[0] <= bx < CAR[2] and CAR[1] <= by < CAR[3]:
                continue
            s = n = 0
            for y in range(by, by + B):
                r = rows[y]
                prev = None
                for x in range(bx, bx + B):
                    i = x * bpp
                    l = 0.299 * r[i] + 0.587 * r[i + 1] + 0.114 * r[i + 2]
                    if prev is not None:
                        s += abs(l - prev); n += 1
                    prev = l
            out[(bx, by)] = s / n
    return out

final = energy("pp_base.png")
tex = energy("rb_t0.png")
ratio = {k: final[k] / max(tex[k], 0.01) for k in final}
vals = sorted(ratio.values())
lo = vals[len(vals) // 5]       # bottom 20 %: band candidates
hi = vals[len(vals) // 2]
band = [k for k, v in ratio.items() if v <= lo]
norm = [k for k, v in ratio.items() if v >= hi]
print(f"blocks {len(ratio)}, final/texture detail ratio: p20 {lo:.2f}  median {hi:.2f}  max {vals[-1]:.2f}")
print("band blocks (x,y):", sorted(band)[:24])
for name in ["rb_t0.png", "rc_t0w.png", "ri_r10.png", "ri_r6.png", "ri_r9.png", "ri_r4.png", "ri_r1.png", "ri_r0.png", "pp_base.png"]:
    try:
        e = energy(name)
    except Exception as ex:
        print(name, "skip", ex); continue
    eb = sum(e[k] for k in band) / len(band)
    en = sum(e[k] for k in norm) / len(norm)
    print(f"{name:<14} detail in band blocks {eb:6.2f}   in normal blocks {en:6.2f}   band/normal {eb / max(en, 0.01):.2f}")
