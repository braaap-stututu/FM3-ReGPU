# Vinyl editor side view (1280x720 shot): how far up the green vinyl reaches on the car.
# Usage: python -I greentop.py shot.png [...]. Prints the median first green row over columns 450-750
# (complete vinyl: ~130-160; cut: ~260-270) and the green pixel count in the car box.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png

X0, X1, Y0, Y1 = 250, 960, 100, 420  # car box

def green(p):
    r, g, b = p[0], p[1], p[2]
    return g > r + 40 and g > b + 20

for path in sys.argv[1:]:
    w, h, bpp, rows = read_png(path)
    count, tops = 0, []
    for x in range(X0, X1):
        top = None
        for y in range(Y0, Y1):
            if green(rows[y][x * bpp:x * bpp + 3]):
                count += 1
                if top is None:
                    top = y
        if 450 <= x < 750 and top is not None:
            tops.append(top)
    tops.sort()
    print(f"{os.path.basename(path)}: green top {tops[len(tops) // 2] if tops else None}, green px {count}")
