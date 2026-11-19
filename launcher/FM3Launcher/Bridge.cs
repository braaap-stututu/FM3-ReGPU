using System.Text.Json;
using System.Text.Json.Nodes;
using Photino.NET;

namespace FM3Launcher;

/// <summary>
/// The page's calls: { id, cmd, args } over the window's message channel (wwwroot/host.js), answered with
/// { id, ok, result | error }.
/// </summary>
public sealed class Bridge
{
    static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    readonly IShell _window; // the launcher's window or the browser page (Shell.cs)
    Updater.Release? _latest; // the newest GitHub release, once asked (About tab, and quietly at start)

    public Bridge(IShell shell)
    {
        _window = shell;
        shell.OnMessage(OnMessage);
        ShaderShare.Start();
    }

    async void OnMessage(string text)
    {
        JsonNode? msg;
        try { msg = JsonNode.Parse(text); } catch { return; }
        var id = msg?["id"]?.GetValue<int>() ?? 0;
        var cmd = msg?["cmd"]?.GetValue<string>() ?? "";
        var args = msg?["args"];
        try
        {
            var result = await Run(cmd, args);
            Post(new { id, ok = true, result });
        }
        catch (Exception ex)
        {
            Post(new { id, ok = false, error = ex.Message });
        }
    }

    // Photino takes messages from any thread.
    void Post(object o) => _window.Send(JsonSerializer.Serialize(o, Json));
    // A line of JSON from the wheel probe (Wheel.cs), passed on as it is.
    void PushWheel(string json) => _window.Send("{\"event\":\"wheel\",\"data\":" + json + "}");

    static readonly (string, string[])[] ZipFilter = [("Profile zip", ["*.zip"])];
    static readonly (string, string[])[] IsoFilter = [("Disc image", ["*.iso"]), ("All files", ["*.*"])];

    object UpdateInfo()
    {
        var installed = Updater.InstalledVersion();
        return new
        {
            installed,
            latest = _latest?.Version,
            name = _latest?.Name,
            notes = _latest?.Notes,
            page = _latest?.Page ?? $"https://github.com/{Updater.Repo}/releases",
            published = _latest?.Published,
            patchBytes = _latest?.PatchBytes ?? 0,
            hasPatch = _latest?.PatchUrl is not null && Updater.Installed,
            available = _latest is not null && Updater.IsNewer(_latest.Version, installed),
        };
    }

    async Task<object?> Run(string cmd, JsonNode? a)
    {
        switch (cmd)
        {
            case "state": return State();
            // Shader sharing (Sharing.cs): the first-start notice's answer and the Settings switch.
            case "share.set":
                ShaderShare.Set(a?["on"]?.GetValue<bool>() == true);
                return State();
            case "save":
                SaveFromPage(a.Deserialize<GameSettings>(Json)!);
                return State();
            // The game folder is changed only by a pick or Reset, never by the page's copy of the settings.
            case "game.reset":
                {
                    var s = GameSettings.Load();
                    s.GameFolder = "";
                    s.Save();
                    return State();
                }
            case "game.browse":
                {
                    var picked = await _window.PickFolder("Choose the game folder (the extracted Disc 1, with default.xex and Media)", Directory.Exists(Paths.GameDir) ? Paths.GameDir : null);
                    if (picked is { Length: > 0 })
                    {
                        await Task.Run(() => Xex.Require(picked[0]));
                        var s = GameSettings.Load();
                        s.GameFolder = picked[0];
                        s.Save();
                    }
                    return State();
                }
            // Disc 1 from its image (Disc1.cs): extracted into the game folder next to the launcher, which becomes the
            // game folder; progress as "disc1" events.
            case "game.iso":
                {
                    var files = await _window.PickFile("Choose the Disc 1 image (Play Disc)", IsoFilter);
                    if (files is not { Length: > 0 }) return null;
                    await Task.Run(() => Disc1.Install(files[0], line => Post(new { @event = "disc1", data = new { text = line } })));
                    var s = GameSettings.Load();
                    s.GameFolder = "";
                    s.Save();
                    await Task.Run(() => Xex.CheckNow(Paths.GameDir));
                    return State();
                }
            // "Check game files": the only way the game folder's default.xex is hashed besides a pick.
            case "xex.check":
                await Task.Run(() => Xex.CheckNow(Paths.GameDir));
                return State();
            // Disc 2's content packages (Disc2.cs): from the disc image, or a folder; progress as "disc2" events.
            case "disc2.iso":
            case "disc2.folder":
                {
                    string? source;
                    if (cmd == "disc2.iso")
                    {
                        var files = await _window.PickFile("Choose the Disc 2 image (Content Install Disc)", IsoFilter);
                        source = files is { Length: > 0 } ? files[0] : null;
                    }
                    else
                    {
                        var dirs = await _window.PickFolder("Choose the extracted Disc 2 folder, or a folder with the content packages", null);
                        source = dirs is { Length: > 0 } ? dirs[0] : null;
                    }
                    if (source is null) return null;
                    await Task.Run(() => Disc2.Install(source, line => Post(new { @event = "disc2", data = new { text = line } })));
                    return State();
                }
            case "launch":
                if (a?["settings"].Deserialize<GameSettings>(Json) is { } toSave) SaveFromPage(toSave);
                // (The Play tab, or the VR / Triple screens tab's own Play button.)
                Game.Launch(null, a?["mode"]?.GetValue<string>() is "vr" or "triple" ? a["mode"]!.GetValue<string>() : "normal")?.Dispose();
                return null;
            case "benchmark.run":
                {
                    var seconds = a?["seconds"]?.GetValue<int>() ?? 120;
                    var result = await Benchmark.Run(seconds, text => Post(new { @event = "benchmark", data = new { text } }));
                    return new { result, history = Benchmark.List() };
                }
            case "benchmark.list": return Benchmark.List();
            case "shaders.delete":
                ShaderCache.Delete();
                return State();
            case "shaders.rebuild":
                ShaderCache.Delete();
                Game.Launch()?.Dispose();
                return State();
            case "open":
                {
                    var which = a?.GetValue<string>();
                    var path = which switch
                    {
                        "user" => Directory.CreateDirectory(Paths.User).FullName,
                        "install" => Paths.Root,
                        "game" => Paths.GameDir,
                        "dlc" => Directory.CreateDirectory(Disc2.Dir).FullName,
                        "log" => Directory.CreateDirectory(Path.GetDirectoryName(Paths.Log)!).FullName,
                        "bench" => Directory.CreateDirectory(Benchmark.Dir).FullName,
                        _ => throw new ArgumentException("Unknown folder."),
                    };
                    Platform.OpenFolder(path);
                    return null;
                }
            case "profiles.list": return ProfilesState();
            case "profiles.use":
                Profiles.SetActive(Str(a, "name"));
                return ProfilesState();
            case "profiles.create":
                Profiles.Create(Str(a, "name"));
                return ProfilesState();
            case "profiles.duplicate":
                Profiles.Duplicate(Str(a, "name"), Str(a, "newName"));
                return ProfilesState();
            case "profiles.rename":
                Profiles.Rename(Str(a, "name"), Str(a, "newName"));
                return ProfilesState();
            case "profiles.delete":
                {
                    var b = Profiles.Delete(Str(a, "name"));
                    return new { backup = b.File, state = ProfilesState() };
                }
            case "profiles.backup":
                {
                    var b = Profiles.MakeBackup(Str(a, "name"));
                    return new { backup = b.File, state = ProfilesState() };
                }
            case "profiles.restore":
                {
                    var name = Profiles.Restore(Str(a, "file"), a?["asName"]?.GetValue<string>());
                    return new { name, state = ProfilesState() };
                }
            case "profiles.import":
                {
                    var files = await _window.PickFile("Import a profile", ZipFilter);
                    if (files is not { Length: > 0 }) return null;
                    var name = Profiles.Restore(files[0], a?["asName"]?.GetValue<string>());
                    return new { name, state = ProfilesState() };
                }
            case "profiles.export":
                {
                    var name = Str(a, "name");
                    var file = await _window.PickSaveFile("Export profile", Profiles.FolderName(name) + ".zip", ZipFilter);
                    if (string.IsNullOrEmpty(file)) return false;
                    Profiles.Export(name, file);
                    return true;
                }
            case "profiles.credits":
                Profiles.RequestCredits(Str(a, "name"), Str(a, "mode"), a?["amount"]?.GetValue<long>() ?? 0);
                return ProfilesState();
            case "profiles.cancelEdit":
                Profiles.CancelEdit(Str(a, "name"));
                return ProfilesState();
            case "profiles.openBackups":
                Platform.OpenFolder(Directory.CreateDirectory(Profiles.Backups).FullName);
                return null;
            // The Wheel stuff tab (Wheel.cs): the wheel_* settings, and the live view of the wheels while the tab is open.
            case "wheel.settings": return Wheel.Load();
            case "wheel.save":
                Wheel.Save(a.Deserialize<Dictionary<string, string>>(Json) ?? []);
                return Wheel.Load();
            case "wheel.start":
                Wheel.Start(PushWheel);
                return null;
            case "wheel.stop":
                Wheel.Stop();
                return null;
            case "wheel.fftest":
                if (Game.Running) throw new InvalidOperationException("Close the game first: it holds the wheel's force feedback while it runs.");
                Wheel.TestForceFeedback();
                return null;
            // Updates from GitHub releases (Updater.cs), as in FM2's and PGR4's launchers.
            case "update.check":
                _latest = await Updater.LatestAsync();
                return UpdateInfo();
            case "update.apply":
                {
                    _latest ??= await Updater.LatestAsync();
                    var applied = await Updater.DownloadAndApplyAsync(_latest, (p, text) => Post(new { @event = "update", data = new { p, text } }));
                    return new { applied.Files, applied.LauncherChanged, update = UpdateInfo() };
                }
            case "restart":
                {
                    // the new launcher waits for this one to let go of the single-instance mutex (Program.cs)
                    var psi = new System.Diagnostics.ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false };
                    foreach (var arg in Environment.GetCommandLineArgs().Skip(1).Where(x => x != "--after-update")) psi.ArgumentList.Add(arg);
                    psi.ArgumentList.Add("--after-update");
                    System.Diagnostics.Process.Start(psi);
                    _ = Task.Delay(300).ContinueWith(_ => Environment.Exit(0));
                    return null;
                }
            // The ReGPU Discord server: the same invite as FM2:ReGPU's launcher.
            case "discord":
                Platform.OpenUrl("https://discord.gg/pVEpmSk2G3");
                return null;
            case "url":
                {
                    var url = a?.GetValue<string>() ?? "";
                    if (!url.StartsWith("https://github.com/", StringComparison.OrdinalIgnoreCase)) throw new ArgumentException("Only GitHub links open from here.");
                    Platform.OpenUrl(url);
                    return null;
                }
            default:
                throw new ArgumentException($"Unknown command {cmd}.");
        }
    }

    static string Str(JsonNode? a, string key) => a?[key]?.GetValue<string>() ?? throw new ArgumentException($"Missing {key}.");

    static object ProfilesState() => new { profiles = Profiles.List(), backups = Profiles.ListBackups(), running = Game.Running };

    // The page's copy of the settings, with the game folder kept as the backend has it (a pick may have changed it
    // after the page last looked).
    static void SaveFromPage(GameSettings fromPage)
    {
        fromPage.GameFolder = GameSettings.Load().GameFolder;
        fromPage.Save();
    }

    static object State()
    {
        var settings = GameSettings.Load();
        var stringTables = Path.Combine(Paths.GameDir, "Media", "StringTables");
        return new
        {
            settings,
            share = ShaderShare.State(),
            // The release name from VERSION (e.g. 0.41.0-beta); AssemblyVersion drops the -beta part.
            version = System.Reflection.CustomAttributeExtensions.GetCustomAttribute<System.Reflection.AssemblyInformationalVersionAttribute>(typeof(Bridge).Assembly)?.InformationalVersion.Split('+')[0]
                ?? typeof(Bridge).Assembly.GetName().Version?.ToString(3) ?? "0.0.0",
            paths = new { root = Paths.Root, user = Paths.User, exe = Paths.Exe, gameDefault = Paths.GameDirDefault, gameDir = Paths.GameDir, gameFromEnv = Paths.GameDirFromEnv, log = Paths.Log },
            checks = new
            {
                exe = File.Exists(Paths.Exe),
                game = File.Exists(Path.Combine(Paths.GameDir, "default.xex")),
                media = Directory.Exists(Path.Combine(Paths.GameDir, "Media")),
                xex = Xex.Status(Paths.GameDir),
                running = Game.Running,
            },
            options = new
            {
                resolutions = Options.Resolutions(),
                gpus = Options.Gpus().Select(g => new { value = g.Name, name = g.MemoryMb >= 1024 ? $"{g.Name} ({g.MemoryMb / 1024.0:0.#} GB)" : $"{g.Name} (uses system memory)" }),
                aspects = Options.Aspects.Select(x => new { value = x.Value, name = x.Name }),
                fps = Options.Fps,
                msaa = Options.Msaa.Select(x => new { value = x.Value, name = x.Name }),
                aa = Options.Aa.Select(x => new { value = x.Value, name = x.Name }),
                vrModes = Options.VrModes.Select(x => new { value = x.Value, name = x.Name }),
                tripleResolutions = Options.TripleResolutions(),
                tripleFov = Options.TripleFov.Select(x => new { value = x.Value, name = x.Name }),
                vrEyeLimit = Options.VrEyeLimit.Select(x => new { value = x.Value, name = x.Name }),
                ssaa = Options.Ssaa.Select(x => new { value = x.Value, name = x.Name }),
                upscalers = Options.Upscalers.Select(x => new { value = x.Value, name = x.Name }),
                upscale = Options.Upscale.Select(x => new { value = x.Value, name = x.Name }),
                sharpening = Options.Sharpening.Select(x => new { value = x.Value, name = x.Name }),
                languages = Options.InstalledLanguages().Select(l => new { id = l.Id, code = l.Code, name = l.Name }),
                missingLanguages = Directory.Exists(stringTables)
                    ? new[] { "CZ", "HU", "MX" }.Where(c => File.Exists(Path.Combine(stringTables, c + ".zip"))).ToArray() : [],
            },
            shaders = ShaderCache.Get(),
            disc2 = Disc2.Status(),
            linux = Platform.Linux ? new
            {
                runners = Runner.Choices().Select(c => new { value = c.Value, name = c.Name, available = c.Available }),
                resolved = Runner.Resolve(settings).Way,
                detail = Runner.Resolve(settings).Detail,
                prefix = settings.LinuxPrefix is { Length: > 0 } pre ? pre : Runner.DefaultPrefix,
                protons = Runner.Protons(),
            } : null,
        };
    }
}
