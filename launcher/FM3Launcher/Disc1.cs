using System.Security.Cryptography;

namespace FM3Launcher;

/// <summary>
/// Disc 1 (the play disc) from its disc image (.iso): every file of the image copied into the game folder next to the
/// launcher (Paths.GameDirDefault, the install's game\), so a new PC needs only the player's own .iso. The image's
/// default.xex is hashed first: a wrong disc stops before anything is written. About 6.8 GB.
/// </summary>
public static class Disc1
{
    /// <summary>Extracts <paramref name="iso"/> into <paramref name="dest"/> (default: Paths.GameDirDefault) and
    /// returns the folder. report gets progress lines ("N%" for the bar).</summary>
    public static string Install(string iso, Action<string> report, string? dest = null)
    {
        if (Game.Running) throw new InvalidOperationException("Close the game first: it reads the game files while it runs.");
        dest = Path.GetFullPath(dest ?? Paths.GameDirDefault);
        using var f = new FileStream(iso, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20);
        var files = XIso.Files(f, Path.GetFileName(iso));
        var xex = files.FirstOrDefault(x => string.Equals(x.Path, "default.xex", StringComparison.OrdinalIgnoreCase))
            ?? throw new InvalidDataException($"{Path.GetFileName(iso)} has no default.xex: choose the Disc 1 image (Play Disc).");
        report("Checking default.xex...");
        f.Seek(xex.Offset, SeekOrigin.Begin);
        var xexBytes = new byte[xex.Size];
        f.ReadExactly(xexBytes);
        if (!string.Equals(Convert.ToHexString(SHA256.HashData(xexBytes)), Xex.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException(files.Any(x => x.Path.StartsWith("Content\\", StringComparison.OrdinalIgnoreCase))
                ? "That's Disc 2 (Content Install Disc): install it in the Disc 2 tile. The game folder needs Disc 1 (Play Disc)."
                : Xex.Mismatch);
        long total = files.Sum(x => (long)x.Size);
        Directory.CreateDirectory(dest);
        long have = files.Sum(x => { var p = Path.Combine(dest, x.Path); return File.Exists(p) ? new FileInfo(p).Length : 0; });
        var drive = new DriveInfo(Path.GetPathRoot(dest)!);
        if (drive.AvailableFreeSpace + have < total + (256L << 20))
            throw new IOException($"Not enough free space on {drive.Name}: Disc 1 needs {total / (1 << 20):N0} MB, {drive.AvailableFreeSpace / (1 << 20):N0} MB free.");
        var buf = new byte[1 << 20];
        long done = 0;
        int lastPercent = -1;
        foreach (var x in files)
        {
            var path = Path.Combine(dest, x.Path);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            // A file already there with the right size is kept (an extraction run again after a stop).
            if (!(File.Exists(path) && new FileInfo(path).Length == x.Size))
            {
                var tmp = path + ".part";
                using (var o = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
                {
                    f.Seek(x.Offset, SeekOrigin.Begin);
                    long left = x.Size;
                    while (left > 0)
                    {
                        int n = (int)Math.Min(buf.Length, left);
                        f.ReadExactly(buf, 0, n);
                        o.Write(buf, 0, n);
                        left -= n;
                        done += n;
                        var percent = (int)(done * 100 / Math.Max(1, total));
                        if (percent != lastPercent)
                        {
                            lastPercent = percent;
                            report($"{percent}%");
                        }
                    }
                }
                File.Move(tmp, path, true);
            }
            else done += x.Size;
        }
        report($"Disc 1: {files.Count} files in {dest}");
        return dest;
    }
}
