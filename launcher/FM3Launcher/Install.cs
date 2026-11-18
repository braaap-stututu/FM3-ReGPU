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
    // Anti-aliasing filters (session 36; read by the game, so play.cmd too): aa = off/fxaa/smaa/cmaa2/taa on the 3D scene,
    // ssaa = supersampling factor "1"/"1.5"/"2", eqaa = AMD's enhanced quality AA with MSAA (where the driver offers it).
    public string Aa { get; set; } = "off";
    public string Ssaa { get; set; } = "1";
    public bool Eqaa { get; set; }
    // gpu: the graphics card the game renders on, by name (Options.Gpus; the game's REGPU_ADAPTER); "" = Windows' choice.
    public string Gpu { get; set; } = "";
    // The VR tab (session 39): the headset mode its Play button starts (screen = the game on a virtual screen, cockpit = the
    // in-car view follows the head, stereo = cockpit in true 3D) and its resolution ("same" = the Graphics tab's). Saved
    // as vr_mode / vr_resolution; settings.ini's vr itself stays off, so a start without the VR tab (Play, play.cmd) is
    // a normal one (FM3_VR from the VR tab's Play button turns it on for that run).
    public string VrMode { get; set; } = "stereo";
    public string VrResolution { get; set; } = "same";
    // vr_camera_motion: keep FM3's in-car camera motion (shake, G-force lean, bumps) in VR (session 38: off with VR).
    public bool VrCameraMotion { get; set; }
    // vr_pace: the frame cap follows the headset's display rate in the head-tracked modes (session 39; on by default).
    public bool VrPace { get; set; } = true;
    // vr_eye_limit: how far the head may move from the seat in VR, cm each way ("0" = no limit, the default).
    public string VrEyeLimit { get; set; } = "0";
    // Linux only (launcher, Runner.cs): how fm3.exe runs (auto, umu, proton, wine), a Proton folder, the Wine prefix.
    public string LinuxRunner { get; set; } = "auto";
    public string LinuxProton { get; set; } = "";
    public string LinuxPrefix { get; set; } = "";
    // triple: one view across three monitors (session 39; the resolution is the three together); triple_fov: each
    // screen's vertical field of view in degrees ("0" = the camera's own where three fit). triple_angle/_bezel stay
    // settings.ini-only (kept as they are).
    // The Triple screens tab's Play button: its resolution ("same" = the Graphics tab's; normally three screens together)
    // and fullscreen. Like VR, triple screen is on only for runs started there (settings.ini triple stays 0).
    public string TripleResolution { get; set; } = "same";
    public bool TripleFullscreen { get; set; } = true;
    public string TripleFov { get; set; } = "0";
    // The Triple screens page (session 39): triple_mode auto (TripleFov above) or physical (this layout: screen diagonal
    // in inches, eye distance in cm, one screen's bezel and the gap between two screens in mm, each side screen's angle in
    // from the centre screen's plane in degrees, 0 = facing the eye). src/hooks/triple_screen.cpp.
    public string TripleMode { get; set; } = "auto";
    public double TripleDiag { get; set; } = 27;
    public double TripleDist { get; set; } = 60;
    public double TripleBezelMm { get; set; }
    public double TripleGapMm { get; set; }
    public double TripleAngleLeft { get; set; }
    public double TripleAngleRight { get; set; }
    // The 3D scene's upscaler: off, fsr1, fsr3 or bilinear (session 34: plain upscale, no sharpening) (settings.ini "upscaler"), at Upscale = the game's upscale_quality
    // (1 quality to 4 ultra performance). FSR 4 (session 36): RDNA 4 GPUs; FSR 3 on others.
    public string Upscaler { get; set; } = "off";
    public int Upscale { get; set; } = 1;
    public int Sharpening { get; set; } = 50; // percent; settings.ini holds 0-1
    public int Language { get; set; } = 1;
    public bool SkipIntro { get; set; }            // skip_intro: straight to the title screen (src/hooks/boot_options.cpp)
    public bool QuickRace { get; set; }            // quick_race: Free Play > Quick Race by itself after boot (implies skip_intro)
    public bool GridTest { get; set; }             // grid_test: the reference career race's start grid by itself, world frozen there (src/hooks/grid_test.cpp)
    // perf_preset: ReGPU's Hyper presets (session 43): off, mild, low, extreme, potato, auto (src/gpu_host.cpp ApplyPerfPreset;
    // read by the game, so play.cmd too). The older "turbo" reads as off.
    public string PerfPreset { get; set; } = "off";
    public static readonly string[] PerfPresets = ["off", "mild", "low", "extreme", "potato", "auto"];
    // The iGPU tab (session 43, Intel/integrated GPU tests; all read by the game, so play.cmd too; src/gpu_host.cpp):
    public bool HyperShaderCuts { get; set; }      // hyper_shader_cuts: Hyper's lossless shader cuts in any preset
    public int HyperCull { get; set; } = -1;       // hyper_cull: -1 preset decides, 0 off, 64 or 256 pixels
    public bool IgpuRootSig11 { get; set; }        // igpu_rootsig11: REGPU_ROOTSIG_11
    public bool IgpuSpecConstants { get; set; }    // igpu_spec_constants: REGPU_SPEC_CONSTANTS
    public bool IgpuHalfRegs { get; set; }         // igpu_half_regs: REGPU_HALF_REGS (real halves with DXC)
    public bool IgpuUmaNoArena { get; set; }       // igpu_uma_no_arena: REGPU_UMA_NO_ARENA
    public bool IgpuUmaTextures { get; set; }      // igpu_uma_textures: REGPU_UMA_TEXTURES
    public string IgpuCompileThreads { get; set; } = "auto"; // igpu_compile_threads: REGPU_COMPILE_BACKGROUND_THREADS (auto, 0 no limit, 1, 2)
    public static readonly string[] IgpuCompileChoices = ["auto", "0", "1", "2"];
    // Launcher only, set on the game's environment at launch (Game.Launch):
    public bool Anisotropy { get; set; } = true;   // off: REGPU_NO_ANISO
    public bool CarLodFar { get; set; }            // car_lod_far: AI cars past the nearest 1-2 at detail level 3 (session 32, L60; read by the game)
    public bool AnisoFromGame { get; set; }        // aniso_from_game: REGPU_ANISO_FROM_FETCH, each texture's own anisotropy instead of 16x (session 32, I8: GPU -3.8% at 4K; read by the game, so play.cmd too)
    public bool HideShaderOverlay { get; set; }    // REGPU_NO_OVERLAY
    public bool FpsPanel { get; set; }             // fps_panel: fps counter and frame-time graph, top centre (session 32; read by the game)
    public bool Timer { get; set; } = true;        // the test timer, top left (off: FM3_NO_TIMER)
    public bool PerfLog { get; set; }              // REGPU_PERF: frame timings in fm3.log (times every draw: costs the draw thread)
    public bool PerfLogLight { get; set; }         // perf_log_light: REGPU_PERF_LITE, one line a second, no per-draw work (session 31; read by the game, so play.cmd too)
    public bool DrawWorkers { get; set; }          // FM3_DRAW_THREAD_PIECES: draw workers on the draw thread (A/B; off = faster at 720p)
    public bool HalfRateReflections { get; set; } = false; // reflect_rate=2: the car reflection faces every second time (src/hooks/car_reflections.cpp, session 32)
    public bool HalfRateShadows { get; set; } = false; // shadow_rate=2: the car shadow passes every second frame (src/hooks/car_shadows.cpp, session 32)
    public bool HalfRateMirror { get; set; } = false; // mirror_rate=2: the rear-view mirror rendered every second frame (src/hooks/mirror.cpp)
    public bool OneTile { get; set; } = true; // one_tile (on by default since session 40, user): L36, the race scene drawn as one guest tile (src/hooks/one_tile.cpp; read by the game, so play.cmd too)
    public bool DxcShaders { get; set; } // shader_compiler=dxc: ReGPU compiles shaders with DXC (REGPU_DXC, src/gpu_host.cpp; read by the game, so play.cmd too)
    public bool VramUploadRing { get; set; } = true; // vram_upload_ring: L47, ReGPU's upload ring in VRAM via resizable BAR (src/gpu_host.cpp; read by the game, so play.cmd too)
    public bool AsyncPresent { get; set; } = true; // async_present: L12, the frame ends on ReGPU's replay thread (src/gpu_host.cpp; read by the game, so play.cmd too)
    public bool EngineCrossfade { get; set; }      // engine_crossfade: engine sound loops crossfaded at the restart (src/audio/xma.cpp; read by the game, so play.cmd too)
    public bool DebugServer { get; set; } = true;  // REGPU_DEBUG: tools\fm3ctl.ps1 can attach (127.0.0.1 only)
    // Launcher only: where the disc's files are; empty = Paths.GameDirDefault.
    public string GameFolder { get; set; } = "";

    // The racing wheel's wheel_* keys are the Wheel stuff tab's (Wheel.cs); Save keeps them like any other key it doesn't know.
    static double Num(string v, double fallback, double min, double max) =>
        double.TryParse(v, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var d) ? Math.Clamp(d, min, max) : fallback;

    static readonly string[] Known = ["profile", "gpu", "resolution", "aspect", "fullscreen", "vsync", "max_fps", "msaa", "aa", "ssaa", "eqaa", "smaa" /* old FM2/FM4 key: dropped on save */, "upscaler", "upscale_quality", "sharpness", "language", "skip_intro", "quick_race", "grid_test", "perf_preset", "accurate_sun" /* old option (session 13): dropped on save */, "anisotropy", "aniso_from_game", "car_lod_far", "hide_shader_overlay", "fps_panel", "timer", "perf_log", "perf_log_light", "draw_workers", "async_present", "vram_upload_ring", "upload_ring_auto" /* session 36 experiment: dropped on save */, "shader_compiler", "vr", "vr_mode", "vr_resolution", "triple_resolution", "triple_fullscreen", "vr_camera_motion", "vr_pace", "vr_eye_limit", "linux_runner", "linux_proton", "linux_prefix", "triple", "triple_fov", "triple_mode", "triple_diag", "triple_dist", "triple_bezel_mm", "triple_gap_mm", "triple_angle_left", "triple_angle_right", "one_tile", "mirror_rate", "shadow_rate", "reflect_rate", "engine_crossfade", "debug_server", "game_folder", "hyper_shader_cuts", "hyper_cull", "igpu_rootsig11", "igpu_spec_constants", "igpu_half_regs", "igpu_uma_no_arena", "igpu_uma_textures", "igpu_compile_threads"];

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
                case "aa": s.Aa = Options.Aa.Any(x => x.Value == v) ? v : "off"; break;
                case "ssaa": s.Ssaa = Options.Ssaa.Any(x => x.Value == v) ? v : "1"; break;
                case "eqaa": s.Eqaa = v == "1"; break;
                case "gpu": s.Gpu = v; break;
                case "vr": if (Options.VrModes.Any(x => x.Value == v)) s.VrMode = v; break; // (before session 39: the mode itself)
                case "vr_mode": s.VrMode = Options.VrModes.Any(x => x.Value == v) ? v : "stereo"; break;
                case "vr_resolution": s.VrResolution = v is { Length: > 0 } ? v : "same"; break;
                case "triple_resolution": s.TripleResolution = v is { Length: > 0 } ? v : "same"; break;
                case "triple_fullscreen": s.TripleFullscreen = v != "0"; break;
                case "upscaler": s.Upscaler = Options.Upscalers.Any(u => u.Value == v) ? v : "off"; break;
                case "upscale_quality": s.Upscale = int.TryParse(v, out var q) ? Math.Clamp(q, 0, 4) : 1; break;
                case "sharpness":
                    s.Sharpening = double.TryParse(v, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var sh)
                        ? (int)Math.Round(sh * 100) : 50;
                    break;
                case "language": s.Language = int.TryParse(v, out var l) && l > 0 ? l : 1; break;
                case "skip_intro": s.SkipIntro = v == "1"; break;
                case "quick_race": s.QuickRace = v == "1"; break;
                case "grid_test": s.GridTest = v == "1"; break;
                case "perf_preset": s.PerfPreset = PerfPresets.Contains(v) ? v : "off"; break;
                case "hyper_shader_cuts": s.HyperShaderCuts = v == "1"; break;
                case "hyper_cull": s.HyperCull = v is "0" or "64" or "256" ? int.Parse(v) : -1; break;
                case "igpu_rootsig11": s.IgpuRootSig11 = v == "1"; break;
                case "igpu_spec_constants": s.IgpuSpecConstants = v == "1"; break;
                case "igpu_half_regs": s.IgpuHalfRegs = v == "1"; break;
                case "igpu_uma_no_arena": s.IgpuUmaNoArena = v == "1"; break;
                case "igpu_uma_textures": s.IgpuUmaTextures = v == "1"; break;
                case "igpu_compile_threads": s.IgpuCompileThreads = IgpuCompileChoices.Contains(v) ? v : "auto"; break;
                case "anisotropy": s.Anisotropy = v != "0"; break;
                case "aniso_from_game": s.AnisoFromGame = v == "1"; break;
                case "car_lod_far": s.CarLodFar = v == "1"; break;
                case "hide_shader_overlay": s.HideShaderOverlay = v == "1"; break;
                case "timer": s.Timer = v != "0"; break;
                case "fps_panel": s.FpsPanel = v == "1"; break;
                case "perf_log": s.PerfLog = v == "1"; break;
                case "perf_log_light": s.PerfLogLight = v == "1"; break;
                case "draw_workers": s.DrawWorkers = v == "1"; break;
                case "async_present": s.AsyncPresent = v != "0"; break;
                case "vram_upload_ring": s.VramUploadRing = v == "1"; break;
                case "shader_compiler": s.DxcShaders = v == "dxc"; break;
                case "one_tile": s.OneTile = v != "0"; break;
                case "vr_camera_motion": s.VrCameraMotion = v == "1"; break;
                case "vr_pace": s.VrPace = v != "0"; break;
                case "linux_runner": s.LinuxRunner = v is "auto" or "umu" or "proton" or "wine" ? v : "auto"; break;
                case "linux_proton": s.LinuxProton = v; break;
                case "linux_prefix": s.LinuxPrefix = v; break;
                case "vr_eye_limit": s.VrEyeLimit = Options.VrEyeLimit.Any(x => x.Value == v) ? v : "0"; break;
                case "triple_mode": s.TripleMode = v == "physical" ? "physical" : "auto"; break;
                case "triple_diag": s.TripleDiag = Num(v, 27, 10, 100); break;
                case "triple_dist": s.TripleDist = Num(v, 60, 20, 300); break;
                case "triple_bezel_mm": s.TripleBezelMm = Num(v, 0, 0, 100); break;
                case "triple_gap_mm": s.TripleGapMm = Num(v, 0, 0, 300); break;
                case "triple_angle_left": s.TripleAngleLeft = Num(v, 0, 0, 89); break;
                case "triple_angle_right": s.TripleAngleRight = Num(v, 0, 0, 89); break;
                case "triple_fov": s.TripleFov = Options.TripleFov.Any(x => x.Value == v) ? v : "0"; break;
                case "mirror_rate": s.HalfRateMirror = v == "2"; break;
                case "shadow_rate": s.HalfRateShadows = v == "2"; break;
                case "reflect_rate": s.HalfRateReflections = v == "2"; break;
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
        sb.Append("aa=" + (Options.Aa.Any(x => x.Value == Aa) ? Aa : "off") + "\nssaa=" + (Options.Ssaa.Any(x => x.Value == Ssaa) ? Ssaa : "1") + "\neqaa=" + B(Eqaa) + "\n");
        sb.Append($"gpu={new string(Gpu.Where(c => !char.IsControl(c) && c != '=').ToArray()).Trim()}\n");
        sb.Append($"vr=off\nvr_mode={(Options.VrModes.Any(x => x.Value == VrMode) ? VrMode : "stereo")}\nvr_resolution={VrResolution}\ntriple_resolution={TripleResolution}\ntriple_fullscreen={B(TripleFullscreen)}\n");
        string N(double d) => Math.Round(d, 1).ToString(System.Globalization.CultureInfo.InvariantCulture);
        sb.Append($"triple_mode={(TripleMode == "physical" ? "physical" : "auto")}\ntriple_diag={N(TripleDiag)}\ntriple_dist={N(TripleDist)}\ntriple_bezel_mm={N(TripleBezelMm)}\ntriple_gap_mm={N(TripleGapMm)}\ntriple_angle_left={N(TripleAngleLeft)}\ntriple_angle_right={N(TripleAngleRight)}\n");
        sb.Append($"upscaler={(Options.Upscalers.Any(u => u.Value == Upscaler) ? Upscaler : "off")}\nupscale_quality={Math.Clamp(Upscale, 0, 4)}\nsharpness={sharp}\n");
        sb.Append($"language={Language}\n");
        sb.Append($"skip_intro={B(SkipIntro)}\nquick_race={B(QuickRace)}\ngrid_test={B(GridTest)}\n"); // (session 23: these three were read but never saved)
        sb.Append($"perf_preset={(PerfPresets.Contains(PerfPreset) ? PerfPreset : "off")}\n");
        if (HyperShaderCuts) sb.Append("hyper_shader_cuts=1\n"); // (the iGPU tab: written only when set, so the preset decides otherwise)
        if (HyperCull is 0 or 64 or 256) sb.Append($"hyper_cull={HyperCull}\n");
        sb.Append($"igpu_rootsig11={B(IgpuRootSig11)}\nigpu_spec_constants={B(IgpuSpecConstants)}\nigpu_half_regs={B(IgpuHalfRegs)}\nigpu_uma_no_arena={B(IgpuUmaNoArena)}\nigpu_uma_textures={B(IgpuUmaTextures)}\n");
        sb.Append($"igpu_compile_threads={(IgpuCompileChoices.Contains(IgpuCompileThreads) ? IgpuCompileThreads : "auto")}\n");
        sb.Append($"anisotropy={B(Anisotropy)}\naniso_from_game={B(AnisoFromGame)}\ncar_lod_far={B(CarLodFar)}\nhide_shader_overlay={B(HideShaderOverlay)}\ntimer={B(Timer)}\nfps_panel={B(FpsPanel)}\nperf_log={B(PerfLog)}\nperf_log_light={B(PerfLogLight)}\ndraw_workers={B(DrawWorkers)}\nasync_present={B(AsyncPresent)}\nvram_upload_ring={B(VramUploadRing)}\none_tile={B(OneTile)}\nmirror_rate={(HalfRateMirror ? 2 : 1)}\nshadow_rate={(HalfRateShadows ? 2 : 1)}\nreflect_rate={(HalfRateReflections ? 2 : 1)}\nengine_crossfade={B(EngineCrossfade)}\nvr_camera_motion={B(VrCameraMotion)}\nvr_pace={B(VrPace)}\nlinux_runner={LinuxRunner}\nlinux_proton={LinuxProton}\nlinux_prefix={LinuxPrefix}\nvr_eye_limit={(Options.VrEyeLimit.Any(x => x.Value == VrEyeLimit) ? VrEyeLimit : "0")}\ntriple=0\ntriple_fov={(Options.TripleFov.Any(x => x.Value == TripleFov) ? TripleFov : "0")}\ndebug_server={B(DebugServer)}\n");
        sb.Append(DxcShaders ? "shader_compiler=dxc\n" : "shader_compiler=fxc\n");
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
    public static readonly (string Value, string Name)[] VrEyeLimit = [("0", "No limit"), ("30", "30 cm each way"), ("60", "60 cm each way"), ("100", "1 m each way")];
    public static readonly (string Value, string Name)[] TripleFov = [("0", "Auto (the camera's own, narrowed to fit)"), ("20", "20 degrees"), ("25", "25 degrees"), ("30", "30 degrees"), ("35", "35 degrees"), ("40", "40 degrees")];
    public static readonly (string Value, string Name)[] VrModes = [("stereo", "Stereo cockpit (3D)"), ("cockpit", "Head-tracked cockpit"), ("screen", "Virtual screen")];

    /// <summary>Resolutions for the Triple screens tab: the display's modes (an Eyefinity/Surround desktop lists its
    /// whole width) and three of each 16:9 mode side by side.</summary>
    public static List<string> TripleResolutions()
    {
        var set = new HashSet<(int, int)>();
        foreach (var r in Resolutions())
        {
            var p = r.Split('x');
            if (p.Length != 2 || !int.TryParse(p[0], out var w) || !int.TryParse(p[1], out var h)) continue;
            set.Add((w, h));
            if (w * 9 == h * 16) set.Add((w * 3, h));
        }
        return set.Where(m => m.Item1 * 9 >= m.Item2 * 40).OrderBy(m => (long)m.Item1 * m.Item2).Select(m => $"{m.Item1}x{m.Item2}").ToList();
    }
    public static readonly (string Value, string Name)[] Aa = [("off", "Off"), ("fxaa", "FXAA (fastest, softest)"), ("smaa", "SMAA (sharp)"), ("cmaa2", "CMAA 2 (sharpest, keeps fine detail)"), ("taa", "TAA (smoothest; can ghost in motion)")];
    public static readonly (string Value, string Name)[] Ssaa = [("1", "Off"), ("1.5", "1.5x (2.25x the pixels)"), ("2", "2x (4x the pixels)")];
    public static readonly (string Value, string Name)[] Upscalers = [("off", "Off (native)"), ("fsr1", "FSR 1 (any graphics card)"), ("fsr3", "FSR 3 (any graphics card)"), ("fsr4", "FSR 4 (RX 9000; RX 7000 with recent drivers; else FSR 3)"), ("bilinear", "Bilinear (fastest, softer; for slow GPUs)"), ("xess", "XeSS (Intel; any recent graphics card)")];
    public static readonly (int Value, string Name)[] Upscale = [(0, "Native (anti-aliasing only)"), (1, "Quality"), (2, "Balanced"), (3, "Performance"), (4, "Ultra performance")];
    public static readonly (int Value, string Name)[] Sharpening = [(0, "Off"), (25, "Low"), (50, "Medium"), (75, "High"), (100, "Maximum")];

    // ---- Graphics cards (DXGI), for the "Graphics card" choice: the game picks one by a part of its name
    // (ReGPU's REGPU_ADAPTER; settings.ini gpu=). The same card can be listed twice (hybrid set-ups): shown once.
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DXGI_ADAPTER_DESC1
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Description;
        public uint VendorId, DeviceId, SubSysId, Revision;
        public nuint DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory;
        public uint LuidLow;
        public int LuidHigh;
        public uint Flags;
    }
    [ComImport, Guid("29038f61-3839-4626-91fd-086879011a05"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IDXGIAdapter1
    {
        void SetPrivateData(); void SetPrivateDataInterface(); void GetPrivateData(); void GetParent(); // IDXGIObject
        void EnumOutputs(); void GetDesc(); void CheckInterfaceSupport();                                // IDXGIAdapter
        [PreserveSig] int GetDesc1(out DXGI_ADAPTER_DESC1 desc);
    }
    [ComImport, Guid("770aae78-f26f-4dba-a829-253c83d1b387"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IDXGIFactory1
    {
        void SetPrivateData(); void SetPrivateDataInterface(); void GetPrivateData(); void GetParent(); // IDXGIObject
        void EnumAdapters(); void MakeWindowAssociation(); void GetWindowAssociation(); void CreateSwapChain(); void CreateSoftwareAdapter(); // IDXGIFactory
        [PreserveSig] int EnumAdapters1(uint index, out IDXGIAdapter1 adapter);
    }
    [DllImport("dxgi.dll")]
    static extern int CreateDXGIFactory1(ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IDXGIFactory1 factory);

    public sealed record Gpu(string Name, long MemoryMb);

    /// <summary>The system's graphics cards (no software renderer), Windows' default first.</summary>
    public static List<Gpu> Gpus()
    {
        var list = new List<Gpu>();
        if (!OperatingSystem.IsWindows()) return list; // (Linux: Automatic; Wine names the cards its own way)
        try
        {
            var iid = typeof(IDXGIFactory1).GUID;
            if (CreateDXGIFactory1(ref iid, out var factory) != 0) return list;
            for (uint i = 0; factory.EnumAdapters1(i, out var adapter) == 0; i++)
            {
                if (adapter.GetDesc1(out var d) == 0 && (d.Flags & 2 /* DXGI_ADAPTER_FLAG_SOFTWARE */) == 0 && list.All(g => g.Name != d.Description.Trim()))
                    list.Add(new Gpu(d.Description.Trim(), (long)(d.DedicatedVideoMemory >> 20)));
                Marshal.ReleaseComObject(adapter);
            }
            Marshal.ReleaseComObject(factory);
        }
        catch { } // (no list: the choice offers Automatic only)
        return list;
    }

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
        if (!OperatingSystem.IsWindows()) return Runner.Resolutions();
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
    public static bool Running => Platform.Linux
        ? Process.GetProcesses().Any(p => { try { return p.ProcessName is "fm3.exe" or "fm3"; } catch { return false; } })
        : Process.GetProcessesByName("fm3").Any(p =>
    {
        try { return string.Equals(p.MainModule?.FileName, Paths.Exe, StringComparison.OrdinalIgnoreCase); }
        catch { return true; } // can't look inside: assume it is ours
    });

    /// <summary>Starts fm3.exe; `extra` adds environment variables for this run (the benchmark's). The caller disposes the process.</summary>
    /// <summary>`mode`: "normal" (the Play tab: no VR, no triple screen), "vr" (the VR tab's headset mode and resolution),
    /// "triple" (the Triple screens tab's resolution and fullscreen).</summary>
    public static Process? Launch(IReadOnlyDictionary<string, string>? extra = null, string mode = "normal")
    {
        if (!File.Exists(Paths.Exe)) throw new InvalidOperationException($"fm3.exe isn't at {Paths.Exe}.");
        if (!File.Exists(Path.Combine(Paths.GameDir, "default.xex"))) throw new InvalidOperationException("No game files yet: choose the game folder (the extracted Disc 1) on the Play tab.");
        if (Running) throw new InvalidOperationException("The game is already running.");
        Directory.CreateDirectory(Paths.RunDir);
        // fm3.exe overwrites fm3.log: keep the last run's as fm3.prev.log, so two runs can be compared.
        try { if (File.Exists(Paths.Log)) File.Copy(Paths.Log, Path.ChangeExtension(Paths.Log, ".prev.log"), true); } catch (IOException) { }
        // Linux: through Wine/Proton (Runner.cs), the paths the game is given as Wine paths.
        ProcessStartInfo psi;
        if (Platform.Linux)
        {
            psi = Runner.StartInfo(Paths.Exe, [Runner.WinePath(Paths.GameDir)], Paths.RunDir);
            psi.Environment["FM3_USER_DIR"] = Runner.WinePath(Paths.User);
        }
        else
        {
            psi = new ProcessStartInfo(Paths.Exe) { WorkingDirectory = Paths.RunDir, UseShellExecute = false, CreateNoWindow = true };
            psi.ArgumentList.Add(Paths.GameDir);
            psi.Environment["FM3_USER_DIR"] = Paths.User;
        }
        var settings = GameSettings.Load();
        // The profile comes from settings.ini (the Profiles tab): an FM3_PROFILE left in the environment would win.
        psi.Environment.Remove("FM3_PROFILE");
        if (!settings.Anisotropy) psi.Environment["REGPU_NO_ANISO"] = "1";
        if (settings.HideShaderOverlay) psi.Environment["REGPU_NO_OVERLAY"] = "1";
        if (!settings.Timer) psi.Environment["FM3_NO_TIMER"] = "1";
        if (settings.PerfLog) psi.Environment["REGPU_PERF"] = "1";
        if (settings.DrawWorkers) psi.Environment["FM3_DRAW_THREAD_PIECES"] = "1";
        // The live debug server (127.0.0.1 only, tools\fm3ctl.ps1): status, captures, live switches.
        if (settings.DebugServer) psi.Environment["REGPU_DEBUG"] = "1";
        // This run's mode (the game reads FM3_VR, FM3_TRIPLE, FM3_RESOLUTION, FM3_FULLSCREEN before settings.ini).
        psi.Environment["FM3_VR"] = mode == "vr" ? settings.VrMode : "off";
        psi.Environment["FM3_TRIPLE"] = mode == "triple" ? "1" : "0";
        var res = mode == "vr" ? settings.VrResolution : mode == "triple" ? settings.TripleResolution : "same";
        if (res != "same") psi.Environment["FM3_RESOLUTION"] = res;
        if (mode == "triple") psi.Environment["FM3_FULLSCREEN"] = settings.TripleFullscreen ? "1" : "0";
        if (extra is not null)
            foreach (var (k, v) in extra) psi.Environment[k] = v;
        return Process.Start(psi);
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
