import re, sys
from collections import defaultdict

HEAD = re.compile(r"gpu passes: ([0-9.]+) ms/frame over (\d+) frames")
ROW = re.compile(r"gpu passes:\s+([0-9.]+) ms\s+(\d+) draws\s+(.*)$")


def load(p):
    blocks = []
    cur = None
    for line in open(p, encoding="utf-8-sig", errors="replace"):
        m = HEAD.search(line)
        if m:
            cur = {"total": float(m.group(1)), "rows": {}}
            blocks.append(cur)
            continue
        m = ROW.search(line)
        if m and cur is not None:
            label = m.group(3).strip()
            if label.startswith("resolve to"):
                label = "resolve " + label.split("(", 1)[-1]
            t, d = cur["rows"].get(label, (0.0, 0))
            cur["rows"][label] = (t + float(m.group(1)), d + int(m.group(2)))
    blocks = blocks[1:] if len(blocks) > 2 else blocks  # first block may straddle the pause
    total = sum(b["total"] for b in blocks) / len(blocks)
    rows = defaultdict(lambda: [0.0, 0.0])
    for b in blocks:
        for k, (t, d) in b["rows"].items():
            rows[k][0] += t / len(blocks)
            rows[k][1] += d / len(blocks)
    return total, rows, len(blocks)


ta, a, na = load(sys.argv[1])
tb, b, nb = load(sys.argv[2])
print("race %.2f ms/frame (%d blocks), tt %.2f ms/frame (%d blocks), diff %.2f" % (ta, na, tb, nb, ta - tb))
print("listed: race %.2f tt %.2f" % (sum(v[0] for v in a.values()), sum(v[0] for v in b.values())))
keys = set(a) | set(b)
rows = sorted(keys, key=lambda k: a.get(k, [0, 0])[0] - b.get(k, [0, 0])[0], reverse=True)
print(" diff ms   race ms  tt ms   race dr  tt dr  label")
for k in rows[:int(sys.argv[3]) if len(sys.argv) > 3 else 30]:
    ra, rb = a.get(k, [0, 0]), b.get(k, [0, 0])
    print("%7.3f  %7.3f %7.3f  %6.0f %6.0f  %s" % (ra[0] - rb[0], ra[0], rb[0], ra[1], rb[1], k))
