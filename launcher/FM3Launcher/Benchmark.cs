using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace FM3Launcher;

/// <summary>
/// The benchmark race (session 33): FM3's own race AI drives the player's car through the reference career race (the
/// test grid's event: Ladera Test Track, 3 laps, 7 opponents) and the light performance log measures. fm3.exe does the
/// race by itself with FM3_BENCHMARK=&lt;seconds&gt; (src/hooks/grid_test.cpp: route without the freeze, the AI on, a
/// "benchmark: measuring N s" line 10 s after GO, "benchmark: done" and the window closed after N s). It runs on a copy
/// of the Baseline profile (the route's event is Baseline's next career race), never on the real one: a finished race
/// would write into that save. Results are kept in the user folder's bench\ (one .json each, runs.csv).
/// </summary>
public static partial class Benchmark
{
    public const string ProfileName = "Benchmark";
    public static string Dir => Path.Combine(Paths.User, "bench");

    public sealed record Result(string Time, string Profile, string Resolution, int Msaa, int MaxFps, string Options, int Seconds,
        double FpsAvg, double FpsLowestSecond, double P99Ms, double WorstMs, int LongFrames, int LongFramesOver10, int Draws,
        double GpuMs, double DrawThreadBusy);

    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public static async Task<Result> Run(int seconds, Action<string> progress)
    {
        seconds = Math.Clamp(seconds, 20, 600);
        if (Game.Running) throw new InvalidOperationException("Close the game first: the benchmark starts it by itself.");
        var root = Profiles.Root;
        var source = Directory.Exists(Path.Combine(root, "Baseline")) ? "Baseline" : GameSettings.Load().Profile;
        var from = Path.Combine(root, Profiles.FolderName(source));
        if (!Directory.Exists(from)) throw new InvalidOperationException($"There is no {source} profile to copy for the benchmark.");
        var bench = Path.Combine(root, ProfileName);
        if (Directory.Exists(bench)) Directory.Delete(bench, true);
        Profiles.CopyDir(from, bench);
        try
        {
            progress($"Starting the game on a copy of {source}…");
            using var game = Game.Launch(new Dictionary<string, string>
            {
                ["FM3_PROFILE"] = ProfileName,
                ["FM3_BENCHMARK"] = seconds.ToString(CultureInfo.InvariantCulture),
            }) ?? throw new InvalidOperationException("The game didn't start.");
            // Boot and menus ~25 s, 10 s settling, the window, closing: give up well after that.
            var limit = TimeSpan.FromSeconds(seconds + 240);
            var watch = Stopwatch.StartNew();
            while (!game.HasExited)
            {
                if (watch.Elapsed > limit)
                {
                    try { game.Kill(); } catch (InvalidOperationException) { }
                    throw new TimeoutException("The benchmark race didn't finish in time; the game was closed. fm3.log has the details.");
                }
                progress(Phase(watch.Elapsed.TotalSeconds, seconds));
                await Task.Delay(1000);
            }
            progress("Reading the results…");
            var result = Parse(File.ReadAllText(Paths.Log), seconds, source, GameSettings.Load());
            Save(result);
            return result;
        }
        finally
        {
            try { if (Directory.Exists(bench)) Directory.Delete(bench, true); } catch (IOException) { }
        }
    }

    static string Phase(double t, int seconds)
    {
        // What the log will show, roughly: the route ~25 s to GO, 10 s settling, the window.
        if (t < 25) return "Loading the race (the AI drives your car)…";
        if (t < 35) return "Race started: settling in…";
        var left = Math.Max(0, (int)(35 + seconds - t));
        return left > 0 ? $"Measuring: about {left} s left" : "Finishing…";
    }

    [GeneratedRegex(@"\[\s*([\d.]+)\] perf lite: ([\d.]+) fps, ([\d.]+) ms/frame \(p99 ([\d.]+) ms, max ([\d.]+) ms\) \| (\d+) draws/frame \| GPU ([\d.]+) ms/frame \| draw thread (-?\d+)% busy")]
    private static partial Regex LiteLine();
    [GeneratedRegex(@"\[\s*([\d.]+)\] long frame \d+: ([\d.]+) ms")]
    private static partial Regex LongLine();
    [GeneratedRegex(@"\[\s*([\d.]+)\] benchmark: measuring (\d+) s")]
    private static partial Regex MeasureLine();
    [GeneratedRegex(@"gpu: ReGPU at (\d+x\d+), msaa (\d+)")]
    private static partial Regex SizeLine();

    static double D(string s) => double.Parse(s, CultureInfo.InvariantCulture);

    /// <summary>The window's perf lite lines (as tools/analysis/bench.py).</summary>
    public static Result Parse(string log, int seconds, string profile, GameSettings settings)
    {
        var mark = MeasureLine().Match(log);
        if (!mark.Success)
            throw new InvalidOperationException("The race never reached the measuring part (no \"benchmark: measuring\" line in fm3.log): the menus may have changed, or the game stopped.");
        double t0 = D(mark.Groups[1].Value), t1 = t0 + D(mark.Groups[2].Value);
        var rows = LiteLine().Matches(log).Select(m => m.Groups.Values.Skip(1).Select(g => D(g.Value)).ToArray()).Where(r => r[0] >= t0 && r[0] < t1).ToList();
        if (rows.Count == 0) throw new InvalidOperationException("No performance lines in the measured part of fm3.log.");
        var longs = LongLine().Matches(log).Select(m => (t: D(m.Groups[1].Value), ms: D(m.Groups[2].Value))).Where(x => x.t >= t0 && x.t < t1).ToList();
        var size = SizeLine().Match(log);
        var p99 = rows.Select(r => r[3]).OrderBy(x => x).ToList();
        var options = new List<string>();
        if (settings.Upscaler != "off") options.Add($"{settings.Upscaler} q{settings.Upscale}");
        if (settings.PerfPreset != "off") options.Add($"preset {settings.PerfPreset}");
        if (settings.OneTile) options.Add("one tile");
        if (settings.HalfRateMirror) options.Add("half-rate mirror");
        if (settings.HalfRateShadows) options.Add("half-rate shadows");
        if (settings.HalfRateReflections) options.Add("half-rate reflections");
        if (settings.CarLodFar) options.Add("far car LOD");
        if (settings.AnisoFromGame) options.Add("game anisotropy");
        if (!settings.VramUploadRing) options.Add("ring in RAM");
        if (!settings.AsyncPresent) options.Add("sync present");
        return new Result(
            DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture), profile,
            size.Success ? size.Groups[1].Value : settings.Resolution, size.Success ? int.Parse(size.Groups[2].Value, CultureInfo.InvariantCulture) : settings.Msaa,
            settings.MaxFps, string.Join(", ", options), seconds,
            Math.Round(rows.Average(r => r[1]), 1), Math.Round(rows.Min(r => r[1]), 1), Math.Round(p99[p99.Count / 2], 2),
            Math.Round(rows.Max(r => r[4]), 2), longs.Count, longs.Count(x => x.ms > 10),
            (int)Math.Round(rows.Average(r => r[5])), Math.Round(rows.Average(r => r[6]), 2), Math.Round(rows.Average(r => r[7]), 1));
    }

    static void Save(Result r)
    {
        Directory.CreateDirectory(Dir);
        var name = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
        File.WriteAllText(Path.Combine(Dir, name + ".json"), JsonSerializer.Serialize(r, Json));
        var csv = Path.Combine(Dir, "runs.csv");
        if (!File.Exists(csv))
            File.WriteAllText(csv, "time,resolution,msaa,max_fps,options,seconds,fps_avg,fps_lowest_second,p99_ms,worst_ms,long_frames,long_over_10ms,draws,gpu_ms,draw_busy\n");
        File.AppendAllText(csv, string.Join(",", r.Time, r.Resolution, r.Msaa, r.MaxFps, $"\"{r.Options}\"", r.Seconds,
            F(r.FpsAvg), F(r.FpsLowestSecond), F(r.P99Ms), F(r.WorstMs), r.LongFrames, r.LongFramesOver10, r.Draws, F(r.GpuMs), F(r.DrawThreadBusy)) + "\n");
        try { File.Copy(Paths.Log, Path.Combine(Dir, name + ".log"), true); } catch (IOException) { }
    }

    static string F(double v) => v.ToString(CultureInfo.InvariantCulture);

    /// <summary>Earlier results, newest first.</summary>
    public static List<Result> List(int max = 20)
    {
        if (!Directory.Exists(Dir)) return [];
        var list = new List<Result>();
        foreach (var f in new DirectoryInfo(Dir).EnumerateFiles("*.json").OrderByDescending(f => f.Name).Take(max))
            try { if (JsonSerializer.Deserialize<Result>(File.ReadAllText(f.FullName)) is { } r) list.Add(r); } catch (JsonException) { }
        return list;
    }
}
