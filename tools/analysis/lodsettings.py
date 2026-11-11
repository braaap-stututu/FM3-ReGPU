"""FM3's per-track LOD and detail settings (Media/Tracks/<track>/TrackSettings.xml), session 19.

  python -I tools/analysis/lodsettings.py list [TRACK...]
      the LOD/distance fields of every track (or those named), one row each
  python -I tools/analysis/lodsettings.py mod NAME FIELD=VALUE [...] [--tracks T1,T2]
      writes changed copies to out/user/mods/NAME/game/Media/Tracks/<track>/TrackSettings.xml, which the game opens
      instead of the disc's (imports_io.cpp loose-file mods; log line "mods: ..."). VALUE: x2 (times), +0.5 (plus),
      =100 (set). Rename the folder to _NAME to switch it off, or FM3_NO_OVERLAY=1 for all mods.
      Example: mod lodx2 crowd=x2 sprite=x2 grass=x2 cubefar=x2 shadowend=x1.5
  python -I tools/analysis/lodsettings.py fields
      the field names mod understands

The game folder: FM3_GAME_DIR, else the hl360 Disc 1 copy play.cmd uses. Where each value is read in code:
tools/analysis/xref.py refs <name> (the TrackSettings parser is sub_8283CE78).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.environ.get("FM3_GAME_DIR") or os.path.join(ROOT, "reference", "rexglue recomp reference", "hl360-forza3", "game", "disc1")
TRACKS = os.path.join(GAME, "Media", "Tracks")
MODS = os.path.join(ROOT, "out", "user", "mods")

# short name -> (element, attribute): the LOD, distance and detail knobs TrackSettings has
FIELDS = {
    "crowd0": ("Crowds", "lod0"), "crowd1": ("Crowds", "lod1"), "crowd2": ("Crowds", "lod2"), "crowd3": ("Crowds", "lod3"),
    "sprite": ("Crowds", "SpriteDistance"), "crowdsize": ("Performance", "CrowdSize"),
    "grass": ("GrassLODDist", "value"), "grassfade": ("GrassLODFadeRange", "value"),
    "grassmin": ("GrassMinNumShells", "value"), "grassmax": ("GrassMaxNumShells", "value"),
    "cubefar": ("Cubemap", "FarDistance"), "meshlimit": ("Cubemap", "MeshLimit"), "meshsize": ("Cubemap", "MeshSizeThreshold"),
    "cubefaces": ("Cubemap", "FacesPerFrame"), "cubevis": ("Cubemap", "VisibilityThreshold"),
    "mirrorvis": ("Mirror", "VisibilityThreshold"), "mipbias": ("MipMapLodBiasModifier", "value"),
    "shadowend": ("ShadowEnds", "InGameShadowEnd"), "segshadowend": ("ShadowEnds", "TrackSegCamShadowEnd"),
    "dyncube": ("Performance", "DynamicCubemap"), "aishadows": ("Performance", "NonFocusCarProjectedShadows"),
}
# "crowd" scales all four crowd LOD distances
GROUPS = {"crowd": ["crowd0", "crowd1", "crowd2", "crowd3"]}


def tracks():
    return sorted(d for d in os.listdir(TRACKS) if os.path.isfile(os.path.join(TRACKS, d, "TrackSettings.xml")))


def read(track):
    return open(os.path.join(TRACKS, track, "TrackSettings.xml"), "rb").read().decode("latin-1")


def get(text, element, attr):
    m = re.search(r"<" + element + r"\b[^>]*?\b" + attr + r'="([^"]*)"', text)
    return m.group(1) if m else None


def put(text, element, attr, fn):
    def sub(m):
        old = m.group(2)
        new = fn(float(old))
        out = str(int(round(new))) if re.fullmatch(r"-?\d+", old) else f"{new:.6f}"
        return m.group(1) + out + '"'
    return re.sub(r"(<" + element + r"\b[^>]*?\b" + attr + r'=")([^"]*)"', sub, text, count=1)


def op(spec):
    if spec.startswith("x"):
        k = float(spec[1:])
        return lambda v: v * k
    if spec.startswith("+") or spec.startswith("-"):
        k = float(spec)
        return lambda v: v + k
    if spec.startswith("="):
        k = float(spec[1:])
        return lambda v: k
    raise SystemExit(f"value {spec}: use x2, +0.5, -1 or =100")


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("list", "mod", "fields"):
        print(__doc__)
        return
    if args[0] == "fields":
        for k, (e, a) in FIELDS.items():
            print(f"{k:12s} <{e} {a}>")
        print("crowd        all four crowd LOD distances")
        return
    if args[0] == "list":
        names = args[1:] or tracks()
        cols = ["crowd0", "crowd1", "crowd2", "crowd3", "sprite", "crowdsize", "grass", "grassfade", "cubefar", "meshlimit", "meshsize",
                "cubefaces", "cubevis", "mirrorvis", "mipbias", "shadowend", "dyncube"]
        print(f"{'track':14s}" + "".join(f"{c:>10s}" for c in cols))
        for t in names:
            text = read(t)
            row = []
            for c in cols:
                v = get(text, *FIELDS[c])
                row.append("-" if v is None else (f"{float(v):g}"))
            print(f"{t:14s}" + "".join(f"{v:>10s}" for v in row))
        return
    # mod
    if len(args) < 3:
        print(__doc__)
        return
    name, specs = args[1], args[2:]
    only = None
    if "--tracks" in specs:
        i = specs.index("--tracks")
        only = specs[i + 1].split(",")
        specs = specs[:i] + specs[i + 2:]
    changes = []
    for s in specs:
        key, _, value = s.partition("=")
        if not value:
            raise SystemExit(f"{s}: FIELD=VALUE")
        if value[0].isdigit():
            value = "=" + value
        for k in GROUPS.get(key, [key]):
            if k not in FIELDS:
                raise SystemExit(f"unknown field {k} (see: fields)")
            changes.append((FIELDS[k], op(value), k))
    written = 0
    for t in tracks():
        if only and t not in only:
            continue
        text = read(t)
        for (element, attr), fn, _ in changes:
            text = put(text, element, attr, fn)
        out = os.path.join(MODS, name, "game", "Media", "Tracks", t)
        os.makedirs(out, exist_ok=True)
        open(os.path.join(out, "TrackSettings.xml"), "wb").write(text.encode("latin-1"))
        written += 1
    print(f"{written} tracks written to {os.path.join(MODS, name)} ({', '.join(k for _, _, k in changes)})")


if __name__ == "__main__":
    main()
