// Windows and Linux (session 39: on Linux the window is WebKitGTK and the game runs through Wine/Proton, Runner.cs).

namespace FM3Launcher;

internal static class Program
{
    const string Title = "Forza Motorsport 3 ReGPU";

    [STAThread]
    static int Main(string[] args)
    {
        // --play: start the game with the saved settings, without the window (shortcuts, tests)
        if (args.Contains("--play"))
        {
            try
            {
                Game.Launch()?.Dispose();
                return 0;
            }
            catch (Exception ex) { Platform.Error(Title, ex.Message); return 1; }
        }
        // --install-disc2 <Disc 2 .iso | folder | package>: installs Disc 2's content packages without the window
        // (install-disc2.cmd). Progress goes to the console that started it.
        var at = Array.IndexOf(args, "--install-disc2");
        if (at >= 0)
        {
            if (OperatingSystem.IsWindows() && GetStdHandle(-11) == IntPtr.Zero) AttachConsole(-1); // (output redirected to a file: kept there)
            try
            {
                if (at + 1 >= args.Length) throw new ArgumentException("Usage: FM3Launcher --install-disc2 <Disc 2 .iso, its folder, or a package file>");
                Disc2.Install(args[at + 1], Console.WriteLine);
                return 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine("Error: " + ex.Message);
                return 1;
            }
        }
        // --install-disc1 <Disc 1 .iso> [folder]: extracts Disc 1 into the game folder next to the launcher (or the
        // folder given) without the window.
        var d1 = Array.IndexOf(args, "--install-disc1");
        if (d1 >= 0)
        {
            if (OperatingSystem.IsWindows() && GetStdHandle(-11) == IntPtr.Zero) AttachConsole(-1);
            try
            {
                if (d1 + 1 >= args.Length) throw new ArgumentException("Usage: FM3Launcher --install-disc1 <Disc 1 .iso> [folder]");
                var last = -1;
                Disc1.Install(args[d1 + 1], line =>
                {
                    if (line.EndsWith('%') && int.TryParse(line[..^1], out var p)) { if (p / 10 == last) return; last = p / 10; }
                    Console.WriteLine(line);
                }, d1 + 2 < args.Length ? args[d1 + 2] : null);
                return 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine("Error: " + ex.Message);
                return 1;
            }
        }
        // --gpus: the graphics cards the "Graphics card" choice lists (name, own memory), to the console.
        if (args.Contains("--gpus"))
        {
            if (OperatingSystem.IsWindows() && GetStdHandle(-11) == IntPtr.Zero) AttachConsole(-1);
            foreach (var g in Options.Gpus()) Console.WriteLine($"{g.Name}: {g.MemoryMb} MB");
            return 0;
        }
        // --benchmark [seconds]: the benchmark race (Benchmark.cs) without the window; the result to the console and
        // the user folder's bench\, as the Settings tab's Benchmark button.
        var bench = Array.IndexOf(args, "--benchmark");
        if (bench >= 0)
        {
            if (OperatingSystem.IsWindows() && GetStdHandle(-11) == IntPtr.Zero) AttachConsole(-1);
            try
            {
                var seconds = bench + 1 < args.Length && int.TryParse(args[bench + 1], out var s) ? s : 120;
                var last = "";
                var r = Benchmark.Run(seconds, t => { if (t != last) Console.WriteLine(last = t); }).GetAwaiter().GetResult();
                Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(r, new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
                return 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine("Error: " + ex.Message);
                return 1;
            }
        }
        // one window: starting the launcher again brings the open one forward
        using var single = new Mutex(true, "FM3ReGPU.Launcher", out bool first);
        if (!first && (args.Contains("--browser") || BrowserShell.UnderWine()))
        {
            // The launcher already runs in a browser page (no window to bring forward): open that page again.
            try
            {
                var url = File.ReadAllText(Path.Combine(Path.GetTempPath(), "fm3-launcher-url.txt")).Trim();
                if (BrowserShell.UnderWine()) System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("winebrowser.exe", url) { UseShellExecute = false });
                else System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(url) { UseShellExecute = true });
            }
            catch { }
            return 0;
        }
        if (!first)
        {
            if (OperatingSystem.IsWindows())
                foreach (var p in System.Diagnostics.Process.GetProcessesByName("FM3Launcher"))
                    if (p.Id != Environment.ProcessId && p.MainWindowHandle != IntPtr.Zero) SetForegroundWindow(p.MainWindowHandle);
            return 0;
        }
        // Under Proton or Wine there is no WebView2: the page opens in the system's browser instead, served by the launcher
        // (Shell.cs BrowserShell); also with --browser, and where the window fails to open on Windows.
        if (args.Contains("--browser") || BrowserShell.UnderWine())
            return RunInBrowser(BrowserShell.UnderWine() ? "Proton/Wine" : "--browser");
        Photino.NET.PhotinoWindow window;
        if (Platform.Linux) Platform.DesktopEntry(); // (the window's app id and icon on Wayland)
        var (w, h) = DefaultSize(1600, 960);
        try { window = Web.Create(Title, "index.html", w, h, 1000, 680); }
        catch (Exception ex)
        {
            if (!Platform.Linux) return RunInBrowser("no WebView2: " + ex.Message);
            Platform.Error(Title, "The launcher needs WebKitGTK (libwebkit2gtk-4.1: e.g. sudo apt install libwebkit2gtk-4.1-0, sudo pacman -S webkit2gtk-4.1).\n\n" + ex.Message);
            return 1;
        }
        _ = new Bridge(new PhotinoShell(window));
        window.WaitForClose();
        Wheel.Stop(); // the Wheel stuff tab's live view
        return 0;
    }

    /// <summary>The launcher's page in the system's browser until it is closed.</summary>
    static int RunInBrowser(string why)
    {
        var shell = new BrowserShell();
        _ = new Bridge(shell);
        Console.WriteLine($"FM3 launcher ({why}): {shell.Url}");
        try { File.WriteAllText(Path.Combine(Path.GetTempPath(), "fm3-launcher-url.txt"), shell.Url); } catch { }
        // Also beside the game, where it is easy to find when the browser didn't open by itself (Steam's container).
        try { File.WriteAllText(Path.Combine(Paths.Root, "launcher-url.txt"), shell.Url + "\n"); } catch { }
        shell.OpenBrowser();
        shell.WaitForClose();
        Wheel.Stop();
        return 0;
    }

    /// <summary>The window's first size: width x height at 100% display scaling, scaled up with Windows' scaling (window
    /// sizes are in pixels, app.manifest), so the page gets the same room at 125% or 150%. Never more than 92% of the
    /// screen's work area.</summary>
    static (int, int) DefaultSize(int width, int height)
    {
        if (!OperatingSystem.IsWindows()) return (width, height);
        try
        {
            var scale = Math.Max(1.0, GetDpiForSystem() / 96.0);
            int maxW = (int)(GetSystemMetrics(16) * 0.92), maxH = (int)(GetSystemMetrics(17) * 0.92); // SM_CX/CYFULLSCREEN            return (Math.Min((int)(width * scale), maxW), Math.Min((int)(height * scale), maxH));
        }
        catch { return (width, height); }
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern uint GetDpiForSystem();

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern int GetSystemMetrics(int index);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool SetForegroundWindow(IntPtr hWnd);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern bool AttachConsole(int processId);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern IntPtr GetStdHandle(int which);
}
