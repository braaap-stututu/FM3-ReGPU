using System.IO.Compression;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace FM3Launcher;

/// <summary>
/// Shader sharing, as FM2:ReGPU does it: the game's pipelines.manifest (which shader and render-state combinations it
/// has built: hashes only, no game data, nothing about the player) goes to the shaders server (server\shaders, a
/// Cloudflare Worker), which keeps one file per install in a private GitHub repo. tools/pipeline_seed.py merges those
/// files into src/pipeline_seed.h, so the next release builds them all on the first boot instead of stuttering in a
/// race. Off until the server is set up (Endpoint), then asked once on the launcher's first start (the page's
/// notice), and a switch on the Settings tab after that.
/// </summary>
public static class ShaderShare
{
    // The deployed Worker's /v1/pipelines URL. Empty: sharing is off and the launcher never asks.
    // FM3_SHADERS_URL overrides it (a local `wrangler dev` for testing).
    const string DefaultEndpoint = "";
    static readonly string Endpoint = Environment.GetEnvironmentVariable("FM3_SHADERS_URL") is { Length: > 0 } e ? e : DefaultEndpoint;
    public static bool Configured => Endpoint.Length > 0;

    const long MaxManifest = 2 << 20;
    static readonly Regex Line = new(@"^(P( [0-9a-f]{1,16}){6}( [0-9a-f:.]{1,40})*|V [0-9a-f]{1,16} [0-9a-f]{1,16}( [0-9a-f:.]{1,40})+)$", RegexOptions.Compiled);
    static string Manifest => Path.Combine(Paths.User, "pipelines.manifest");
    static readonly HttpClient Http = MakeClient();
    static Timer? _timer;
    static int _busy;

    static HttpClient MakeClient()
    {
        var h = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };
        h.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("FM3Launcher", Version()));
        return h;
    }

    static string Version() =>
        System.Reflection.CustomAttributeExtensions.GetCustomAttribute<System.Reflection.AssemblyInformationalVersionAttribute>(typeof(ShaderShare).Assembly)
            ?.InformationalVersion.Split('+')[0] ?? "0.0.0";

    /// <summary>The player's choice (shader-sharing.json in the user folder, not the game's settings.ini).</summary>
    public sealed class Options
    {
        public bool Share { get; set; }
        public bool Asked { get; set; }
        public string InstallId { get; set; } = "";
        public DateTime LastSent { get; set; }

        static string File => Path.Combine(Paths.User, "shader-sharing.json");

        public static Options Load()
        {
            Options o;
            try { o = JsonSerializer.Deserialize<Options>(System.IO.File.ReadAllText(File)) ?? new(); }
            catch { o = new(); }
            if (o.InstallId.Length == 0)
            {
                // Random: one PC's uploads replace each other without saying whose PC it is.
                o.InstallId = Guid.NewGuid().ToString("N")[..12];
                o.Save();
            }
            return o;
        }

        public void Save()
        {
            Directory.CreateDirectory(Paths.User);
            System.IO.File.WriteAllText(File, JsonSerializer.Serialize(this));
        }
    }

    public static object State()
    {
        var o = Options.Load();
        return new { configured = Configured, on = o.Share, asked = o.Asked };
    }

    /// <summary>The first-start notice's answer, or the Settings switch.</summary>
    public static void Set(bool on)
    {
        var o = Options.Load();
        o.Share = on;
        o.Asked = true;
        o.Save();
        if (on) _ = SendAsync();
    }

    /// <summary>Checks now and every few minutes: a manifest the game has added to since the last upload goes once the
    /// game has closed (it writes the file while it runs).</summary>
    public static void Start()
    {
        if (!Configured) return;
        _timer ??= new Timer(_ => _ = SendAsync(), null, TimeSpan.FromSeconds(10), TimeSpan.FromMinutes(3));
    }

    /// <summary>Never throws: a failed upload tries again next time.</summary>
    public static async Task SendAsync()
    {
        if (!Configured || Interlocked.Exchange(ref _busy, 1) == 1) return;
        try
        {
            var o = Options.Load();
            if (!o.Share || !o.Asked || !File.Exists(Manifest) || Game.Running) return;
            var info = new FileInfo(Manifest);
            if (info.Length > MaxManifest || info.LastWriteTimeUtc <= o.LastSent) return;
            // Only well-formed manifest lines leave the PC.
            var lines = File.ReadAllLines(Manifest).Select(l => l.Trim()).Where(l => Line.IsMatch(l)).Distinct().ToList();
            if (lines.Count == 0) return;
            using var res = await Http.PostAsJsonAsync(Endpoint, new
            {
                version = Version(),
                installId = o.InstallId,
                pipelines = Gzip64(string.Join("\n", lines) + "\n"),
            });
            if (!res.IsSuccessStatusCode) return;
            o.LastSent = info.LastWriteTimeUtc;
            o.Save();
        }
        catch { }
        finally { Interlocked.Exchange(ref _busy, 0); }
    }

    static string Gzip64(string text)
    {
        using var ms = new MemoryStream();
        using (var gz = new GZipStream(ms, CompressionLevel.SmallestSize))
            gz.Write(Encoding.UTF8.GetBytes(text));
        return Convert.ToBase64String(ms.ToArray());
    }
}
