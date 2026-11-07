"""Decode and diff FM3 frame dumps written by the patch-0007 texture dump hook.

A dump folder (frame_NNNN/) holds index.txt plus raw guest-memory textures
(tex_<base>_<w>x<h>_f<fmt>_<hash>.bin). index.txt data lines have fixed token
positions (see patches/sdk/0007-frame-dump-hook.patch):

  0 "draw" 1 draw# 2 "slot" 3 fetch# 4 "dim" 5 dim 6 "size" 7 w 8 h 9 "pitch"
  10 pitch/32 11 tiled 12 "fmt" 13 format 14 endian 15 "base" 16 addr
  17-22 fetch dwords 23 "vs" 24 hash 25 "ps" 26 hash 27 file

Commands:
  dumpdecode.py list   <frame_dir>
  dumpdecode.py decode <frame_dir> [--out DIR] [--only NAME_SUBSTRING] [--raw]
  dumpdecode.py diff   <frame_dir_a> <frame_dir_b> [--out DIR] [--tolerance N]

decode writes one RGBA PNG per distinct texture into DIR (default
<frame_dir>/png), with the fetch constant's swizzle applied (what shaders see;
--raw keeps the stored channel order). Integer/float formats are normalised
for viewing. diff always compares the raw stored channels. diff matches textures by (base, size, format),
decodes both sides and reports identical / max channel delta / PSNR, lists
textures present on one side only, and compares the resolve sequence. With
--out it writes a |a-b| x8 difference PNG for every texture that differs.

Pure Python (no numpy): large textures take a few seconds each.
"""
import math
import os
import struct
import sys
import zlib

# --- format table -----------------------------------------------------------
# name, block width/height in texels, bytes per block
FORMATS = {
    2: ("k_8", 1, 1), 3: ("k_1_5_5_5", 1, 2), 4: ("k_5_6_5", 1, 2), 5: ("k_6_5_5", 1, 2),
    6: ("k_8_8_8_8", 1, 4), 7: ("k_2_10_10_10", 1, 4), 8: ("k_8_A", 1, 1), 9: ("k_8_B", 1, 1),
    10: ("k_8_8", 1, 2), 14: ("k_8_8_8_8_A", 1, 4), 15: ("k_4_4_4_4", 1, 2),
    16: ("k_10_11_11", 1, 4), 17: ("k_11_11_10", 1, 4),
    18: ("k_DXT1", 4, 8), 19: ("k_DXT2_3", 4, 16), 20: ("k_DXT4_5", 4, 16),
    22: ("k_24_8", 1, 4), 23: ("k_24_8_FLOAT", 1, 4), 24: ("k_16", 1, 2), 25: ("k_16_16", 1, 4),
    26: ("k_16_16_16_16", 1, 8), 30: ("k_16_FLOAT", 1, 2), 31: ("k_16_16_FLOAT", 1, 4),
    32: ("k_16_16_16_16_FLOAT", 1, 8), 33: ("k_32", 1, 4), 36: ("k_32_FLOAT", 1, 4),
    37: ("k_32_32_FLOAT", 1, 8), 49: ("k_DXN", 4, 16), 58: ("k_DXT3A", 4, 8),
    59: ("k_DXT5A", 4, 8), 60: ("k_CTX1", 4, 8),
}


# --- helpers -----------------------------------------------------------------
def tiled_offset_2d(x, y, pitch_blocks, bpb_log2):
    """Byte offset of block (x, y) in a Xenos 2D tiled surface (pitch in blocks)."""
    pitch_blocks = (pitch_blocks + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch_blocks >> 5)) << (bpb_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bpb_log2
    off = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((off & ~0x1FF) << 3) + ((y & 16) << 7) + ((off & 0x1C0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (off & 0x3F))


def endian_swap(data, mode):
    """Xenos endian modes: 0 none, 1 8in16, 2 8in32, 3 16in32."""
    if mode == 0:
        return data
    n = len(data) & ~3
    src, out = data[:n], bytearray(n)
    if mode == 1:
        out[0::2], out[1::2] = src[1::2], src[0::2]
    elif mode == 2:
        out[0::4], out[1::4], out[2::4], out[3::4] = src[3::4], src[2::4], src[1::4], src[0::4]
    elif mode == 3:
        out[0::4], out[1::4], out[2::4], out[3::4] = src[2::4], src[3::4], src[0::4], src[1::4]
    return bytes(out) + data[n:]


def half_to_float(h):
    return struct.unpack("<e", struct.pack("<H", h))[0]


def to8(v):
    """Clamp a 0..1 float to a byte."""
    if v != v:  # NaN
        return 255
    return 0 if v <= 0 else 255 if v >= 1 else int(v * 255 + 0.5)


def write_png(path, w, h, rgba):
    stride = w * 4
    raw = b"".join(b"\0" + bytes(rgba[y * stride:(y + 1) * stride]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


# --- block decoders (return list of 16 RGBA tuples, row-major) ---------------
def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def bc1_colors(blk, allow_punch):
    c0, c1 = struct.unpack_from("<HH", blk, 0)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or not allow_punch:
        p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1))
        p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))
        pal = [p0 + (255,), p1 + (255,), p2 + (255,), p3 + (255,)]
    else:
        p2 = tuple((a + b) // 2 for a, b in zip(p0, p1))
        pal = [p0 + (255,), p1 + (255,), p2 + (255,), (0, 0, 0, 0)]
    bits = struct.unpack_from("<I", blk, 4)[0]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def bc4_values(blk):
    a0, a1 = blk[0], blk[1]
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(blk[2:8], "little")
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode_block(fmt, blk):
    if fmt == 18:
        return bc1_colors(blk, True)
    if fmt == 19:  # DXT2_3: explicit 4-bit alpha + BC1 colour
        cols = bc1_colors(blk[8:16], False)
        abits = int.from_bytes(blk[0:8], "little")
        return [c[:3] + ((((abits >> (4 * i)) & 15) * 17),) for i, c in enumerate(cols)]
    if fmt == 20:  # DXT4_5: BC4 alpha + BC1 colour
        cols = bc1_colors(blk[8:16], False)
        al = bc4_values(blk[0:8])
        return [c[:3] + (a,) for c, a in zip(cols, al)]
    if fmt == 49:  # DXN: two BC4 channels
        r, g = bc4_values(blk[0:8]), bc4_values(blk[8:16])
        return [(a, b, 0, 255) for a, b in zip(r, g)]
    if fmt == 59:  # DXT5A: one BC4 channel
        return [(v, v, v, 255) for v in bc4_values(blk)]
    if fmt == 58:  # DXT3A: explicit 4-bit single channel
        bits = int.from_bytes(blk[0:8], "little")
        return [((v := ((bits >> (4 * i)) & 15) * 17), v, v, 255) for i in range(16)]
    if fmt == 60:  # CTX1: two 8:8 endpoints, 2-bit indices (normal-map XY)
        x0, y0, x1, y1 = blk[0], blk[1], blk[2], blk[3]
        pal = [(x0, y0), (x1, y1), ((2 * x0 + x1) // 3, (2 * y0 + y1) // 3),
               ((x0 + 2 * x1) // 3, (y0 + 2 * y1) // 3)]
        bits = struct.unpack_from("<I", blk, 4)[0]
        return [pal[(bits >> (2 * i)) & 3] + (0, 255) for i in range(16)]
    raise ValueError(fmt)


# --- pixel decoders ----------------------------------------------------------
def decode_pixel(fmt, px):
    """Return an RGBA byte tuple for one uncompressed texel (raw channel order)."""
    if fmt in (2, 8, 9):
        return (px[0], px[0], px[0], 255)
    if fmt in (6, 14):
        return (px[0], px[1], px[2], px[3])
    if fmt == 10:
        return (px[0], px[1], 0, 255)
    v = int.from_bytes(px, "little")
    if fmt == 4:
        return rgb565(v) + (255,)
    if fmt == 3:
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return (r << 3, g << 3, b << 3, 255 if v >> 15 else 0)
    if fmt == 5:
        r, g, b = (v >> 10) & 63, (v >> 5) & 31, v & 31
        return (r << 2, g << 3, b << 3, 255)
    if fmt == 15:
        return tuple(((v >> s) & 15) * 17 for s in (8, 4, 0, 12))
    if fmt == 7:
        return (to8((v & 1023) / 1023), to8(((v >> 10) & 1023) / 1023),
                to8(((v >> 20) & 1023) / 1023), (v >> 30) * 85)
    if fmt == 16:  # 10_11_11 (r10 g11 b11)
        return (to8((v & 1023) / 1023), to8(((v >> 10) & 2047) / 2047), to8((v >> 21) / 2047), 255)
    if fmt == 17:  # 11_11_10
        return (to8((v & 2047) / 2047), to8(((v >> 11) & 2047) / 2047), to8((v >> 22) / 1023), 255)
    if fmt in (22, 23):  # depth 24 + stencil 8
        d = (v >> 8) / 16777215.0 if fmt == 22 else 0.0
        if fmt == 23:  # 20e4 float depth
            e, m = (v >> 28) & 15, (v >> 8) & 0xFFFFF
            d = (m / (1 << 20)) * (2.0 ** (e - 15)) if e else 0.0
        g = to8(d)
        return (g, g, g, v & 255)
    if fmt == 24:
        g = (v >> 8) & 255
        return (g, g, g, 255)
    if fmt == 25:
        return (v & 0xFFFF) >> 8, (v >> 24) & 255, 0, 255
    if fmt == 26:
        c = [(v >> (16 * i)) & 0xFFFF for i in range(4)]
        return tuple(x >> 8 for x in c)
    if fmt == 30:
        g = to8(half_to_float(v))
        return (g, g, g, 255)
    if fmt == 31:
        return (to8(half_to_float(v & 0xFFFF)), to8(half_to_float(v >> 16)), 0, 255)
    if fmt == 32:
        c = [to8(half_to_float((v >> (16 * i)) & 0xFFFF)) for i in range(4)]
        return tuple(c)
    if fmt == 33:
        g = v >> 24
        return (g, g, g, 255)
    if fmt == 36:
        g = to8(struct.unpack("<f", px)[0])
        return (g, g, g, 255)
    if fmt == 37:
        a, b = struct.unpack("<ff", px)
        return (to8(a), to8(b), 0, 255)
    raise ValueError(fmt)


def apply_fetch_swizzle(rgba, fetch_dword3):
    """Apply the fetch constant's destination swizzle (dword 3 bits 1-12, 3 bits
    per output xyzw: 0-3 = source x/y/z/w, 4 = constant 0, 5 = constant 1).
    The raw stored order can differ from what shaders see, e.g. resolves with
    the red/blue swap bit are corrected by a swizzle when sampled."""
    swz = (fetch_dword3 >> 1) & 0xFFF
    sel = [(swz >> (3 * i)) & 7 for i in range(4)]
    if sel == [0, 1, 2, 3]:
        return rgba
    out = bytearray(len(rgba))
    for c, s in enumerate(sel):
        if s < 4:
            out[c::4] = rgba[s::4]
        elif s == 5:
            out[c::4] = b"\xff" * (len(rgba) // 4)
        # s == 4 (or invalid): leave 0
    return out


def decode_texture(meta, data):
    """Decode a dumped texture's base level to (w, h, rgba bytearray)."""
    fmt, w, h = meta["fmt"], meta["w"], meta["h"]
    if fmt not in FORMATS:
        raise ValueError(f"format {fmt} not supported")
    _, bw, bpb = FORMATS[fmt]
    bpb_log2 = bpb.bit_length() - 1
    data = endian_swap(data, meta["endian"])
    blocks_x, blocks_y = (w + bw - 1) // bw, (h + bw - 1) // bw
    pitch_blocks = (meta["pitch"] * 32) // bw if meta["pitch"] else blocks_x
    row_bytes = (pitch_blocks * bpb + 255) & ~255
    rgba = bytearray(w * h * 4)
    n = len(data)
    for by in range(blocks_y):
        for bx in range(blocks_x):
            off = (tiled_offset_2d(bx, by, pitch_blocks, bpb_log2) if meta["tiled"]
                   else by * row_bytes + bx * bpb)
            if off + bpb > n:
                continue
            blk = data[off:off + bpb]
            if bw == 1:
                o = (by * w + bx) * 4
                rgba[o:o + 4] = bytes(decode_pixel(fmt, blk))
            else:
                texels = decode_block(fmt, blk)
                for i, c in enumerate(texels):
                    x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                    if x < w and y < h:
                        o = (y * w + x) * 4
                        rgba[o:o + 4] = bytes(c)
    return w, h, rgba


# --- index parsing -------------------------------------------------------------
def read_index(frame_dir):
    """Returns (textures: {file: meta}, draws: [meta...], resolves: [str], header: [str])."""
    textures, draws, resolves, header = {}, [], [], []
    with open(os.path.join(frame_dir, "index.txt"), encoding="ascii", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("# resolve "):
                resolves.append(line[len("# resolve "):])
                continue
            if line.startswith("#"):
                if not line.startswith("# draw "):
                    header.append(line)
                continue
            t = line.split()
            if len(t) < 28 or t[0] != "draw":
                continue
            meta = {"draw": int(t[1]), "slot": int(t[3]), "dim": int(t[5]), "w": int(t[7]),
                    "h": int(t[8]), "pitch": int(t[10]), "tiled": int(t[11]), "fmt": int(t[13]),
                    "endian": int(t[14]), "base": int(t[16], 16), "fetch": t[17:23],
                    "vs": t[24], "ps": t[26], "file": t[27]}
            draws.append(meta)
            textures.setdefault(meta["file"], meta)
    return textures, draws, resolves, header


def tex_key(meta):
    return (meta["base"], meta["w"], meta["h"], meta["fmt"])


def fmt_name(fmt):
    return FORMATS.get(fmt, (f"fmt{fmt}",))[0]


# --- commands ------------------------------------------------------------------
def cmd_list(frame_dir):
    textures, draws, resolves, header = read_index(frame_dir)
    for line in header:
        print(line)
    print(f"{len(draws)} texture bindings, {len(textures)} distinct textures, {len(resolves)} resolves")
    for name, m in sorted(textures.items(), key=lambda kv: kv[1]["base"]):
        users = sum(1 for d in draws if d["file"] == name)
        print(f"  {m['base']:08X} {m['w']:5}x{m['h']:<5} {fmt_name(m['fmt']):22} tiled={m['tiled']} "
              f"endian={m['endian']} uses={users:3}  {name}")
    for r in resolves:
        print("  resolve", r)


def cmd_decode(frame_dir, out_dir, only, raw):
    textures, _, _, _ = read_index(frame_dir)
    out_dir = out_dir or os.path.join(frame_dir, "png")
    os.makedirs(out_dir, exist_ok=True)
    done = skipped = 0
    for name, m in sorted(textures.items()):
        if only and only not in name:
            continue
        try:
            w, h, rgba = decode_texture(m, open(os.path.join(frame_dir, name), "rb").read())
        except ValueError as e:
            print(f"skip {name}: {e}")
            skipped += 1
            continue
        if not raw:
            rgba = apply_fetch_swizzle(rgba, int(m["fetch"][3], 16))
        png = os.path.join(out_dir, os.path.splitext(name)[0] + ".png")
        write_png(png, w, h, rgba)
        print(f"{fmt_name(m['fmt']):22} {w}x{h} -> {png}")
        done += 1
    print(f"decoded {done}, skipped {skipped}")


def compare(a, b):
    """Return (identical, max_delta, psnr_db) for two RGBA buffers of equal size."""
    if a == b:
        return True, 0, float("inf")
    mx, se = 0, 0
    for x, y in zip(a, b):
        d = x - y if x > y else y - x
        if d:
            se += d * d
            if d > mx:
                mx = d
    mse = se / len(a)
    return False, mx, (10 * math.log10(255 * 255 / mse) if mse else float("inf"))


def content_hash(name):
    return os.path.splitext(name)[0].rsplit("_", 1)[-1]


def strip_addresses(resolve_line):
    """Drop guest addresses from a resolve line: allocations move between runs."""
    out, skip = [], 0
    for tok in resolve_line.split():
        if skip:
            skip -= 1
            continue
        if tok in ("base", "range"):
            skip = 1
            continue
        out.append(tok)
    return " ".join(out)


def cmd_diff(dir_a, dir_b, out_dir, tolerance):
    """Match textures across two dumps. Guest addresses differ between runs
    (allocation order), so: 1) identical content (same hash) matches at any
    address; 2) the rest is paired within each (width, height, format) group in
    order of first use (draw, slot) and compared decoded."""
    ta, da, ra, _ = read_index(dir_a)
    tb, db, rb, _ = read_index(dir_b)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    def first_use(draws):
        order = {}
        for d in draws:
            order.setdefault(d["file"], (d["draw"], d["slot"]))
        return order

    ua, ub = first_use(da), first_use(db)
    hb = {}
    for n in tb:
        hb.setdefault(content_hash(n), []).append(n)
    identical, left_a, used_b = 0, [], set()
    for n in sorted(ta, key=lambda n: ua.get(n, (0, 0))):
        cands = [x for x in hb.get(content_hash(n), []) if x not in used_b]
        if cands:
            used_b.add(cands[0])
            identical += 1
        else:
            left_a.append(n)
    left_b = [n for n in sorted(tb, key=lambda n: ub.get(n, (0, 0))) if n not in used_b]

    def group(names, table):
        g = {}
        for n in names:
            m = table[n]
            g.setdefault((m["w"], m["h"], m["fmt"]), []).append(n)
        return g

    ga, gb = group(left_a, ta), group(left_b, tb)
    close = differ = 0
    only_a, only_b = [], []
    for key in sorted(set(ga) | set(gb)):
        la, lb = ga.get(key, []), gb.get(key, [])
        for na, nb in zip(la, lb):
            ma, mb = ta[na], tb[nb]
            label = f"{key[0]}x{key[1]} {fmt_name(key[2])} (A {ma['base']:08X} draw {ua[na][0]} / B {mb['base']:08X} draw {ub[nb][0]})"
            try:
                _, _, pa = decode_texture(ma, open(os.path.join(dir_a, na), "rb").read())
                w, h, pb = decode_texture(mb, open(os.path.join(dir_b, nb), "rb").read())
            except ValueError as e:
                print(f"DIFF   {label}: raw bytes differ ({e}, not decoded)")
                differ += 1
                continue
            ident, mx, psnr = compare(pa, pb)
            if ident or mx <= tolerance:
                close += 1
                continue
            differ += 1
            print(f"DIFF   {label}: max delta {mx}, PSNR {psnr:.1f} dB")
            if out_dir:
                vis = bytearray(min(255, abs(x - y) * 8) for x, y in zip(pa, pb))
                vis[3::4] = b"\xff" * (len(vis) // 4)
                write_png(os.path.join(out_dir, f"diff_{key[0]}x{key[1]}_f{key[2]}_A{ma['base']:08X}_B{mb['base']:08X}.png"), w, h, vis)
        only_a += [ta[n] for n in la[len(lb):]]
        only_b += [tb[n] for n in lb[len(la):]]
    for m in only_a:
        print(f"ONLY A {m['w']}x{m['h']} {fmt_name(m['fmt'])} at {m['base']:08X}")
    for m in only_b:
        print(f"ONLY B {m['w']}x{m['h']} {fmt_name(m['fmt'])} at {m['base']:08X}")
    print(f"textures: {identical} identical content, {close} equal after decode (tolerance {tolerance}), "
          f"{differ} differ, {len(only_a)} only in A, {len(only_b)} only in B")
    na_, nb_ = [strip_addresses(x) for x in ra], [strip_addresses(x) for x in rb]
    if na_ == nb_:
        print(f"resolves: identical sequence ignoring addresses ({len(ra)})")
    else:
        print(f"resolves: A has {len(ra)}, B has {len(rb)}; first difference (addresses ignored):")
        for i, (x, y) in enumerate(zip(na_ + [""] * len(nb_), nb_ + [""] * len(na_))):
            if x != y:
                print(f"  #{i}\n    A: {x or '(none)'}\n    B: {y or '(none)'}")
                break


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]

    def opt(name, default=None):
        if name in args:
            i = args.index(name)
            val = args[i + 1]
            del args[i:i + 2]
            return val
        return default

    out = opt("--out")
    if cmd == "list":
        cmd_list(args[0])
    elif cmd == "decode":
        raw = "--raw" in args
        if raw:
            args.remove("--raw")
        cmd_decode(args[0], out, opt("--only"), raw)
    elif cmd == "diff":
        tol = int(opt("--tolerance", "0"))
        cmd_diff(args[0], args[1], out, tol)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
