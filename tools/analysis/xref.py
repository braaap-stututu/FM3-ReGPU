"""Finds which recompiled functions use a string or an address (LOD research, session 19).

  python -I tools/analysis/xref.py strings TEXT [TEXT...]     where TEXT is in the image (out/fm3.bin, base 0x82000000)
  python -I tools/analysis/xref.py refs TEXT|0xADDR [...]     functions in out/ppc that form that address
                                                              (lis rX,hi then addi/ori/load/store at lo(rX))
  python -I tools/analysis/xref.py floats 0xADDR [COUNT]      float constants at an address (.rdata tables)
  python -I tools/analysis/xref.py func sub_XXXXXXXX          the strings and constants a function forms

Address forming is tracked per function, per register, from the last `lis` (XenonRecomp keeps the PowerPC source as
`// lis r11,-32256` comments). A string usually reaches code as one address, so `refs` finds its users directly;
`func` then lists what else that function touches (the neighbouring field names of a settings parser, say).
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, "out", "fm3.bin")
PPC = os.path.join(ROOT, "out", "ppc")
BASE = 0x82000000

FUNC = re.compile(r"^PPC_FUNC_IMPL\(__imp__(sub_[0-9A-F]{8})\)")
LIS = re.compile(r"^\s*// lis r(\d+),(-?\d+)")
USE = re.compile(r"^\s*// (addi|addic|ori|lwz|lhz|lbz|lfs|lfd|stw|sth|stb|stfs|stfd|ld|std|lwzu|lha) r?f?(\d+),(?:r(\d+),)?(-?\d+)(?:\(r(\d+)\))?")


def image():
    return open(BIN, "rb").read()


def find_strings(data, text):
    out = []
    needle = text.encode()
    i = data.find(needle)
    while i >= 0:
        # whole string: a zero before it (or another string's end) and a zero after
        end = data.find(b"\0", i)
        start = data.rfind(b"\0", 0, i) + 1
        out.append((BASE + i, data[start:end].decode("latin-1")))
        i = data.find(needle, i + 1)
    return out


def scan(targets, span=0):
    """Yields (function, file, line, address) for every address formed within [t, t + span] of a target."""
    for name in sorted(os.listdir(PPC)):
        if not name.startswith("ppc_recomp"):
            continue
        func = "?"
        hi = {}
        with open(os.path.join(PPC, name), encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                m = FUNC.match(line)
                if m:
                    func = m.group(1)
                    hi = {}
                    continue
                m = LIS.match(line)
                if m:
                    hi[int(m.group(1))] = (int(m.group(2)) << 16) & 0xFFFFFFFF
                    continue
                m = USE.match(line)
                if not m:
                    continue
                op = m.group(1)
                if op in ("addi", "addic", "ori"):
                    src, imm = m.group(3), int(m.group(4))
                else:
                    src, imm = m.group(5), int(m.group(4))
                if src is None or int(src) not in hi:
                    continue
                addr = (hi[int(src)] + (imm if op != "ori" else imm & 0xFFFF)) & 0xFFFFFFFF
                for t in targets:
                    if t <= addr <= t + span:
                        yield func, name, n, addr, op


def resolve(arg, data):
    """The addresses code forms for TEXT: the start of each string that contains it."""
    if arg.lower().startswith("0x"):
        return [int(arg, 16)]
    out = []
    for a, _ in find_strings(data, arg):
        i = a - BASE
        out.append(BASE + data.rfind(bytes(1), 0, i) + 1)
    return sorted(set(out))


def string_at(data, addr):
    i = addr - BASE
    if not 0 <= i < len(data):
        return None
    end = data.find(b"\0", i)
    s = data[i:end]
    if 3 <= len(s) <= 120 and all(32 <= c < 127 for c in s):
        return s.decode()
    return None


def function_body(want):
    for name in sorted(os.listdir(PPC)):
        if not name.startswith("ppc_recomp"):
            continue
        text = open(os.path.join(PPC, name), encoding="utf-8", errors="replace").read()
        if f"PPC_FUNC_IMPL(__imp__{want})" in text:
            return text.split(f"PPC_FUNC_IMPL(__imp__{want})", 1)[1].split("PPC_FUNC_IMPL(", 1)[0]
    return None


# Turn 10's developer command line (NOTES.md session 17): the handler, the options object's getter.
OPTION_HANDLER, OPTION_GETTER = "sub_8225EF78", "sub_82210D10"
OPTION_OBJECT, OPTION_POINTER = 0x82C21C60, 0x82C21C54  # the object, and the pointer to it the getter keeps
ADDI = re.compile(r"^\s*// addi r(\d+),r(\d+),(-?\d+)")
MR = re.compile(r"^\s*// mr r(\d+),r(\d+)")
BL = re.compile(r"^\s*// bl 0x([0-9a-f]{8})")
LOAD = re.compile(r"^\s*// (lbz|lhz|lwz|lfs|lfd|ld) r?f?(\d+),(-?\d+)\(r(\d+)\)")


def parse_options(data):
    """name -> (offset in the options object, parser function), from the handler's calls (r4 name, r5 field)."""
    body = function_body(OPTION_HANDLER)
    hi, fld, opts = {}, {}, {}
    for line in body.splitlines():
        m = LIS.match(line)
        if m:
            hi[int(m.group(1))] = (int(m.group(2)) << 16) & 0xFFFFFFFF
            fld.pop(int(m.group(1)), None)
            continue
        m = ADDI.match(line)
        if m:
            d, s, imm = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if s == 31:
                fld[d] = ("off", imm)
            elif s in hi:
                fld[d] = ("str", (hi[s] + imm) & 0xFFFFFFFF)
            continue
        m = MR.match(line)
        if m and int(m.group(2)) in fld:
            fld[int(m.group(1))] = fld[int(m.group(2))]
            continue
        m = BL.match(line)
        if m:
            name, off = fld.get(4), fld.get(5)
            if name and off and name[0] == "str" and off[0] == "off":
                s = string_at(data, name[1])
                if s:
                    opts[s] = (off[1], "sub_" + m.group(1).upper())
    return opts


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd, args = sys.argv[1], sys.argv[2:]
    data = image()
    if cmd == "options":
        opts = parse_options(data)
        for name, (off, fn) in sorted(opts.items(), key=lambda kv: kv[1][0]):
            if not args or any(a.lower() in name.lower() for a in args):
                print(f"+{off:<5d} {fn}  {name}")
        return
    if cmd == "readers":
        # Functions that call the options getter and load a field of what it returns (r3) before r3 changes.
        opts = parse_options(data)
        by_off = {}
        for name, (off, _) in opts.items():
            by_off.setdefault(off, []).append(name)
        want = {o for n, (o, _) in opts.items() if not args or any(a.lower() in n.lower() for a in args)}
        hits = {}
        for fname in sorted(os.listdir(PPC)):
            if not fname.startswith("ppc_recomp"):
                continue
            func = "?"
            live = set()  # registers holding the options object (the getter's result, or the pointer it keeps)
            hi = {}
            with open(os.path.join(PPC, fname), encoding="utf-8", errors="replace") as f:
                for line in f:
                    m = FUNC.match(line)
                    if m:
                        func, live, hi = m.group(1), set(), {}
                        continue
                    m = LIS.match(line)
                    if m:
                        hi[int(m.group(1))] = (int(m.group(2)) << 16) & 0xFFFFFFFF
                        live.discard(int(m.group(1)))
                        continue
                    m = LOAD.match(line)
                    if m and m.group(1) == "lwz" and int(m.group(4)) in hi and (hi[int(m.group(4))] + int(m.group(3))) & 0xFFFFFFFF == OPTION_POINTER:
                        live.add(int(m.group(2)))
                        continue
                    m = ADDI.match(line)
                    if m and int(m.group(2)) in hi and (hi[int(m.group(2))] + int(m.group(3))) & 0xFFFFFFFF == OPTION_OBJECT:
                        live.add(int(m.group(1)))
                        continue
                    m = BL.match(line)
                    if m:
                        live = {3} if "sub_" + m.group(1).upper() == OPTION_GETTER else set()
                        continue
                    m = MR.match(line)
                    if m:
                        d, s = int(m.group(1)), int(m.group(2))
                        live.discard(d)
                        if s in live:
                            live.add(d)
                        continue
                    m = LOAD.match(line)
                    if m and int(m.group(4)) in live and int(m.group(3)) in want:
                        hits.setdefault(int(m.group(3)), set()).add((func, m.group(1)))
                        continue
                    m = re.match(r"^\s*// \w+\.? r(\d+),", line)
                    if m and not LOAD.match(line):
                        live.discard(int(m.group(1)))
        for off in sorted(hits):
            print(f"+{off} {'/'.join(by_off.get(off, ['?']))}: " + ", ".join(f"{fn} ({op})" for fn, op in sorted(hits[off])))
        return
    if len(args) < 1:
        print(__doc__)
        return
    if cmd == "strings":
        for t in args:
            for a, s in find_strings(data, t):
                print(f"{a:08X}  {s}")
    elif cmd == "refs":
        targets = []
        for a in args:
            targets += resolve(a, data)
        for func, name, n, addr, op in scan(targets):
            print(f"{func}  {addr:08X} {op:5s} {string_at(data, addr) or ''}  ({name}:{n})")
    elif cmd == "floats":
        addr = int(args[0], 16)
        count = int(args[1]) if len(args) > 1 else 16
        i = addr - BASE
        for k in range(count):
            (v,) = struct.unpack(">f", data[i + 4 * k:i + 4 * k + 4])
            print(f"{addr + 4 * k:08X}  {v:g}")
    elif cmd == "func":
        want = args[0]
        for name in sorted(os.listdir(PPC)):
            if not name.startswith("ppc_recomp"):
                continue
            text = open(os.path.join(PPC, name), encoding="utf-8", errors="replace").read()
            if f"__imp__{want})" not in text:
                continue
            body = text.split(f"PPC_FUNC_IMPL(__imp__{want})", 1)[1].split("PPC_FUNC_IMPL(", 1)[0]
            hi = {}
            seen = set()
            for line in body.splitlines():
                m = LIS.match(line)
                if m:
                    hi[int(m.group(1))] = (int(m.group(2)) << 16) & 0xFFFFFFFF
                    continue
                m = USE.match(line)
                if not m:
                    continue
                op = m.group(1)
                src, imm = (m.group(3), int(m.group(4))) if op in ("addi", "addic", "ori") else (m.group(5), int(m.group(4)))
                if src is None or int(src) not in hi:
                    continue
                addr = (hi[int(src)] + imm) & 0xFFFFFFFF
                if addr in seen:
                    continue
                seen.add(addr)
                s = string_at(data, addr)
                if s is not None:
                    print(f"{addr:08X} {op:5s} \"{s}\"")
                elif op in ("lfs",) and 0 <= addr - BASE < len(data) - 4:
                    (v,) = struct.unpack(">f", data[addr - BASE:addr - BASE + 4])
                    print(f"{addr:08X} {op:5s} float {v:g}")
                else:
                    print(f"{addr:08X} {op:5s}")
            calls = sorted(set(re.findall(r"\b(sub_[0-9A-F]{8})\(ctx, base\)", body)))
            print("calls:", " ".join(calls))
            return
        print("not found:", want)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
