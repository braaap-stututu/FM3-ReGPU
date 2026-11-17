"""Dump a tiled 32-bit texture from the running game's memory (debug server peeks) to a PNG.

    python -I tools/analysis/dumptex.py <hex address> <width> <height> <out.png> [--raw file.bin]
    python -I tools/analysis/dumptex.py --from-raw file.bin <width> <height> <out.png>

Reads width*height*4 bytes through ReGPU's debug server (`peek`, 4096 bytes a call, port 7341), untiles them
(Xenos 2D tiling, XGAddress2DTiledOffset) and writes an RGBA PNG (k_8_8_8_8 words big-endian: A R G B... the
channel order is left as stored, byte 1-3 as RGB, byte 0 as alpha). Session 41: livery atlases before/after a fix.
"""
import json
import socket
import struct
import sys
import zlib


def tiled_index(x, y, aligned_width, log_bpp=2):
    macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return (((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp


def peek_all(address, size):
    s = socket.create_connection(("127.0.0.1", 7341))
    f = s.makefile("rwb")
    out = bytearray()
    for a in range(address, address + size, 4096):
        f.write(b"peek %08X 4096\n" % a)
        f.flush()
        reply = json.loads(f.readline())
        out += bytes.fromhex(reply["hex"])
    s.close()
    return bytes(out)


def write_png(path, width, height, rgba):
    raw = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    args = sys.argv[1:]
    raw_out = None
    if "--raw" in args:
        i = args.index("--raw")
        raw_out = args[i + 1]
        del args[i:i + 2]
    if args[0] == "--from-raw":
        data = open(args[1], "rb").read()
        width, height, out = int(args[2]), int(args[3]), args[4]
    else:
        address, width, height, out = int(args[0], 16), int(args[1]), int(args[2]), args[3]
        data = peek_all(address, width * height * 4)
        if raw_out:
            open(raw_out, "wb").write(data)
    aligned = (width + 31) & ~31
    rgba = bytearray(width * height * 4)
    for y in range(height):
        row = y * width * 4
        for x in range(width):
            o = tiled_index(x, y, aligned) * 4
            p = row + x * 4
            # Stored big-endian 32-bit: byte 0 = A (or the top channel), 1-3 = the colour
            rgba[p] = data[o + 1]
            rgba[p + 1] = data[o + 2]
            rgba[p + 2] = data[o + 3]
            rgba[p + 3] = 255
    write_png(out, width, height, bytes(rgba))
    print(out)


if __name__ == "__main__":
    main()
