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
    readonly PhotinoWindow _window;

    public Bridge(PhotinoWindow window)
    {
        _window = window;
        window.RegisterWebMessageReceivedHandler((_, text) => OnMessage(text));
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
    void Post(object o) => _window.SendWebMessage(JsonSerializer.Serialize(o, Json));
    // A line of JSON from the wheel probe (Wheel.cs), passed on as it is.
    void PushWheel(string json) => _window.SendWebMessage("{\"event\":\"wheel\",\"data\":" + json + "}");

    static readonly (string, string[])[] ZipFilter = [("Profile zip", ["*.zip"])];

    async Task<object?> Run(string cmd, JsonNode? a)
    {
        switch (cmd)
        {
            case "state": return State();
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
                    var picked = await _window.ShowOpenFolderAsync("Choose the game folder (the extracted Disc 1, with default.xex and Media)",
                        Directory.Exists(Paths.GameDir) ? Paths.GameDir : null, false);
                    if (picked is { Length: > 0 })
                    {
                        await Task.Run(() => Xex.Require(picked[0]));
                        var s = GameSettings.Load();
                        s.GameFolder = picked[0];
                        s.Save();
                    }
                    return State();
                }
            // "Check game files": the only way the game folder's default.xex is hashed besides a pick.
            case "xex.check":
                await Task.Run(() => Xex.CheckNow(Paths.GameDir));
                return State();
            case "launch":
                if (a?["settings"].Deserialize<GameSettings>(Json) is { } toSave) SaveFromPage(toSave);
                Game.Launch();
                return null;
            case "shaders.delete":
                ShaderCache.Delete();
                return State();
            case "shaders.rebuild":
                ShaderCache.Delete();
                Game.Launch();
                return State();
            case "open":
                {
                    var which = a?.GetValue<string>();
                    var path = which switch
                    {
                        "user" => Directory.CreateDirectory(Paths.User).FullName,
                        "install" => Paths.Root,
                        "game" => Paths.GameDir,
                        "log" => Directory.CreateDirectory(Path.GetDirectoryName(Paths.Log)!).FullName,
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
                    var files = await _window.ShowOpenFileAsync("Import a profile", null, false, ZipFilter);
                    if (files is not { Length: > 0 }) return null;
                    var name = Profiles.Restore(files[0], a?["asName"]?.GetValue<string>());
                    return new { name, state = ProfilesState() };
                }
            case "profiles.export":
                {
                    var name = Str(a, "name");
                    var file = await _window.ShowSaveFileAsync("Export profile", Profiles.FolderName(name) + ".zip", ZipFilter);
                    if (string.IsNullOrEmpty(file)) return false;
                    Profiles.Export(name, file);
                    return true;
                }
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
            version = typeof(Bridge).Assembly.GetName().Version?.ToString(3) ?? "0.1.0",
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
                aspects = Options.Aspects.Select(x => new { value = x.Value, name = x.Name }),
                fps = Options.Fps,
                msaa = Options.Msaa.Select(x => new { value = x.Value, name = x.Name }),
                upscalers = Options.Upscalers.Select(x => new { value = x.Value, name = x.Name }),
                upscale = Options.Upscale.Select(x => new { value = x.Value, name = x.Name }),
                sharpening = Options.Sharpening.Select(x => new { value = x.Value, name = x.Name }),
                languages = Options.InstalledLanguages().Select(l => new { id = l.Id, code = l.Code, name = l.Name }),
                missingLanguages = Directory.Exists(stringTables)
                    ? new[] { "CZ", "HU", "MX" }.Where(c => File.Exists(Path.Combine(stringTables, c + ".zip"))).ToArray() : [],
            },
            shaders = ShaderCache.Get(),
        };
    }
}
