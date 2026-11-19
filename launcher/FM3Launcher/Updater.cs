using System.IO.Compression;
using System.Net.Http.Headers;
using System.Reflection;
using System.Text.Json;

namespace FM3Launcher;

/// <summary>
/// Updates from GitHub releases, as FM2's and PGR4's launchers do: the newest release's FM3-ReGPU-x.y.z-patch.zip
/// (installer\build-installer.ps1) is downloaded and unpacked over the install. Files in use (the running launcher's)
/// are renamed to *.old first, since Windows lets a running exe be renamed but not overwritten; the next start deletes them.
/// </summary>
public static class Updater
{
    public const string Repo = "braaap-stututu/FM3-ReGPU";
    public sealed record Release(string Version, string Name, string Notes, string Page, string? PatchUrl, long PatchBytes, DateTime Published,
        string? PatchSha256 = null);
    /// <summary>The install's file list (installer\build-installer.ps1 writes it into the install and each patch zip): files the
    /// previous release had and the new one doesn't are removed after an update.</summary>
    public const string Manifest = "install-manifest.txt";

    static readonly HttpClient Http = MakeClient();

    static HttpClient MakeClient()
    {
        var h = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        h.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("FM3Launcher", Normalise(InstalledVersion())));
        h.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
        return h;
    }

    /// <summary>The installed version: the launcher's (VERSION at build time), which ships with fm3.exe in each release.</summary>
    public static string InstalledVersion() =>
        typeof(Updater).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0]
        ?? typeof(Updater).Assembly.GetName().Version?.ToString(3) ?? "0.0.0";

    /// <summary>The newest published release. Not /releases/latest: that skips pre-releases, and FM3's are betas.</summary>
    public static async Task<Release> LatestAsync()
    {
        using var doc = JsonDocument.Parse(await Http.GetStringAsync($"https://api.github.com/repos/{Repo}/releases?per_page=10"));
        foreach (var r in doc.RootElement.EnumerateArray())
        {
            if (r.GetProperty("draft").GetBoolean()) continue;
            string tag = r.GetProperty("tag_name").GetString() ?? "";
            string? url = null, sha = null;
            long bytes = 0;
            foreach (var a in r.GetProperty("assets").EnumerateArray())
            {
                var name = a.GetProperty("name").GetString() ?? "";
                if (name.EndsWith("-patch.zip", StringComparison.OrdinalIgnoreCase))
                {
                    url = a.GetProperty("browser_download_url").GetString();
                    bytes = a.GetProperty("size").GetInt64();
                    // GitHub's own SHA-256 of the asset ("sha256:<hex>"), checked after the download
                    if (a.TryGetProperty("digest", out var d) && d.ValueKind == JsonValueKind.String && d.GetString() is { } ds &&
                        ds.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase))
                        sha = ds[7..].ToLowerInvariant();
                }
            }
            if (Platform.Linux) url = null; // the patch is the Windows build: Linux takes the release's tarball
            return new Release(tag.TrimStart('v', 'V'), r.GetProperty("name").GetString() is { Length: > 0 } n ? n : tag,
                r.GetProperty("body").GetString() ?? "",
                r.GetProperty("html_url").GetString() ?? $"https://github.com/{Repo}/releases", url, bytes,
                r.TryGetProperty("published_at", out var p) && p.ValueKind == JsonValueKind.String ? p.GetDateTime() : DateTime.MinValue, sha);
        }
        throw new InvalidOperationException("There are no releases yet.");
    }

    /// <summary>x.y.z compared first; on a tie a release beats its own pre-release (0.42.0 over 0.42.0-beta).
    /// A local build's -hash suffix doesn't count as a pre-release.</summary>
    public static bool IsNewer(string latest, string installed)
    {
        if (!Version.TryParse(Normalise(latest), out var a) || !Version.TryParse(Normalise(installed), out var b)) return false;
        if (a != b) return a > b;
        return Pre(installed) && !Pre(latest);
    }

    static bool Pre(string v) => v.Contains("-alpha", StringComparison.OrdinalIgnoreCase) || v.Contains("-beta", StringComparison.OrdinalIgnoreCase)
                                 || v.Contains("-rc", StringComparison.OrdinalIgnoreCase);

    static string Normalise(string v)
    {
        var core = new string(v.TakeWhile(c => char.IsDigit(c) || c == '.').ToArray()).Trim('.');
        if (core.Length == 0) return "0.0";
        return core.Count(c => c == '.') == 0 ? core + ".0" : core;
    }

    /// <summary>An installed copy (fm3.exe beside the launcher folder), not a development checkout.</summary>
    public static bool Installed => File.Exists(Path.Combine(Paths.Root, "fm3.exe")) && File.Exists(Path.Combine(Paths.Root, "launcher", Path.GetFileName(Environment.ProcessPath ?? "")));

    public sealed record Applied(int Files, bool LauncherChanged);

    /// <summary>Downloads the patch and unpacks it over the install. progress: 0..1 while downloading.</summary>
    public static async Task<Applied> DownloadAndApplyAsync(Release release, Action<double, string> progress)
    {
        if (release.PatchUrl is null) throw new InvalidOperationException("This release has no patch for this PC. Use the download on the release page.");
        if (!Installed) throw new InvalidOperationException("This launcher runs from a build folder, not an install: update with git and rebuild.");
        if (Game.Running) throw new InvalidOperationException("Close the game first: its files are about to be replaced.");
        var zipPath = Path.Combine(Path.GetTempPath(), $"FM3-ReGPU-{release.Version}-patch.zip");
        using (var response = await Http.GetAsync(release.PatchUrl, HttpCompletionOption.ResponseHeadersRead))
        {
            response.EnsureSuccessStatusCode();
            long total = response.Content.Headers.ContentLength ?? release.PatchBytes;
            await using var input = await response.Content.ReadAsStreamAsync();
            await using var output = File.Create(zipPath);
            var buffer = new byte[1 << 16];
            long done = 0;
            int n;
            while ((n = await input.ReadAsync(buffer)) > 0)
            {
                await output.WriteAsync(buffer.AsMemory(0, n));
                done += n;
                if (total > 0) progress(Math.Min(1, (double)done / total), $"Downloading {done / 1048576.0:0.0} of {total / 1048576.0:0.0} MB");
            }
        }
        // The download checked against GitHub's SHA-256 of the asset before anything is unpacked (a cut or corrupted
        // download would otherwise be written over the install).
        if (release.PatchSha256 is { } want)
        {
            progress(1, "Checking the download");
            string got;
            using (var f = File.OpenRead(zipPath))
                got = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(f)).ToLowerInvariant();
            if (got != want)
            {
                try { File.Delete(zipPath); } catch { }
                throw new InvalidDataException("The download is damaged (its checksum doesn't match GitHub's). Try again.");
            }
        }
        progress(1, "Installing");
        return Apply(zipPath);
    }

    public static Applied Apply(string zipPath, bool deleteZip = true)
    {
        var root = Path.GetFullPath(Paths.Root) + Path.DirectorySeparatorChar;
        var self = Path.GetFullPath(AppContext.BaseDirectory);
        int files = 0;
        bool launcher = false;
        var oldManifest = ReadManifest(Path.Combine(root, Manifest));
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using (var zip = ZipFile.OpenRead(zipPath))
        {
            // every entry checked before anything is touched: names must stay inside the install
            var entries = new List<(ZipArchiveEntry Entry, string Target)>();
            foreach (var e in zip.Entries)
            {
                if (e.FullName.EndsWith('/') || e.FullName.EndsWith('\\')) continue;
                var name = e.FullName.Replace('\\', '/');
                if (name.Any(char.IsControl)) throw new InvalidDataException($"The patch has a broken file name ({Uri.EscapeDataString(name)}).");
                var target = Path.GetFullPath(Path.Combine(root, name));
                if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException($"The patch writes outside the install ({name}).");
                entries.Add((e, target));
                names.Add(name);
            }
            foreach (var (e, target) in entries)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                bool ours = target.StartsWith(self, StringComparison.OrdinalIgnoreCase);
                var temp = target + ".new";
                e.ExtractToFile(temp, true);
                if (File.Exists(target) && SameFile(temp, target)) { File.Delete(temp); continue; } // unchanged
                if (File.Exists(target))
                {
                    if (ours) { RenameAside(target); launcher = true; } // the running launcher's files
                    else
                    {
                        try { File.Move(temp, target, true); files++; continue; }
                        catch (Exception x) when (x is UnauthorizedAccessException or IOException) { RenameAside(target); } // in use
                    }
                }
                File.Move(temp, target, true);
                files++;
            }
        }
        // Files the previous release installed and this one no longer has (a renamed or dropped DLL): removed, or renamed
        // aside when in use. Only names from the old manifest, and only inside the install; none when either side has no list.
        if (oldManifest.Count > 0 && names.Contains(Manifest))
            foreach (var name in oldManifest)
            {
                if (names.Contains(name)) continue;
                var target = Path.GetFullPath(Path.Combine(root, name));
                if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase) || !File.Exists(target)) continue;
                try { File.Delete(target); }
                catch (Exception x) when (x is UnauthorizedAccessException or IOException) { try { RenameAside(target); } catch { } }
                files++;
            }
        if (deleteZip) try { File.Delete(zipPath); } catch { }
        return new Applied(files, launcher);
    }

    static List<string> ReadManifest(string path)
    {
        try
        {
            if (!File.Exists(path)) return [];
            return File.ReadAllLines(path).Select(l => l.Trim().Replace('\\', '/')).Where(l => l.Length > 0 && !l.Any(char.IsControl)).ToList();
        }
        catch { return []; }
    }

    static void RenameAside(string target)
    {
        var old = target + ".old";
        if (File.Exists(old)) File.Delete(old);
        File.Move(target, old);
    }

    static bool SameFile(string a, string b)
    {
        var fa = new FileInfo(a); var fb = new FileInfo(b);
        if (fa.Length != fb.Length) return false;
        using var sa = File.OpenRead(a);
        using var sb = new FileStream(b, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var h = System.Security.Cryptography.SHA256.Create();
        return h.ComputeHash(sa).AsSpan().SequenceEqual(h.ComputeHash(sb));
    }

    /// <summary>Leftovers of an update: files renamed while in use, and writes that failed half way.</summary>
    public static void CleanUp()
    {
        if (!Installed) return; // (never in a development checkout, where Paths.Root is the repository)
        try
        {
            // the launcher's folder, and the install's top folder (fm3.exe and its DLLs; not the game's files)
            foreach (var (dir, depth) in new[] { (AppContext.BaseDirectory, SearchOption.AllDirectories), (Paths.Root, SearchOption.TopDirectoryOnly) })
                if (Directory.Exists(dir))
                    foreach (var f in Directory.EnumerateFiles(dir, "*.*", depth))
                        if (f.EndsWith(".old", StringComparison.OrdinalIgnoreCase) || f.EndsWith(".new", StringComparison.OrdinalIgnoreCase))
                            try { File.Delete(f); } catch { }
        }
        catch { }
    }
}
