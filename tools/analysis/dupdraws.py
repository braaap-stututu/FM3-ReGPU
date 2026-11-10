"""Duplicate-draw check on a capture with REGPU_TRACE_VC: per draw the shaders, index buffer (initiator + dma),
vertex fetch addresses, textures, render state, pixel constants and the vertex constants the shader reads.
Reports exact duplicates (everything equal), and same geometry drawn more than once in a pass with different
constants or shaders."""
import re
import sys
from collections import Counter, defaultdict

DRAW = re.compile(r"\] draw #(\d+) vs (\w+) ps (\w+) prim (\d+) n (\d+) blend (\w+) mask (\w+) scissor (\S+).*?pitch (\d+)(.*)$")
RESOLVE = re.compile(r"\] resolve: ")
IDX = re.compile(r"indices \(initiator (\w+) dma (\w+) offset (\w+)\)")
VF = re.compile(r"    (vf\d+): (\w+) size (\d+)")
PC = re.compile(r"    (pc\d+) = (.*)$")
VC = re.compile(r"    (vc\d+) = (.*) \(read\)$")
SURF = re.compile(r"    surface (.*)$")

draws = []
cur = None
pas = 0
for line in open(sys.argv[1], encoding="utf-8-sig", errors="replace"):
    if RESOLVE.search(line):
        pas += 1
        continue
    m = DRAW.search(line)
    if m:
        tex = " ".join(t.split(" [")[0] for t in re.findall(r"t\d+=\S+ \S+", m.group(10)))
        cur = {"n": int(m.group(1)), "vs": m.group(2), "ps": m.group(3), "prim": m.group(4), "state": (m.group(6), m.group(7), m.group(8), m.group(9)),
               "tex": tex, "pass": pas, "idx": None, "vf": [], "pc": [], "vc": [], "surf": ""}
        draws.append(cur)
        continue
    if cur is None:
        continue
    for rx, key in ((IDX, "idx"), (VF, "vf"), (PC, "pc"), (VC, "vc"), (SURF, "surf")):
        mm = rx.search(line)
        if mm:
            if key == "idx":
                cur["idx"] = mm.groups()
            elif key == "surf":
                cur["surf"] = mm.group(1)
            else:
                cur[key].append(mm.groups())
            break

print("draws parsed:", len(draws), " with vertex constants:", sum(1 for d in draws if d["vc"]))
geom = lambda d: (d["idx"], tuple(d["vf"]))
full = lambda d: (d["pass"], d["vs"], d["ps"], d["prim"], d["state"], d["tex"], d["surf"], geom(d), tuple(d["pc"]), tuple(d["vc"]))

c = Counter(full(d) for d in draws if d["idx"])
exact = sum(n - 1 for n in c.values() if n > 1)
print("\nexact duplicates (same pass, shaders, state, textures, geometry, pc and vc): %d extra draws" % exact)
for k, n in c.most_common(12):
    if n < 2:
        break
    print("  x%d  pass %d vs %s ps %s idx %s" % (n, k[0], k[1][-8:], k[2][-8:], k[7][0]))

# same geometry + same vs + same vertex constants in the same pass: geometry drawn twice in the same place
same_place = defaultdict(list)
for d in draws:
    if d["idx"] and d["vc"]:
        same_place[(d["pass"], geom(d), d["vs"], tuple(d["vc"]))].append(d)
multi = {k: v for k, v in same_place.items() if len(v) > 1}
extra = sum(len(v) - 1 for v in multi.values())
print("\nsame geometry, same vertex shader and constants, same pass (drawn again on the same spot): %d groups, %d extra draws" % (len(multi), extra))
bypass = Counter()
for k, v in multi.items():
    bypass[k[0]] += len(v) - 1
print("  extra draws by pass:", dict(sorted(bypass.items())))
shown = 0
for k, v in sorted(multi.items(), key=lambda kv: -len(kv[1])):
    pss = [d["ps"][-8:] for d in v]
    st = [d["state"][0] for d in v]
    print("  x%d pass %d vs %s idx %s ps %s blend %s" % (len(v), k[0], k[2][-8:], k[1][0][1] if k[1][0] else "-", ",".join(pss), ",".join(st)))
    shown += 1
    if shown >= 15:
        break

# same geometry anywhere in the frame (any pass, any constants): how often each mesh is drawn
g = Counter(geom(d) for d in draws if d["idx"] and d["idx"][1] != "00000000")
hist = Counter(g.values())
print("\nmesh reuse across the frame (index buffer + vertex buffers): draws per mesh -> meshes:", dict(sorted(hist.items())))
