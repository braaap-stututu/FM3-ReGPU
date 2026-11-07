#!/usr/bin/env python3
"""Checks a XenonRecomp output (out/ppc) against the image, after each recompile.

  py -3 -I tools/fm3_check.py out/fm3.bin out/fm3.sections out/ppc [--refs FILE ...] [--toml]

Reports:
  1. leftovers in the generated code: `// ERROR` branches and `__builtin_debugtrap` (unsupported
     instructions), with the function they are in;
  2. functions whose last instruction can fall through but are not marked "falls through into"
     (they run off their end: a no-return call, or a wrong boundary);
  3. pointer-only targets that are not function starts in ppc_func_mapping.cpp, from:
       - our own scan: 4-aligned words in data sections pointing into code, and lis+addi/ori
         pairs building a code address, kept only where the previous instruction ends a block
         (blr / bctr / b / padding) - the same heuristic both ReXGlue ports use;
       - --refs files: TOML entry keys (0x82XXXXXX = {...} / address = 0x82XXXXXX) (hl360's fptr_functions.toml,
         forza3-recomp's manifest), each address taken as a claimed function start.
     For each, the size up to the first unconditional blr/bctr/b (estimate, max 0x400) and the
     function it currently lies inside. --toml prints config lines for the new ones.
Own code (FM3 project).
"""
import bisect
import glob
import re
import struct
import sys


def parse_sections(path):
    base, sections = None, {}
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) >= 2 and p[0] == "base":
            base = int(p[1], 16)
        elif len(p) == 4 and p[3] in ("code", "data"):
            sections[p[0]] = (int(p[1], 16), int(p[2], 16), p[3] == "code")
    return base, sections


def word(img, base, a):
    return struct.unpack_from(">I", img, a - base)[0]


def ends_block(w):
    op = w >> 26
    if w == 0x4E800020 or w == 0x4E800420:      # blr, bctr
        return True
    if op == 18 and (w & 1) == 0:               # b (not bl)
        return True
    return w == 0                               # padding


def estimate_size(img, base, a, code_end, limit=0x400):
    """Up to and including the first blr / bctr / b; branches forward past it extend it."""
    end_target = a
    for off in range(0, limit, 4):
        x = a + off
        if x >= code_end:
            return off
        w = word(img, base, x)
        if w == 0:
            return off
        op = w >> 26
        if op == 16:                            # bc: forward targets keep the function open
            d = w & 0xFFFC
            if d & 0x8000:
                d -= 0x10000
            end_target = max(end_target, x + d)
        if (w == 0x4E800020 or w == 0x4E800420 or (op == 18 and (w & 1) == 0)) and x >= end_target:
            return off + 4
    return None


def main():
    args = sys.argv[1:]
    toml = "--toml" in args
    if toml:
        args.remove("--toml")
    refs = []
    if "--refs" in args:
        k = args.index("--refs")
        refs = args[k + 1:]
        args = args[:k]
    img_path, sec_path, ppc = args
    img = open(img_path, "rb").read()
    base, sections = parse_sections(sec_path)
    code = sorted((s, s + n) for s, n, c in sections.values() if c)
    data = [(s, s + n) for name, (s, n, c) in sections.items() if not c and name not in (".pdata", ".reloc")]

    def code_section(a):
        for s, e in code:
            if s <= a < e:
                return (s, e)
        return None

    starts = sorted(int(m, 16) for m in re.findall(r"\{ 0x([0-9A-Fa-f]+),", open(f"{ppc}/ppc_func_mapping.cpp").read()))
    start_set = set(starts)

    def containing(a):
        k = bisect.bisect_right(starts, a) - 1
        return starts[k] if k >= 0 else None

    # 1 + 2: generated code
    errors, traps, runoff, falls = [], [], [], 0
    end_ops = {"b", "blr", "bctr", "rfi", "trap", "tw", "twi"}
    for path in sorted(glob.glob(f"{ppc}/ppc_recomp.*.cpp")):
        fn, last, handled = None, None, False
        for line in open(path, errors="replace"):
            m = re.match(r"PPC_FUNC_IMPL\(__imp__(?:\w+_)?sub_([0-9A-F]+)\)", line)
            if m:
                fn, last, handled = m.group(1), None, False
                continue
            if "// ERROR" in line:
                errors.append((fn, line.split()[-1]))
            if "__builtin_debugtrap" in line:
                traps.append((fn, last))
            if line.startswith("\t// falls through into "):
                handled = True
                falls += 1
                continue
            m = re.match(r"\t// ([a-z.+-]+)", line)
            if m:
                last = m.group(1).rstrip("+-")
                continue
            if line.startswith("}") and fn:
                if last and last not in end_ops and not handled:
                    runoff.append((fn, last))
                fn = None
    print(f"{len(starts)} functions; {len(errors)} unresolved branches; {len(traps)} traps; "
          f"{falls} fall-throughs into the next function; {len(runoff)} run off their end")
    for fn, t in errors:
        print(f"  ERROR in sub_{fn}: branch to {t}")
    for fn, last in traps:
        print(f"  trap in sub_{fn} (after {last})")
    for fn, last in runoff:
        print(f"  runs off its end: sub_{fn} (last: {last})")

    # 3: pointer-only targets
    cands = {}
    for s, e in data:
        for a in range(s, e - 3, 4):
            w = word(img, base, a)
            if w & 3 == 0 and code_section(w) and ends_block(word(img, base, w - 4)):
                cands.setdefault(w, set()).add(f"data {a:08X}")
    for s, e in code:
        for a in range(s, e - 4, 4):
            w = word(img, base, a)
            if w >> 26 != 15 or (w >> 16) & 31 != 0:   # lis rD, imm
                continue
            rd, hi = (w >> 21) & 31, (w & 0xFFFF) << 16
            for b in range(a + 4, min(a + 36, e), 4):
                x = word(img, base, b)
                op, rs = x >> 26, (x >> 16) & 31
                if op in (14, 24) and rs == rd:        # addi / ori from rd
                    imm = x & 0xFFFF
                    t = (hi + (imm - 0x10000 if op == 14 and imm & 0x8000 else imm)) & 0xFFFFFFFF
                    # A table address (the next few instructions index it with lwzx/lbzx/lhzx) is a
                    # jump table, not a function pointer.
                    table = any(((word(img, base, c) >> 26) == 31 and ((word(img, base, c) >> 1) & 0x3FF) in (23, 87, 279)
                                 and ((word(img, base, c) >> 16) & 31 == rd or (word(img, base, c) >> 11) & 31 == rd))
                                for c in range(b + 4, min(b + 16, e), 4))
                    if t & 3 == 0 and code_section(t) and t != a and not table and ends_block(word(img, base, t - 4)):
                        cands.setdefault(t, set()).add(f"code {b:08X}")
                    break
                if (x >> 21) & 31 == rd and op not in (36, 37, 38, 44, 45, 47, 62):  # rd overwritten
                    break
    for r in refs:
        # Only entry keys ("0x82XXXXXX = {...}" or "address = 0x82XXXXXX"), not addresses in comments.
        text = open(r, encoding="utf-8", errors="replace").read()
        for m in re.findall(r"(?m)^\s*(?:0x|\{\s*address\s*=\s*0x)(82[0-9A-Fa-f]{6})\b", text):
            t = int(m, 16)
            if code_section(t):
                cands.setdefault(t, set()).add(f"ref {r.replace(chr(92), '/').split('/')[-1]}")
    # Internal branch labels of the generated code (loc_XXXXXXXX:) found only by our own scan are
    # jump-table cases or other in-function targets; the reference ports' entries are kept.
    labels = set()
    for path in glob.glob(f"{ppc}/ppc_recomp.*.cpp"):
        labels.update(int(m, 16) for m in re.findall(r"(?m)^loc_([0-9A-F]+):", open(path, errors="replace").read()))
    def keep(t):
        src = cands[t]
        if any(x.startswith("ref ") for x in src):
            return True
        if t in labels:
            return False
        # An inline table (its first word is a code address) is data, not code.
        return not code_section(word(img, base, t))
    missing = sorted(t for t in cands if t not in start_set and keep(t))
    print(f"\npointer targets: {len(cands)} candidates, {len(cands) - len(missing)} already function starts, {len(missing)} not")
    for t in missing:
        sec = code_section(t)
        size = estimate_size(img, base, t, sec[1])
        inside = containing(t)
        src = sorted(cands[t])
        line = (f"    {{ address = 0x{t:08X}, size = 0x{size or 0:X} }}, # inside sub_{inside:08X}; "
                f"{', '.join(src[:3])}{' +' + str(len(src) - 3) if len(src) > 3 else ''}")
        print(line if toml else line.strip())


if __name__ == "__main__":
    main()
