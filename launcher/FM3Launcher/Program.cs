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
            if (GetStdHandle(-11) == IntPtr.Zero) AttachConsole(-1); // (output redirected to a file: kept there)
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
        // --benchmark [seconds]: the benchmark race (Benchmark.cs) without the window; the result to the console and
        // the user folder's bench\, as the Settings tab's Benchmark button.
        var bench = Array.IndexOf(args, "--benchmark");
        if (bench >= 0)
        {
            if (GetStdHandle(-11) == IntPtr.Zero) AttachConsole(-1);
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
        if (!first)
        {
            if (OperatingSystem.IsWindows())
                foreach (var p in System.Diagnostics.Process.GetProcessesByName("FM3Launcher"))
                    if (p.Id != Environment.ProcessId && p.MainWindowHandle != IntPtr.Zero) SetForegroundWindow(p.MainWindowHandle);
            return 0;
        }
        Photino.NET.PhotinoWindow window;
        try { window = Web.Create(Title, "index.html", 1500, 900, 1000, 680); }
        catch (Exception ex)
        {
            Platform.Error(Title, "The launcher needs Microsoft Edge WebView2 (part of Windows 11; a free download for Windows 10).\n\n" + ex.Message);
            return 1;
        }
        _ = new Bridge(window);
        window.WaitForClose();
        Wheel.Stop(); // the Wheel stuff tab's live view
        return 0;
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool SetForegroundWindow(IntPtr hWnd);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern bool AttachConsole(int processId);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern IntPtr GetStdHandle(int which);
}
