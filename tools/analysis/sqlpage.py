"""Decode one SQLite table-leaf page (FM3's gamedb.slt, 1 KB pages) from the file and, optionally, from a
hex dump of the same page taken from guest memory (Fm3 "peek <address> 1024" | ConvertFrom-Json).hex).

Session 15 (crash C1): the in-memory database is one copy of gamedb.slt; find a record with
Fm3 "find <text>" (e.g. a driver's Name key) and peek its page.

usage: python -I tools/analysis/sqlpage.py <gamedb.slt> <page file offset, hex> [<hex dump file>] [rowid ...]
       (no rowids: every cell)
example: python -I tools/analysis/sqlpage.py "<game>/Media/DB/gamedb.slt" 4400 out/page.hex 124 125
"""
import sys


def varint(b, i):
    v = 0
    for k in range(9):
        c = b[i + k]
        if k == 8:
            return (v << 8) | c, i + 9
        v = (v << 7) | (c & 0x7F)
        if c < 0x80:
            return v, i + k + 1


def record(b, off):
    _, i = varint(b, off)  # payload length
    rowid, i = varint(b, i)
    header, j = varint(b, i)
    end = i + header
    types = []
    while j < end:
        st, j = varint(b, j)
        types.append(st)
    values, p = [], end
    for st in types:
        if st == 0:
            values.append(None)
        elif 1 <= st <= 6:
            n = {1: 1, 2: 2, 3: 3, 4: 4, 5: 6, 6: 8}[st]
            values.append(int.from_bytes(b[p:p + n], "big", signed=True))
            p += n
        elif st in (8, 9):
            values.append(st - 8)
        elif st >= 13 and st % 2:
            n = (st - 13) // 2
            values.append(b[p:p + n].decode("latin1"))
            p += n
        elif st >= 12:
            n = (st - 12) // 2
            values.append(b[p:p + n])
            p += n
        else:
            values.append("?%d" % st)
    return rowid, values, p


def page(b, label, rowids):
    kind, ncell, content = b[0], int.from_bytes(b[3:5], "big"), int.from_bytes(b[5:7], "big")
    print("%s: type %d, %d cells, content from %d, %d fragmented bytes" % (label, kind, ncell, content, b[7]))
    if kind != 13:
        print("  not a table leaf page")
        return
    for c in range(ncell):
        off = int.from_bytes(b[8 + 2 * c:10 + 2 * c], "big")
        try:
            rowid, values, end = record(b, off)
        except Exception as e:  # a damaged cell
            print("  cell %d at %#x: cannot decode (%s)" % (c, off, e))
            continue
        if not rowids or rowid in rowids:
            print("  cell %d at %#x-%#x rowid %d: %s" % (c, off, end, rowid, values))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    at = int(sys.argv[2], 16)
    rest = sys.argv[3:]
    dump = rest.pop(0) if rest and not rest[0].isdigit() else None
    rowids = {int(r) for r in rest}
    disk = open(sys.argv[1], "rb").read()[at:at + 1024]
    page(disk, "file", rowids)
    if dump:
        mem = bytes.fromhex(open(dump).read().strip())[:1024]
        page(mem, "memory", rowids)
        diff = [i for i in range(min(len(disk), len(mem))) if disk[i] != mem[i]]
        print("%d bytes differ from the file%s" % (len(diff), (": first at %#x" % diff[0]) if diff else ""))


if __name__ == "__main__":
    main()
