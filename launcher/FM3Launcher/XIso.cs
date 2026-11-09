using System.Buffers.Binary;
using System.Text;

namespace FM3Launcher;

/// <summary>
/// An Xbox 360 game disc image (XDVDFS, the .iso from a disc dump), read in place. Ported from the FM4 fork's
/// launcher (XIso.cs). The volume descriptor sits 32 sectors (2048 bytes each) into the game partition, which
/// starts at 0 in a bare image and at 0x0FD90000, 0x02080000 or 0x18300000 in a full-disc one (XGD2, XGD3, XGD1).
/// A directory is a binary tree of entries { left u16, right u16 (both in 4-byte units), sector u32, size u32,
/// attributes u8, name length u8, name }.
/// </summary>
public static class XIso
{
    const int Sector = 2048;
    static readonly long[] Bases = [0, 0x0FD90000, 0x02080000, 0x18300000];
    static readonly byte[] Magic = Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA");

    public sealed record File(string Path, long Offset, uint Size);

    static bool FindBase(Stream f, out long baseOffset, out (uint Sector, uint Size) root)
    {
        var buf = new byte[28];
        foreach (var b in Bases)
        {
            var at = b + 32L * Sector;
            if (at + buf.Length > f.Length) continue;
            f.Seek(at, SeekOrigin.Begin);
            f.ReadExactly(buf);
            if (!buf.AsSpan(0, Magic.Length).SequenceEqual(Magic)) continue;
            baseOffset = b;
            root = (BinaryPrimitives.ReadUInt32LittleEndian(buf.AsSpan(20)), BinaryPrimitives.ReadUInt32LittleEndian(buf.AsSpan(24)));
            return true;
        }
        baseOffset = 0; root = default;
        return false;
    }

    /// <summary>Every file of the image (paths with '\'), with its byte offset in the image.</summary>
    public static List<File> Files(Stream f, string name)
    {
        if (!FindBase(f, out var b, out var root)) throw new InvalidDataException($"{name} isn't an Xbox 360 disc image.");
        var files = new List<File>();
        Walk(f, b, root.Sector, root.Size, "", files, 0);
        return files;
    }

    static void Walk(Stream f, long b, uint sector, uint size, string prefix, List<File> files, int depth)
    {
        if (size == 0 || size > 64 << 20 || depth > 32) return;
        var dir = new byte[size];
        f.Seek(b + (long)sector * Sector, SeekOrigin.Begin);
        f.ReadExactly(dir);
        var seen = new HashSet<int>();
        void Node(int at)
        {
            if (at < 0 || at + 14 > dir.Length || !seen.Add(at)) return;
            var left = BinaryPrimitives.ReadUInt16LittleEndian(dir.AsSpan(at));
            if (left == 0xFFFF) return; // padding: nothing here
            var right = BinaryPrimitives.ReadUInt16LittleEndian(dir.AsSpan(at + 2));
            var sec = BinaryPrimitives.ReadUInt32LittleEndian(dir.AsSpan(at + 4));
            var len = BinaryPrimitives.ReadUInt32LittleEndian(dir.AsSpan(at + 8));
            var attr = dir[at + 12];
            int nl = dir[at + 13];
            if (at + 14 + nl > dir.Length) return;
            var name = Encoding.Latin1.GetString(dir, at + 14, nl);
            if (left != 0) Node(left * 4);
            if (name.Length > 0 && name.IndexOfAny(['/', '\\', ':']) < 0 && name != ".." && name != ".")
            {
                var rel = prefix + name;
                if ((attr & 0x10) != 0) Walk(f, b, sec, len, rel + "\\", files, depth + 1);
                else files.Add(new File(rel, b + (long)sec * Sector, len));
            }
            if (right != 0) Node(right * 4);
        }
        Node(0);
    }
}
