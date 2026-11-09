namespace FM3Launcher;

/// <summary>
/// Disc 2 (the "Content Install Disc") of the Ultimate Collection: 8 STFS content packages (type 2) at
/// Content\0000000000000000\4D53084D\00000002\4D53084D0CCF00{13,14,15,30,35,40,45,60} (FM3-QUIRKS Q11): six car
/// packs and two track packs, ~3.4 GB. On the console they are installed to the hard drive; here each is unpacked
/// to the user folder's dlc\ (where fm3.exe mounts content packages, src/kernel/imports_xam.cpp). Disc 2's own
/// default.xex (the installer) is not needed. Sources: the Disc 2 image (.iso), an extracted Disc 2 folder, a
/// folder of package files, or one package file.
/// </summary>
public static class Disc2
{
    public static readonly string[] Packages =
        ["4D53084D0CCF0013", "4D53084D0CCF0014", "4D53084D0CCF0015", "4D53084D0CCF0030",
         "4D53084D0CCF0035", "4D53084D0CCF0040", "4D53084D0CCF0045", "4D53084D0CCF0060"];
    const string ContentDir = @"Content\0000000000000000\4D53084D\00000002\";

    public static string Dir => Path.Combine(Paths.User, "dlc");

    public sealed record Package(string Name, bool Installed, string Title);

    /// <summary>The Disc 2 packages and whether each is installed (folder plus .license).</summary>
    public static List<Package> Status() => Packages.Select(p =>
    {
        var dest = Path.Combine(Dir, p);
        var installed = Directory.Exists(dest) && File.Exists(dest + ".license");
        var title = File.Exists(dest + ".title") ? System.Text.Encoding.Unicode.GetString(File.ReadAllBytes(dest + ".title")) : "";
        return new Package(p, installed, title);
    }).ToList();

    // One package to install: where it is read from.
    sealed record Source(string Name, string File, long Offset, long Size);

    static List<Source> Find(string source)
    {
        var found = new List<Source>();
        if (File.Exists(source))
        {
            using var f = File.OpenRead(source);
            var head = new byte[4];
            f.ReadExactly(head);
            if (head.AsSpan().SequenceEqual("LIVE"u8) || head.AsSpan().SequenceEqual("PIRS"u8) || head.AsSpan().SequenceEqual("CON "u8))
                found.Add(new Source(Path.GetFileName(source), source, 0, f.Length));
            else
                foreach (var x in XIso.Files(f, Path.GetFileName(source)))
                    if (x.Path.StartsWith(ContentDir, StringComparison.OrdinalIgnoreCase) && x.Path.IndexOf('\\', ContentDir.Length) < 0)
                        found.Add(new Source(x.Path[ContentDir.Length..], source, x.Offset, x.Size));
        }
        else if (Directory.Exists(source))
        {
            foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
            {
                var name = Path.GetFileName(file);
                var inContent = Path.GetDirectoryName(file)!.EndsWith(@"4D53084D\00000002", StringComparison.OrdinalIgnoreCase);
                if (inContent || Packages.Contains(name, StringComparer.OrdinalIgnoreCase))
                    found.Add(new Source(name, file, 0, new FileInfo(file).Length));
            }
        }
        else throw new FileNotFoundException($"{source} doesn't exist.");
        return found;
    }

    /// <summary>Installs every content package found in <paramref name="source"/>. report gets progress lines.
    /// Returns the installed package names.</summary>
    public static List<string> Install(string source, Action<string> report)
    {
        if (Game.Running) throw new InvalidOperationException("Close the game first: it reads the content packages while it runs.");
        var found = Find(source);
        if (found.Count == 0)
            throw new InvalidDataException($"No content packages in {Path.GetFileName(source)}: choose the Disc 2 image (\"Content Install Disc\"), its extracted folder, or the package files.");
        Directory.CreateDirectory(Dir);
        long total = found.Sum(x => x.Size);
        var drive = new DriveInfo(Path.GetPathRoot(Path.GetFullPath(Dir))!);
        if (drive.AvailableFreeSpace < total + (256L << 20))
            throw new IOException($"Not enough free space on {drive.Name}: the packages need {total / (1 << 20):N0} MB, {drive.AvailableFreeSpace / (1 << 20):N0} MB free.");
        var installed = new List<string>();
        long done = 0;
        int lastPercent = -1;
        foreach (var p in found)
        {
            report($"Installing {p.Name} ({p.Size / (1 << 20):N0} MB)...");
            using var f = new FileStream(p.File, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 16);
            var files = Stfs.Install(f, p.Offset, p.Name, Dir, n =>
            {
                done += n;
                var percent = (int)(done * 100 / Math.Max(1, total));
                if (percent != lastPercent && percent % 5 == 0)
                {
                    lastPercent = percent;
                    report($"{percent}%");
                }
            });
            installed.Add(Stfs.FolderName(p.Name));
            report($"{p.Name}: {files} files");
        }
        var missing = Status().Where(s => !s.Installed).Select(s => s.Name).ToList();
        report(missing.Count == 0 ? "All 8 Disc 2 packages are installed." : $"Not installed yet: {string.Join(", ", missing)}");
        return installed;
    }
}
