"""MSVC RTTI in fm3.bin (session 33): class name -> type descriptor -> complete object locators -> vtables.

  python -I tools/analysis/rtti.py NAME [NAME...]      e.g. HumanDriver (substring of the decorated name)

Xbox 360 MSVC layout (big-endian): type descriptor = { vfptr, spare, ".?AV<name>@@" }; complete object locator =
{ signature 0, offset, cdOffset, type descriptor, class hierarchy }; a vtable's slot -1 points at its locator.
Prints each vtable with its first slots; then `xref.py refs 0x<vtable>` finds the constructors that store it.
"""
import os
import struct
import sys

BASE = 0x82000000
HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, "..", "..", "out", "fm3.bin")


def main():
    data = open(IMAGE, "rb").read()
    words = {}
    # Index every aligned big-endian word that looks like an image address.
    for off in range(0, len(data) - 3, 4):
        v = struct.unpack_from(">I", data, off)[0]
        if BASE <= v < BASE + len(data):
            words.setdefault(v, []).append(off)
    for want in sys.argv[1:]:
        start = 0
        while True:
            i = data.find(b".?AV", start)
            if i < 0:
                break
            end = data.find(b"\0", i)
            name = data[i:end].decode("latin-1")
            start = end
            if want not in name:
                continue
            td = BASE + i - 8
            print(f"{name}: type descriptor {td:08X}")
            for ref in words.get(td, []):
                col = BASE + ref - 12  # pTypeDescriptor is the locator's 4th word
                sig, offset, cd = struct.unpack_from(">III", data, ref - 12)
                if sig != 0:
                    continue
                for vref in words.get(col, []):
                    vt = BASE + vref + 4
                    slots = [struct.unpack_from(">I", data, vref + 4 + k * 4)[0] for k in range(10)]
                    print(f"  locator {col:08X} (offset {offset}) -> vtable {vt:08X}: " + " ".join(f"{s:08X}" for s in slots))


if __name__ == "__main__":
    main()
