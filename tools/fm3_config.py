#!/usr/bin/env python3
"""Image-specific values for config/fm3.toml, found in an xexdump image.

  py -3 -I tools/fm3_config.py out/fm3.bin out/fm3.sections [--write config/fm3.toml]

Input: xexdump's image (<out.bin>) and its stdout (base line + section list).
Prints:
  - the register save/restore helpers (savegprlr_14 & co): each must match exactly once;
  - the exception-handler words: .pdata entries with the exception flag are preceded by 8 bytes
    of handler data (first word = handler address), which XenonRecomp must skip
    (invalid_instructions);
  - setjmp/longjmp candidates: runs of std/ld r14..r31 at consecutive 8-byte offsets of a base
    register (fiber switches and context captures match too: confirm with build/tools/ppcdis.exe);
  - .pdata statistics.
--write replaces the helper values and the generated exception-data block in the config.
setjmp/longjmp and function boundaries are never written: they are reviewed by hand.

Own code (FM3 project). Same approach as the FM4 port's config scan: helper byte patterns are
the standard Xbox 360 CRT prologue/epilogue instructions.
"""
import bisect
import collections
import re
import struct
import sys

# First instruction(s) of each helper, big endian.
HELPERS = {
    "restgprlr_14_address": "e9c1ff68",            # ld r14,-0x98(r1)
    "savegprlr_14_address": "f9c1ff68",            # std r14,-0x98(r1)
    "restfpr_14_address": "c9ccff70",              # lfd f14,-0x90(r12)
    "savefpr_14_address": "d9ccff70",              # stfd f14,-0x90(r12)
    "restvmx_14_address": "3960fee07dcb60ce",      # li r11,-0x120 ; lvx v14,r11,r12
    "savevmx_14_address": "3960fee07dcb61ce",      # li r11,-0x120 ; stvx v14,r11,r12
    "restvmx_64_address": "3960fc00100b60cb",      # li r11,-0x400 ; lvx128 v64,r11,r12
    "savevmx_64_address": "3960fc00100b61cb",      # li r11,-0x400 ; stvx128 v64,r11,r12
}
EH_BEGIN = "    # BEGIN fm3_config.py exception data"
EH_END = "    # END fm3_config.py exception data"


def parse_sections(path):
    base, sections = None, {}
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) >= 2 and p[0] == "base":
            base = int(p[1], 16)
        elif len(p) == 4 and p[3] in ("code", "data"):
            sections[p[0]] = (int(p[1], 16), int(p[2], 16), p[3] == "code")
    if base is None:
        sys.exit(f"{path}: no 'base' line (xexdump stdout expected)")
    return base, sections


def in_code(addr, code):
    return any(s <= addr < s + n for s, n in code)


def find_helpers(img, base, code):
    out = {}
    for name, hx in HELPERS.items():
        pat = bytes.fromhex(hx)
        hits, i = [], img.find(pat)
        while i != -1:
            if i % 4 == 0 and in_code(base + i, code):
                hits.append(base + i)
            i = img.find(pat, i + 1)
        out[name] = hits
    return out


def read_pdata(img, base, sections):
    start, size, _ = sections[".pdata"]
    funcs = []
    for off in range(start - base, start - base + size, 8):
        begin, packed = struct.unpack_from(">II", img, off)
        if begin:
            # PowerPC .pdata: prolog length (8 bits), function length in words (22 bits),
            # 32-bit flag, exception flag (top bit).
            funcs.append((begin, ((packed >> 8) & 0x3FFFFF) * 4, bool(packed >> 31)))
    funcs.sort()
    return funcs


def exception_words(img, base, funcs, code):
    handlers, other = collections.Counter(), collections.Counter()
    for begin, _, has_eh in funcs:
        if has_eh:
            word = struct.unpack_from(">I", img, begin - 8 - base)[0]
            (handlers if in_code(word, code) else other)[word] += 1
    return handlers, other


def gpr_runs(img, base, code, opcode):
    """Starts of 18-instruction runs `std/ld rN, d(rB)` for N = 14..31, same base register,
    offsets ascending by 8 (other instructions may sit in between)."""
    hits = []
    for s, n in code:
        words = struct.unpack_from(f">{n // 4}I", img, s - base)
        i = 0
        while i < len(words):
            w = words[i]
            if (w >> 26) == opcode and (w >> 21) & 31 == 14 and w & 3 == 0 and (w >> 16) & 31 != 1:
                rb, want, last, j = (w >> 16) & 31, 15, w & 0xFFFC, i + 1
                while j < len(words) and j - i < 48 and want <= 31:
                    x = words[j]
                    if (x >> 26) == opcode and (x >> 21) & 31 == want and (x >> 16) & 31 == rb and x & 3 == 0:
                        if (x & 0xFFFC) != last + 8:
                            break
                        last, want = x & 0xFFFC, want + 1
                    j += 1
                if want == 32:
                    hits.append((s + i * 4, rb))
                    i = j
                    continue
            i += 1
    return hits


def owner(funcs, starts, addr):
    k = bisect.bisect_right(starts, addr) - 1
    if k >= 0 and funcs[k][0] <= addr < funcs[k][0] + funcs[k][1]:
        return f"in .pdata function {funcs[k][0]:08X}+{funcs[k][1]:X}"
    return "outside any .pdata function"


def write_config(path, helpers, eh):
    text = open(path, encoding="utf-8").read()
    for name, hits in helpers.items():
        value = f"0x{hits[0]:08X}" if len(hits) == 1 else "0x0"
        text, n = re.subn(rf"(?m)^{name}\s*=\s*0x[0-9A-Fa-f]+", f"{name} = {value}", text)
        if n != 1:
            sys.exit(f"{path}: expected one '{name} = ...' line, found {n}")
    begin, end = text.find(EH_BEGIN), text.find(EH_END)
    if begin < 0 or end < begin:
        sys.exit(f"{path}: missing the '{EH_BEGIN.strip()}' ... '{EH_END.strip()}' block")
    lines = [f"    {{ data = 0x{w:08X}, size = 8 }}, # handler of {c} functions\n" for w, c in eh.most_common()]
    text = text[:begin] + EH_BEGIN + "\n" + "".join(lines) + text[end:]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"updated {path}")


def main():
    args = sys.argv[1:]
    write = None
    if "--write" in args:
        k = args.index("--write")
        write = args[k + 1]
        del args[k:k + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    img = open(args[0], "rb").read()
    base, sections = parse_sections(args[1])
    code = [(s, n) for s, n, c in sections.values() if c]

    helpers = find_helpers(img, base, code)
    for name, hits in helpers.items():
        print(f"{name} = {', '.join(f'0x{h:08X}' for h in hits) or 'NOT FOUND'}")
    ambiguous = [n for n, h in helpers.items() if len(h) > 1]

    funcs = read_pdata(img, base, sections)
    starts = [f[0] for f in funcs]
    eh, other = exception_words(img, base, funcs, code)
    covered = sum(f[1] for f in funcs)
    print(f"\n{len(funcs)} .pdata functions covering 0x{covered:X} bytes; {sum(eh.values())} with exception data")
    for w, c in eh.most_common():
        print(f"  handler word 0x{w:08X}: {c} functions")
    for w, c in other.most_common():
        print(f"  ignored 0x{w:08X} before {c} function(s): not a code address")
    for name, (s, n, c) in sections.items():
        if c:
            k = sum(1 for f in funcs if s <= f[0] < s + n)
            print(f"  code section {name:<8} {s:08X}+{n:X}: {k} .pdata functions")

    for label, opcode in (("setjmp-like (std r14..r31)", 62), ("longjmp-like (ld r14..r31)", 58)):
        hits = gpr_runs(img, base, code, opcode)
        print(f"\n{label} candidates: {len(hits)}")
        for h, rb in hits:
            print(f"  {h:08X} base r{rb} {owner(funcs, starts, h)}")

    if write:
        if ambiguous:
            sys.exit(f"not writing: more than one match for {', '.join(ambiguous)}")
        write_config(write, helpers, eh)


if __name__ == "__main__":
    main()
