using System.Diagnostics;

namespace FM3Launcher;

/// <summary>
/// Linux: fm3.exe runs through a Windows compatibility layer (session 39). Three ways, picked in this order by "auto"
/// (settings.ini linux_runner, launcher Settings > Linux):
///  - umu: umu-launcher's umu-run (Proton outside Steam, the Steam Runtime container; downloads UMU-Proton itself, or
///    the Proton named in linux_proton / PROTONPATH),
///  - proton: a Proton from Steam (linux_proton, else the newest installed: GE-Proton in compatibilitytools.d first,
///    then Valve's), run as `proton run` with STEAM_COMPAT_DATA_PATH,
///  - wine: wine on PATH (D3D12 needs a Wine with vkd3d; Proton's vkd3d-proton is the tested way).
/// One prefix for all of them: linux_prefix, else ~/.local/share/fm3-regpu/prefix (Proton's pfx inside it). Paths the
/// game is given (the game folder, FM3_USER_DIR) are Wine paths: Z: is the Linux root.
/// </summary>
public static class Runner
{
    public sealed record Choice(string Value, string Name, bool Available);

    static string Home => Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

    public static string DefaultPrefix => Path.Combine(Home, ".local", "share", "fm3-regpu", "prefix");

    static string? OnPath(string name)
    {
        foreach (var dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(':', StringSplitOptions.RemoveEmptyEntries))
        {
            var p = Path.Combine(dir, name);
            if (File.Exists(p)) return p;
        }
        return null;
    }

    static IEnumerable<string> SteamRoots()
    {
        foreach (var r in new[] { Path.Combine(Home, ".steam", "steam"), Path.Combine(Home, ".local", "share", "Steam"),
                     Path.Combine(Home, ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam") })
            if (Directory.Exists(r)) yield return r;
    }

    /// <summary>Installed Protons (folders with a `proton` script), GE-Proton (compatibilitytools.d) first, newest first.</summary>
    public static List<string> Protons()
    {
        var found = new List<string>();
        foreach (var root in SteamRoots())
        {
            foreach (var sub in new[] { "compatibilitytools.d", Path.Combine("steamapps", "common") })
            {
                var dir = Path.Combine(root, sub);
                if (!Directory.Exists(dir)) continue;
                foreach (var d in Directory.GetDirectories(dir).OrderByDescending(d => File.GetLastWriteTimeUtc(d)))
                    if (File.Exists(Path.Combine(d, "proton")) && !found.Any(f => Path.GetFileName(f) == Path.GetFileName(d)))
                        found.Add(d);
            }
        }
        return found;
    }

    public static List<Choice> Choices() =>
    [
        new("auto", "Automatic (umu-launcher, then Steam's Proton, then Wine)", true),
        new("umu", "umu-launcher (Proton outside Steam)" + (OnPath("umu-run") is null ? ": not installed" : ""), OnPath("umu-run") is not null),
        new("proton", "Proton from Steam" + (Protons().Count == 0 ? ": none found" : $" ({Path.GetFileName(Protons()[0])})"), Protons().Count > 0),
        new("wine", "Wine" + (OnPath("wine") is null ? ": not installed" : ""), OnPath("wine") is not null),
    ];

    /// <summary>The way the next start uses, after "auto" is resolved; and why (for the Settings page).</summary>
    public static (string Way, string Detail) Resolve(GameSettings s)
    {
        var want = s.LinuxRunner;
        string? proton = s.LinuxProton is { Length: > 0 } p && File.Exists(Path.Combine(p, "proton")) ? p : Protons().FirstOrDefault();
        if (want == "umu" || (want == "auto" && OnPath("umu-run") is not null))
            return OnPath("umu-run") is { } umu ? ("umu", umu + (s.LinuxProton is { Length: > 0 } ? $" with {s.LinuxProton}" : " (UMU-Proton)")) : ("umu", "umu-run isn't installed");
        if (want == "proton" || (want == "auto" && proton is not null))
            return proton is not null ? ("proton", proton) : ("proton", "no Proton found in Steam");
        return OnPath("wine") is { } wine ? ("wine", wine) : ("wine", "wine isn't installed");
    }

    /// <summary>A Linux path as the game sees it under Wine (Z: is /).</summary>
    public static string WinePath(string linuxPath) => "Z:" + Path.GetFullPath(linuxPath).Replace('/', '\\');

    /// <summary>The start of fm3.exe through the chosen layer, with `args` (Wine paths already) and the game's working
    /// folder. Environment variables put on the result reach the game.</summary>
    public static ProcessStartInfo StartInfo(string exe, IEnumerable<string> args, string workingDir)
    {
        var s = GameSettings.Load();
        var (way, detail) = Resolve(s);
        var prefix = s.LinuxPrefix is { Length: > 0 } pre ? pre : DefaultPrefix;
        Directory.CreateDirectory(prefix);
        ProcessStartInfo psi;
        switch (way)
        {
            case "umu":
                if (!File.Exists(detail.Split(' ')[0])) throw new InvalidOperationException("umu-launcher (umu-run) isn't installed: install it, or pick another way in Settings > Linux.");
                psi = new ProcessStartInfo(detail.Split(' ')[0]);
                psi.Environment["WINEPREFIX"] = Path.Combine(prefix, "pfx");
                psi.Environment["GAMEID"] = "umu-default";
                if (s.LinuxProton is { Length: > 0 }) psi.Environment["PROTONPATH"] = s.LinuxProton;
                psi.ArgumentList.Add(exe);
                break;
            case "proton":
                if (!File.Exists(Path.Combine(detail, "proton"))) throw new InvalidOperationException("No Proton found: install one in Steam (or GE-Proton), set its folder in Settings > Linux, or pick Wine.");
                psi = new ProcessStartInfo(Path.Combine(detail, "proton"));
                psi.Environment["STEAM_COMPAT_DATA_PATH"] = prefix;
                psi.Environment["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = SteamRoots().FirstOrDefault() ?? Path.Combine(Home, ".steam", "steam");
                psi.ArgumentList.Add("run");
                psi.ArgumentList.Add(exe);
                break;
            default:
                if (!File.Exists(detail)) throw new InvalidOperationException("Wine isn't installed: install wine (with vkd3d), Proton from Steam or umu-launcher.");
                psi = new ProcessStartInfo(detail);
                psi.Environment["WINEPREFIX"] = Path.Combine(prefix, "pfx");
                psi.ArgumentList.Add(exe);
                break;
        }
        foreach (var a in args) psi.ArgumentList.Add(a);
        psi.WorkingDirectory = workingDir;
        psi.UseShellExecute = false;
        // (Wine's own messages would otherwise fill the launcher's terminal.)
        psi.Environment["WINEDEBUG"] = Environment.GetEnvironmentVariable("WINEDEBUG") ?? "-all";
        return psi;
    }

    /// <summary>The display's modes on Linux (xrandr), 1280x720 and up, smallest first; common ones if it isn't there.</summary>
    public static List<string> Resolutions()
    {
        var set = new HashSet<(int, int)> { (1280, 720), (1920, 1080), (2560, 1440), (3840, 2160) };
        try
        {
            using var p = Process.Start(new ProcessStartInfo("xrandr", "--query") { RedirectStandardOutput = true, UseShellExecute = false });
            if (p is not null)
            {
                var text = p.StandardOutput.ReadToEnd();
                p.WaitForExit(2000);
                set.Clear();
                set.Add((1280, 720));
                foreach (var line in text.Split('\n'))
                {
                    var t = line.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? "";
                    var xy = t.Split('x');
                    if (xy.Length == 2 && int.TryParse(xy[0], out var w) && int.TryParse(xy[1], out var h) && w >= 1280 && h >= 720)
                        set.Add((w, h));
                }
            }
        }
        catch { } // (no xrandr: the common list)
        return set.OrderBy(m => (long)m.Item1 * m.Item2).Select(m => $"{m.Item1}x{m.Item2}").ToList();
    }
}
