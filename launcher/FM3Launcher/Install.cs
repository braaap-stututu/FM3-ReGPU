using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace FM3Launcher;

/// <summary>Where things are: the install (fm3.exe, game\) and the user folder (Documents\fm3).</summary>
public static class Paths
{
    /// <summary>The install folder: FM3_ROOT, else the first folder up from the launcher that holds build\rel\fm3.exe
    /// (a source checkout) or fm3.exe itself (an install).</summary>
    public static readonly string Root = Env("FM3_ROOT") ?? FindRoot();
    public static string Exe => Env("FM3_EXE") ?? (File.Exists(Path.Combine(Root, "fm3.exe")) ? Path.Combine(Root, "fm3.exe") : Path.Combine(Root, "build", "rel", "fm3.exe"));
    public static string GameDirDefault => Path.Combine(Root, "game");
    /// <summary>The disc's files (Disc 1, the play disc): FM3_GAME_DIR, else the Game folder chosen in the launcher,
    /// else game next to the exe.</summary>
    public static string GameDir => Env("FM3_GAME_DIR") ?? (GameSettings.Load().GameFolder is { Length: > 0 } g ? g : GameDirDefault);
    /// <summary>True when FM3_GAME_DIR decides the game folder (play.cmd, launcher.cmd): the launcher's pick is ignored.</summary>
    public static bool GameDirFromEnv => Env("FM3_GAME_DIR") is not null;
    public static readonly string User = FindUser();
    public static string SettingsFile => Path.Combine(User, "settings.ini");
    /// <summary>Where fm3.exe runs, so its fm3.log (src/log.cpp) and the debug server's out\ folders land there.</summary>
    public static string RunDir => Path.Combine(Root, "out", "run");
    public static string Log => Path.Combine(Env("REGPU_LOG_DIR") ?? RunDir, "fm3.log");

    static string? Env(string name) => Environment.GetEnvironmentVariable(name) is { Length: > 0 } v ? v : null;

    /// <summary>FM3_USER_DIR, else Documents\fm3 in the Documents folder Windows reports (it may be moved off C:), as
    /// fm3.exe picks it (src/user_dir.cpp).</summary>
    static string FindUser()
    {
        if (Env("FM3_USER_DIR") is { } custom) return Path.GetFullPath(custom);
        return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "fm3");
    }

    static string FindRoot()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "build", "rel", "fm3.exe")) || File.Exists(Path.Combine(dir.FullName, "fm3.exe")))
                return dir.FullName;
        return Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
    }
}

/// <summary>
/// settings.ini in the user folder, the file fm3.exe reads (src/settings.cpp). Keys the launcher doesn't know are
/// kept as they are (fm3.exe never writes the file).
/// </summary>
public sealed class GameSettings
{
    public string Profile { get; set; } = "Player";
    public string Resolution { get; set; } = "1280x720";
    public string Aspect { get; set; } = "auto";
    public bool Fullscreen { get; set; }
    public bool Vsync { get; set; } = true;
    public int MaxFps { get; set; } = 60;
    public int Msaa { get; set; } = 1;
    // The 3D scene's upscaler: off, fsr1 or fsr3 (settings.ini "upscaler"), at Upscale = the game's upscale_quality
    // (1 quality to 4 ultra performance). FSR 4 needs a newer ReGPU than FM3 has.
    public string Upscaler { get; set; } = "off";
    public int Upscale { get; set; } = 1;
    public int Sharpening { get; set; } = 50; // percent; settings.ini holds 0-1
    public int Language { get; set; } = 1;
    public bool SkipIntro { get; set; }            // skip_intro: straight to the title screen (src/hooks/boot_options.cpp)
    public bool QuickRace { get; set; }            // quick_race: Free Play > Quick Race by itself after boot (implies skip_intro)
    public bool GridTest { get; set; }             // grid_test: the reference career race's start grid by itself, world frozen there (src/hooks/grid_test.cpp)
    // perf_preset: off, turbo, mild, potato, low, extreme (src/gpu_host.cpp ApplyPerfPreset; read by the game, so play.cmd too).
    public string PerfPreset { get; set; } = "off";
    public static readonly string[] PerfPresets = ["off", "turbo", "mild", "potato", "low", "extreme"];
    // Launcher only, set on the game's environment at launch (Game.Launch):
    public bool Anisotropy { get; set; } = true;   // off: REGPU_NO_ANISO
    public bool AnisoFromGame { get; set; }        // aniso_from_game: REGPU_ANISO_FROM_FETCH, each texture's own anisotropy instead of 16x (session 32, I8: GPU -3.8% at 4K; read by the game, so play.cmd too)
    public bool HideShaderOverlay { get; set; }    // REGPU_NO_OVERLAY
    public bool Timer { get; set; } = true;        // the test timer, top left (off: FM3_NO_TIMER)
    public bool PerfLog { get; set; }              // REGPU_PERF: frame timings in fm3.log (times every draw: costs the draw thread)
    public bool PerfLogLight { get; set; }         // perf_log_light: REGPU_PERF_LITE, one line a second, no per-draw work (session 31; read by the game, so play.cmd too)
    public bool DrawWorkers { get; set; }          // FM3_DRAW_THREAD_PIECES: draw workers on the draw thread (A/B; off = faster at 720p)
    public bool HalfRateShadows { get; set; } = false; // shadow_rate=2: the car shadow passes every second frame (src/hooks/car_shadows.cpp, session 32)
    public bool HalfRateMirror { get; set; } = false; // mirror_rate=2: the rear-view mirror rendered every second frame (src/hooks/mirror.cpp)
    public bool OneTile { get; set; } = false; // one_tile: L36, the race scene drawn as one guest tile (src/hooks/one_tile.cpp; read by the game, so play.cmd too)
    public bool VramUploadRing { get; set; } = true; // vram_upload_ring: L47, ReGPU's upload ring in VRAM via resizable BAR (src/gpu_host.cpp; read by the game, so play.cmd too)
    public bool AsyncPresent { get; set; } = true; // async_present: L12, the frame ends on ReGPU's replay thread (src/gpu_host.cpp; read by the game, so play.cmd too)
    public bool EngineCrossfade { get; set; }      // engine_crossfade: engine sound loops crossfaded at the restart (src/audio/xma.cpp; read by the game, so play.cmd too)
    public bool DebugServer { get; set; } = true;  // REGPU_DEBUG: tools\fm3ctl.ps1 can attach (127.0.0.1 only)
    // Launcher only: where the disc's files are; empty = Paths.GameDirDefault.
    public string GameFolder { get; set; } = "";

    // The racing wheel's wheel_* keys are the Wheel stuff tab's (Wheel.cs); Save keeps them like any other key it doesn't know.
    static readonly string[] Known = ["profile", "resolution", "aspect", "fullscreen", "vsync", "max_fps", "msaa", "upscaler", "upscale_quality", "sharpness", "language", "skip_intro", "quick_race", "grid_test", "perf_preset", "accurate_sun" /* old option (session 13): dropped on save */, "anisotropy", "aniso_from_game", "hide_shader_overlay", "timer", "perf_log", "perf_log_light", "draw_workers", "async_present", "vram_upload_ring", "one_tile", "mirror_rate", "shadow_rate", "engine_crossfade", "debug_server", "game_folder"];

    public static GameSettings Load()
    {
        var s = new GameSettings();
        foreach (var (k, v) in Read())
            switch (k)
            {
                case "profile": s.Profile = v; break;
                case "resolution": s.Resolution = v; break;
                case "aspect": s.Aspect = v; break;
                case "fullscreen": s.Fullscreen = v == "1"; break;
                case "vsync": s.Vsync = v == "1"; break;
                case "max_fps": s.MaxFps = int.TryParse(v, out var f) ? f : 60; break;
                case "msaa": s.Msaa = int.TryParse(v, out var m) ? m : 1; break;
                case "upscaler": s.Upscaler = Options.Upscalers.Any(u => u.Value == v) ? v : "off"; break;
                case "upscale_quality": s.Upscale = int.TryParse(v, out var q) ? Math.Clamp(q, 1, 4) : 1; break;
                case "sharpness":
                    s.Sharpening = double.TryParse(v, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var sh)
                        ? (int)Math.Round(sh * 100) : 50;
                    break;
                case "language": s.Language = int.TryParse(v, out var l) && l > 0 ? l : 1; break;
                case "skip_intro": s.SkipIntro = v == "1"; break;
                case "quick_race": s.QuickRace = v == "1"; break;
                case "grid_test": s.GridTest = v == "1"; break;
                case "perf_preset": s.PerfPreset = PerfPresets.Contains(v) ? v : "off"; break;
                case "anisotropy": s.Anisotropy = v != "0"; break;
                case "aniso_from_game": s.AnisoFromGame = v == "1"; break;
                case "hide_shader_overlay": s.HideShaderOverlay = v == "1"; break;
                case "timer": s.Timer = v != "0"; break;
                case "perf_log": s.PerfLog = v == "1"; break;
                case "perf_log_light": s.PerfLogLight = v == "1"; break;
                case "draw_workers": s.DrawWorkers = v == "1"; break;
                case "async_present": s.AsyncPresent = v != "0"; break;
                case "vram_upload_ring": s.VramUploadRing = v == "1"; break;
                case "one_tile": s.OneTile = v == "1"; break;
                case "mirror_rate": s.HalfRateMirror = v == "2"; break;
                case "shadow_rate": s.HalfRateShadows = v == "2"; break;
                case "engine_crossfade": s.EngineCrossfade = v == "1"; break;
                case "debug_server": s.DebugServer = v != "0"; break;
                case "game_folder": s.GameFolder = v; break;
            }
        return s;
    }

    public void Save()
    {
        var keep = Read().Where(kv => !Known.Contains(kv.Key)).ToList();
        var profile = new string(Profile.Trim().Where(c => !char.IsControl(c) && c != '=').Take(15).ToArray());
        var sharp = (Math.Clamp(Sharpening, 0, 100) / 100.0).ToString(System.Globalization.CultureInfo.InvariantCulture);
        var sb = new StringBuilder("# Forza Motorsport 3 (fm3) settings; the launcher edits this file.\n");
        sb.Append($"profile={(profile.Length > 0 ? profile : "Player")}\n");
        sb.Append($"resolution={Resolution}\naspect={Aspect}\nfullscreen={B(Fullscreen)}\nvsync={B(Vsync)}\nmax_fps={Math.Clamp(MaxFps, 30, 360)}\n");
        sb.Append($"msaa={(Options.Msaa.Any(x => x.Value == Msaa) ? Msaa : 1)}\n");
        sb.Append($"upscaler={(Options.Upscalers.Any(u => u.Value == Upscaler) ? Upscaler : "off")}\nupscale_quality={Math.Clamp(Upscale, 1, 4)}\nsharpness={sharp}\n");
        sb.Append($"language={Language}\n");
        sb.Append($"skip_intro={B(SkipIntro)}\nquick_race={B(QuickRace)}\ngrid_test={B(GridTest)}\n"); // (session 23: these three were read but never saved)
        sb.Append($"perf_preset={(PerfPresets.Contains(PerfPreset) ? PerfPreset : "off")}\n");
        sb.Append($"anisotropy={B(Anisotropy)}\naniso_from_game={B(AnisoFromGame)}\nhide_shader_overlay={B(HideShaderOverlay)}\ntimer={B(Timer)}\nperf_log={B(PerfLog)}\nperf_log_light={B(PerfLogLight)}\ndraw_workers={B(DrawWorkers)}\nasync_present={B(AsyncPresent)}\nvram_upload_ring={B(VramUploadRing)}\none_tile={B(OneTile)}\nmirror_rate={(HalfRateMirror ? 2 : 1)}\nshadow_rate={(HalfRateShadows ? 2 : 1)}\nengine_crossfade={B(EngineCrossfade)}\ndebug_server={B(DebugServer)}\n");
        sb.Append($"game_folder={Clean(GameFolder)}\n");
        foreach (var (k, v) in keep) sb.Append($"{k}={v}\n");
        Directory.CreateDirectory(Paths.User);
        File.WriteAllText(Paths.SettingsFile, sb.ToString());
    }

    static string B(bool b) => b ? "1" : "0";
    static string Clean(string path) => new string(path.Where(c => !char.IsControl(c)).ToArray()).Trim();

    static List<KeyValuePair<string, string>> Read()
    {
        var list = new List<KeyValuePair<string, string>>();
        if (!File.Exists(Paths.SettingsFile)) return list;
        foreach (var raw in File.ReadAllLines(Paths.SettingsFile))
        {
            var line = raw.Trim();
            int eq = line.IndexOf('=');
            if (line.StartsWith('#') || line.StartsWith(';') || eq <= 0) continue;
            list.Add(new(line[..eq].Trim(), line[(eq + 1)..].Trim()));
        }
        return list;
    }
}

/// <summary>Choices the pages offer.</summary>
public static class Options
{
    public sealed record Language(int Id, string Code, string Name);

    // The console's language IDs (XGetLanguage, src/kernel/imports_xam.cpp) by the string table on Disc 1
    // (Media\StringTables\<code>.zip). Only English has been played on FM3 so far. The disc also has CZ, HU and MX
    // (Mexican Spanish) tables, which no language ID names.
    public static readonly Language[] Languages =
    [
        new(1, "EN", "English"), new(3, "DE", "Deutsch"), new(4, "FR", "Français"), new(5, "ES", "Español"),
        new(6, "IT", "Italiano"), new(11, "PL", "Polski"), new(12, "RU", "Русский"),
        new(2, "JP", "日本語"), new(7, "KO", "한국어"), new(8, "CHT", "繁體中文"), new(10, "CHS", "简体中文"),
    ];

    /// <summary>The languages whose string table is in the game folder's Media\StringTables.</summary>
    public static IEnumerable<Language> InstalledLanguages()
    {
        var dir = Path.Combine(Paths.GameDir, "Media", "StringTables");
        if (!Directory.Exists(dir)) return Languages.Take(1);
        var have = Directory.GetFiles(dir, "*.zip").Select(f => Path.GetFileNameWithoutExtension(f).ToUpperInvariant()).ToHashSet();
        return Languages.Where(l => have.Contains(l.Code));
    }

    public static readonly int[] Fps = [30, 60, 120, 144, 165, 240, 360];
    public static readonly (string Value, string Name)[] Aspects = [("auto", "Auto (match resolution)"), ("16:9", "16:9"), ("21:9", "21:9 (experimental)"), ("32:9", "32:9 (experimental)")];
    public static readonly (int Value, string Name)[] Msaa = [(1, "Off"), (2, "2x MSAA"), (4, "4x MSAA")];
    public static readonly (string Value, string Name)[] Upscalers = [("off", "Off (native)"), ("fsr1", "FSR 1 (any graphics card)"), ("fsr3", "FSR 3 (any graphics card)")];
    public static readonly (int Value, string Name)[] Upscale = [(1, "Quality"), (2, "Balanced"), (3, "Performance"), (4, "Ultra performance")];
    public static readonly (int Value, string Name)[] Sharpening = [(0, "Off"), (25, "Low"), (50, "Medium"), (75, "High"), (100, "Maximum")];

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DEVMODE
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumDisplaySettingsW(string? device, int mode, ref DEVMODE dm);

    /// <summary>The primary display's modes from 1280x720 up, smallest first.</summary>
    public static List<string> Resolutions()
    {
        var set = new HashSet<(int, int)> { (1280, 720) };
        var dm = new DEVMODE { dmSize = (short)Marshal.SizeOf<DEVMODE>() };
        for (int i = 0; EnumDisplaySettingsW(null, i, ref dm); i++)
            if (dm.dmPelsWidth >= 1280 && dm.dmPelsHeight >= 720) set.Add((dm.dmPelsWidth, dm.dmPelsHeight));
        return set.OrderBy(m => (long)m.Item1 * m.Item2).Select(m => $"{m.Item1}x{m.Item2}").ToList();
    }
}

/// <summary>The game folder's default.xex against the one this port is built from (config/fm3.toml): Forza Motorsport
/// 3 Ultimate Collection (USA), Disc 1, default.xex v0.0.0.8.</summary>
public static class Xex
{
    public const string Sha256 = "AA15372C13BC4C899B5EF90A1CFB5634D389B0A3620CC9715F6023D61A9B8742";
    public const string Mismatch = "That folder's default.xex isn't the one from Forza Motorsport 3 Ultimate Collection (USA), Disc 1.";

    // Hashing is only done on a pick or a "Check game files" click; the answer is kept per file (path, size, date),
    // so it goes stale by itself when the file changes.
    static readonly System.Collections.Concurrent.ConcurrentDictionary<string, string> Answers = new();

    static string? Key(string xex)
    {
        try { var fi = new FileInfo(xex); return fi.Exists ? $"{fi.FullName}|{fi.Length}|{fi.LastWriteTimeUtc.Ticks}" : null; }
        catch { return null; }
    }

    static bool Matches(string xex)
    {
        using var f = File.OpenRead(xex);
        var ok = string.Equals(Convert.ToHexString(SHA256.HashData(f)), Sha256, StringComparison.OrdinalIgnoreCase);
        if (Key(xex) is { } key) Answers[key] = ok ? "ok" : "mismatch";
        return ok;
    }

    /// <summary>"ok", "mismatch", "unchecked" or "missing" for the folder's default.xex. Reads nothing but file info.</summary>
    public static string Status(string gameDir)
    {
        var key = Key(Path.Combine(gameDir, "default.xex"));
        if (key is null) return "missing";
        return Answers.TryGetValue(key, out var r) ? r : "unchecked";
    }

    public static string CheckNow(string gameDir)
    {
        var xex = Path.Combine(gameDir, "default.xex");
        if (!File.Exists(xex)) return "missing";
        Matches(xex);
        return Status(gameDir);
    }

    /// <summary>Throws unless the folder holds the supported default.xex and the disc's Media folder.</summary>
    public static void Require(string folder)
    {
        var xex = Path.Combine(folder, "default.xex");
        if (!File.Exists(xex)) throw new InvalidDataException("That folder has no default.xex: pick the folder with the extracted Disc 1 (the play disc).");
        if (!Matches(xex)) throw new InvalidDataException(Mismatch);
        if (!Directory.Exists(Path.Combine(folder, "Media"))) throw new InvalidDataException("That folder has default.xex but no Media folder: it needs all of Disc 1's files.");
    }
}

/// <summary>Starting the game, as play.cmd does.</summary>
public static class Game
{
    public static bool Running => Process.GetProcessesByName("fm3").Any(p =>
    {
        try { return string.Equals(p.MainModule?.FileName, Paths.Exe, StringComparison.OrdinalIgnoreCase); }
        catch { return true; } // can't look inside: assume it is ours
    });

    public static void Launch()
    {
        if (!File.Exists(Paths.Exe)) throw new InvalidOperationException($"fm3.exe isn't at {Paths.Exe}.");
        if (!File.Exists(Path.Combine(Paths.GameDir, "default.xex"))) throw new InvalidOperationException("No game files yet: choose the game folder (the extracted Disc 1) on the Play tab.");
        if (Running) throw new InvalidOperationException("The game is already running.");
        Directory.CreateDirectory(Paths.RunDir);
        // fm3.exe overwrites fm3.log: keep the last run's as fm3.prev.log, so two runs can be compared.
        try { if (File.Exists(Paths.Log)) File.Copy(Paths.Log, Path.ChangeExtension(Paths.Log, ".prev.log"), true); } catch (IOException) { }
        var psi = new ProcessStartInfo(Paths.Exe) { WorkingDirectory = Paths.RunDir, UseShellExecute = false, CreateNoWindow = true };
        psi.ArgumentList.Add(Paths.GameDir);
        var settings = GameSettings.Load();
        // The profile comes from settings.ini (the Profiles tab): an FM3_PROFILE left in the environment would win.
        psi.Environment.Remove("FM3_PROFILE");
        psi.Environment["FM3_USER_DIR"] = Paths.User;
        if (!settings.Anisotropy) psi.Environment["REGPU_NO_ANISO"] = "1";
        if (settings.HideShaderOverlay) psi.Environment["REGPU_NO_OVERLAY"] = "1";
        if (!settings.Timer) psi.Environment["FM3_NO_TIMER"] = "1";
        if (settings.PerfLog) psi.Environment["REGPU_PERF"] = "1";
        if (settings.DrawWorkers) psi.Environment["FM3_DRAW_THREAD_PIECES"] = "1";
        // The live debug server (127.0.0.1 only, tools\fm3ctl.ps1): status, captures, live switches.
        if (settings.DebugServer) psi.Environment["REGPU_DEBUG"] = "1";
        Process.Start(psi)?.Dispose();
    }
}

/// <summary>The shader and pipeline caches in the user folder.</summary>
public static class ShaderCache
{
    public sealed record Info(bool Exists, int Shaders, long Bytes, DateTime? Built);

    static readonly string[] Dirs = ["shadercache", "shaders"];
    static readonly string[] Files = ["pipelines.bin", "pipelines.manifest", "static_shaders.bin"];

    public static Info Get()
    {
        long bytes = 0;
        int count = 0;
        DateTime? built = null;
        foreach (var d in Dirs.Select(d => Path.Combine(Paths.User, d)).Where(Directory.Exists))
            foreach (var f in new DirectoryInfo(d).EnumerateFiles("*", SearchOption.AllDirectories))
            {
                bytes += f.Length;
                if (f.Extension is ".dxbc" or ".ucode") count++;
            }
        foreach (var f in Files.Select(f => new FileInfo(Path.Combine(Paths.User, f))).Where(f => f.Exists))
        {
            bytes += f.Length;
            if (f.Name == "pipelines.bin") built = f.LastWriteTime;
        }
        return new Info(bytes > 0, count, bytes, built);
    }

    /// <summary>Deletes the caches; the next start rebuilds them.</summary>
    public static void Delete()
    {
        if (Game.Running) throw new InvalidOperationException("Close the game first: it is using the shader cache.");
        foreach (var d in Dirs.Select(d => Path.Combine(Paths.User, d)).Where(Directory.Exists)) Directory.Delete(d, true);
        foreach (var f in Files.Select(f => Path.Combine(Paths.User, f)).Where(File.Exists)) File.Delete(f);
    }
}
