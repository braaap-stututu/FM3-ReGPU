using System.Buffers.Binary;
using System.Text;

namespace FM3Launcher;

/// <summary>
/// Xbox 360 STFS packages (LIVE, PIRS, CON): Disc 2's content packages and downloaded DLC. Ported from the FM4
/// fork's launcher (Stfs.cs), reading from any position of a stream, so a package is read straight out of the
/// Disc 2 image. A header up to 0xA000, then 0x1000-byte blocks, with a hash table block before every 0xAA data
/// blocks (and higher-level tables every 0xAA*0xAA); CON packages keep two copies of each table. Installed
/// packages are unpacked folders with their name (.title, UTF-16 LE) and license mask (.license) beside them, as
/// the game reads them (src/kernel/imports_xam.cpp); .license is written last and marks a finished install.
/// </summary>
public sealed class Stfs
{
    public const uint Fm3TitleId = 0x4D53084D;
    const int Block = 0x1000, L0 = 0xAA, L1 = 0xAA * 0xAA;
    readonly Stream _f;
    readonly long _base, _first;
    readonly int _sep;
    readonly int _tableBlocks, _tableStart;
    public uint ContentType { get; }
    public uint TitleId { get; }
    public uint LicenseMask { get; }
    public string DisplayName { get; }

    public sealed record Entry(string Path, bool Directory, bool Consecutive, int Blocks, int Start, long Size);

    /// <summary>A package that starts at <paramref name="offset"/> of <paramref name="f"/> (the stream stays the caller's).</summary>
    public Stfs(Stream f, long offset = 0)
    {
        _f = f;
        _base = offset;
        var head = new byte[0x1800];
        _f.Seek(_base, SeekOrigin.Begin);
        _f.ReadExactly(head);
        var magic = Encoding.ASCII.GetString(head, 0, 4);
        if (magic is not ("LIVE" or "PIRS" or "CON ")) throw new InvalidDataException("Not an Xbox 360 package (STFS).");
        _sep = magic == "CON " ? 2 : 1;
        ContentType = BinaryPrimitives.ReadUInt32BigEndian(head.AsSpan(0x344));
        TitleId = BinaryPrimitives.ReadUInt32BigEndian(head.AsSpan(0x360));
        var headerSize = BinaryPrimitives.ReadUInt32BigEndian(head.AsSpan(0x340));
        _first = (headerSize + 0xFFF) & ~0xFFFL;
        var vd = head.AsSpan(0x379, 0x24);
        _tableBlocks = BinaryPrimitives.ReadUInt16LittleEndian(vd[3..]);
        _tableStart = vd[5] | vd[6] << 8 | vd[7] << 16;
        for (int i = 0; i < 16; i++)
            LicenseMask |= BinaryPrimitives.ReadUInt32BigEndian(head.AsSpan(0x22C + i * 0x10 + 8));
        DisplayName = Encoding.BigEndianUnicode.GetString(head.AsSpan(0x411, 0x80)).Split('\0')[0].Trim();
    }

    long Backing(int block)
    {
        long n = block + _sep * (block / L0 + 1);
        if (block >= L0) n += _sep * (block / L1 + 1);
        if (block >= L1) n += _sep;
        return n;
    }

    void ReadBlock(int block, byte[] buf)
    {
        _f.Seek(_base + _first + Backing(block) * Block, SeekOrigin.Begin);
        _f.ReadExactly(buf, 0, Block);
    }

    int NextBlock(int block)
    {
        var table = Backing(block / L0 * L0) - _sep;
        var e = new byte[0x18];
        _f.Seek(_base + _first + table * Block + block % L0 * 0x18, SeekOrigin.Begin);
        _f.ReadExactly(e);
        return e[0x15] << 16 | e[0x16] << 8 | e[0x17];
    }

    IEnumerable<int> Chain(int start, int count, bool consecutive)
    {
        if (consecutive)
        {
            for (int i = 0; i < count; i++) yield return start + i;
            yield break;
        }
        for (int b = start, n = 0; n < count && b != 0xFFFFFF; n++, b = NextBlock(b)) yield return b;
    }

    public List<Entry> Files()
    {
        var table = new List<byte>();
        var buf = new byte[Block];
        foreach (var b in Chain(_tableStart, _tableBlocks, false))
        {
            ReadBlock(b, buf);
            table.AddRange(buf);
        }
        var t = table.ToArray();
        var raw = new List<(string Name, bool Dir, bool Cons, int Blocks, int Start, int Parent, long Size)>();
        for (int i = 0; i + 0x40 <= t.Length; i += 0x40)
        {
            var e = t.AsSpan(i, 0x40);
            int len = e[0x28] & 0x3F;
            if (len == 0) break;
            raw.Add((Encoding.ASCII.GetString(e[..len]), (e[0x28] & 0x80) != 0, (e[0x28] & 0x40) != 0, e[0x2C] | e[0x2D] << 8 | e[0x2E] << 16,
                e[0x2F] | e[0x30] << 8 | e[0x31] << 16, BinaryPrimitives.ReadInt16BigEndian(e[0x32..]), BinaryPrimitives.ReadUInt32BigEndian(e[0x34..])));
        }
        string PathOf(int i)
        {
            var parts = new List<string>();
            for (int guard = 0; i >= 0 && i < raw.Count && guard < 64; guard++, i = raw[i].Parent) parts.Add(raw[i].Name);
            parts.Reverse();
            return string.Join('/', parts);
        }
        return raw.Select((r, i) => new Entry(PathOf(i), r.Dir, r.Cons, r.Blocks, r.Start, r.Size)).ToList();
    }

    /// <summary>Unpacks every file under <paramref name="dest"/> (names checked to stay inside it). progress gets bytes written.</summary>
    public int Extract(string dest, Action<long>? progress = null)
    {
        var root = Path.GetFullPath(dest) + Path.DirectorySeparatorChar;
        int files = 0;
        var buf = new byte[Block];
        foreach (var e in Files())
        {
            var target = Path.GetFullPath(Path.Combine(dest, e.Path.Replace('/', Path.DirectorySeparatorChar)));
            if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) continue;
            if (e.Directory)
            {
                Directory.CreateDirectory(target);
                continue;
            }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            using var output = File.Create(target);
            long remaining = e.Size;
            foreach (var b in Chain(e.Start, e.Blocks, e.Consecutive))
            {
                if (remaining <= 0) break;
                ReadBlock(b, buf);
                var n = (int)Math.Min(remaining, Block);
                output.Write(buf, 0, n);
                remaining -= n;
                progress?.Invoke(n);
            }
            if (remaining > 0) throw new InvalidDataException($"{e.Path} is cut short in the package.");
            files++;
        }
        return files;
    }

    /// <summary>The installed folder name for a package file name: the game's own names are 42 characters at most,
    /// printable ASCII (FM3-QUIRKS Q14).</summary>
    public static string FolderName(string packageName)
    {
        var end = packageName.AsSpan().IndexOfAnyExceptInRange((char)0x20, (char)0x7E);
        var name = end < 0 ? packageName : packageName[..end];
        return name.Length > 42 ? name[..42] : name;
    }

    /// <summary>Installs a package as the game reads DLC: unpacked to dir\name, with name.title and name.license
    /// (written last: an install cut short has no .license and the game doesn't list it). Returns files written.</summary>
    public static int Install(Stream f, long offset, string packageName, string dir, Action<long>? progress = null)
    {
        var s = new Stfs(f, offset);
        if (s.TitleId != Fm3TitleId) throw new InvalidDataException($"{packageName} isn't Forza Motorsport 3 content (title {s.TitleId:X8}).");
        if (s.ContentType != 2) throw new InvalidDataException($"{packageName} isn't downloadable content (type {s.ContentType:X8}).");
        var name = FolderName(packageName);
        var dest = Path.Combine(dir, name);
        foreach (var side in new[] { ".license", ".title" })
            if (File.Exists(dest + side)) File.Delete(dest + side);
        if (Directory.Exists(dest)) Directory.Delete(dest, true);
        Directory.CreateDirectory(dest);
        var files = s.Extract(dest, progress);
        File.WriteAllBytes(dest + ".title", Encoding.Unicode.GetBytes(s.DisplayName));
        File.WriteAllText(dest + ".license", $"{s.LicenseMask:X8}\r\n");
        return files;
    }
}
