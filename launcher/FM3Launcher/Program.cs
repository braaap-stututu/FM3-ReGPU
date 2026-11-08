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
                Game.Launch();
                return 0;
            }
            catch (Exception ex) { Platform.Error(Title, ex.Message); return 1; }
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
}
