using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using Photino.NET;

namespace FM3Launcher;

/// <summary>
/// What the page runs in (session 39): the launcher's own window (Photino: WebView2 on Windows, WebKitGTK on Linux), or
/// the system's browser talking to a small web server in the launcher (BrowserShell), for where the window can't run:
/// under Proton or Wine (no WebView2 there), on a Windows without WebView2, or with --browser. Bridge.cs talks to both
/// the same way.
/// </summary>
public interface IShell
{
    /// <summary>A message to the page (any thread).</summary>
    void Send(string json);
    /// <summary>Messages from the page.</summary>
    void OnMessage(Action<string> handler);
    Task<string[]?> PickFolder(string title, string? start);
    Task<string[]?> PickFile(string title, (string Name, string[] Patterns)[] filters);
    Task<string?> PickSaveFile(string title, string name, (string Name, string[] Patterns)[] filters);
}

public sealed class PhotinoShell(PhotinoWindow window) : IShell
{
    public void Send(string json) => window.SendWebMessage(json);
    public void OnMessage(Action<string> handler) => window.RegisterWebMessageReceivedHandler((_, text) => handler(text));
    public Task<string[]?> PickFolder(string title, string? start) => window.ShowOpenFolderAsync(title, start, false)!;
    public Task<string[]?> PickFile(string title, (string Name, string[] Patterns)[] filters) => window.ShowOpenFileAsync(title, null, false, filters)!;
    public Task<string?> PickSaveFile(string title, string name, (string Name, string[] Patterns)[] filters) => window.ShowSaveFileAsync(title, name, filters)!;
}

/// <summary>
/// The page in the system's browser: a web server on 127.0.0.1 (a random port) serving wwwroot under a random token
/// (/&lt;token&gt;/...: other pages and programs can't reach the launcher's commands without it), the page's messages
/// as POST /&lt;token&gt;/msg, the launcher's as a server-sent event stream /&lt;token&gt;/events. Plain sockets, not
/// HttpListener (which goes through Windows' http.sys, only partly there in Wine). The launcher quits when the page has
/// been gone for 15 s. File pickers: Windows' own dialogs (Wine draws them).
/// </summary>
public sealed class BrowserShell : IShell
{
    readonly TcpListener _listener = new(IPAddress.Loopback, 0);
    readonly string _token = Convert.ToHexString(System.Security.Cryptography.RandomNumberGenerator.GetBytes(16)).ToLowerInvariant();
    readonly object _gate = new();
    readonly List<NetworkStream> _streams = [];
    readonly Queue<string> _pending = new(); // messages before the page connected
    Action<string>? _handler;
    DateTime _lastSeen = DateTime.UtcNow;
    bool _everConnected;
    readonly ManualResetEventSlim _done = new();

    public string Url { get; }

    public BrowserShell()
    {
        _listener.Start();
        Url = $"http://127.0.0.1:{((IPEndPoint)_listener.LocalEndpoint).Port}/{_token}/index.html";
        new Thread(Accept) { IsBackground = true, Name = "launcher web" }.Start();
        new Thread(Watch) { IsBackground = true, Name = "launcher web watch" }.Start();
    }

    /// <summary>Until the page is closed (its event stream gone for 15 s, or never opened in 3 minutes).</summary>
    public void WaitForClose() => _done.Wait();

    public void OnMessage(Action<string> handler) => _handler = handler;

    public void Send(string json)
    {
        var bytes = Encoding.UTF8.GetBytes("data: " + json.Replace("\n", "\\n") + "\n\n");
        lock (_gate)
        {
            if (_streams.Count == 0) { _pending.Enqueue(json); while (_pending.Count > 500) _pending.Dequeue(); return; }
            foreach (var s in _streams.ToArray())
            {
                try { s.Write(bytes); s.Flush(); }
                catch { _streams.Remove(s); }
            }
        }
    }

    void Watch()
    {
        for (;;)
        {
            Thread.Sleep(1000);
            lock (_gate)
            {
                if (_streams.Count > 0) _lastSeen = DateTime.UtcNow;
                var gone = DateTime.UtcNow - _lastSeen;
                if ((_everConnected && gone > TimeSpan.FromSeconds(15)) || (!_everConnected && gone > TimeSpan.FromMinutes(3)))
                {
                    _done.Set();
                    return;
                }
            }
        }
    }

    void Accept()
    {
        for (;;)
        {
            TcpClient c;
            try { c = _listener.AcceptTcpClient(); } catch { return; }
            new Thread(() => Serve(c)) { IsBackground = true }.Start();
        }
    }

    void Serve(TcpClient client)
    {
        var keep = false;
        try
        {
            var s = client.GetStream();
            // The request line and headers.
            var head = new StringBuilder();
            var b = new byte[1];
            while (head.Length < 16384 && s.Read(b, 0, 1) == 1)
            {
                head.Append((char)b[0]);
                if (head.Length >= 4 && head.ToString(head.Length - 4, 4) == "\r\n\r\n") break;
            }
            var lines = head.ToString().Split("\r\n");
            var first = lines[0].Split(' ');
            if (first.Length < 2) return;
            var method = first[0];
            var path = Uri.UnescapeDataString(first[1].Split('?')[0]);
            int length = 0;
            foreach (var l in lines)
                if (l.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase)) int.TryParse(l[15..].Trim(), out length);
            var prefix = "/" + _token + "/";
            if (!path.StartsWith(prefix, StringComparison.Ordinal)) { Reply(s, 404, "text/plain", "Not found"u8.ToArray()); return; }
            var rel = path[prefix.Length..];
            if (method == "POST" && rel == "msg")
            {
                var body = new byte[Math.Min(length, 8 << 20)];
                for (int got = 0; got < body.Length;)
                {
                    var n = s.Read(body, got, body.Length - got);
                    if (n <= 0) break;
                    got += n;
                }
                Reply(s, 204, "text/plain", []);
                var text = Encoding.UTF8.GetString(body);
                ThreadPool.QueueUserWorkItem(_ => _handler?.Invoke(text));
                return;
            }
            if (method == "GET" && rel == "events")
            {
                var hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n";
                s.Write(Encoding.ASCII.GetBytes(hdr));
                s.Flush();
                lock (_gate)
                {
                    _streams.Add(s);
                    _everConnected = true;
                    _lastSeen = DateTime.UtcNow;
                    while (_pending.Count > 0)
                    {
                        var bytes = Encoding.UTF8.GetBytes("data: " + _pending.Dequeue().Replace("\n", "\\n") + "\n\n");
                        s.Write(bytes);
                    }
                    s.Flush();
                }
                keep = true;
                // Stays open; a comment every 10 s finds a closed page (the write fails).
                new Thread(() =>
                {
                    for (;;)
                    {
                        Thread.Sleep(10000);
                        lock (_gate)
                        {
                            if (!_streams.Contains(s)) break;
                            try { s.Write(": ping\n\n"u8); s.Flush(); }
                            catch { _streams.Remove(s); break; }
                        }
                    }
                    try { client.Close(); } catch { }
                }) { IsBackground = true }.Start();
                return;
            }
            if (method != "GET") { Reply(s, 405, "text/plain", []); return; }
            // A file of wwwroot.
            var root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "wwwroot"));
            var file = Path.GetFullPath(Path.Combine(root, rel.Length == 0 ? "index.html" : rel));
            if (!file.StartsWith(root, StringComparison.OrdinalIgnoreCase) || !File.Exists(file)) { Reply(s, 404, "text/plain", "Not found"u8.ToArray()); return; }
            var type = Path.GetExtension(file).ToLowerInvariant() switch
            {
                ".html" => "text/html; charset=utf-8", ".js" => "text/javascript; charset=utf-8", ".css" => "text/css; charset=utf-8",
                ".png" => "image/png", ".svg" => "image/svg+xml", ".ico" => "image/x-icon", ".json" => "application/json", _ => "application/octet-stream",
            };
            Reply(s, 200, type, File.ReadAllBytes(file));
        }
        catch { }
        finally
        {
            if (!keep) try { client.Close(); } catch { }
        }
    }

    static void Reply(NetworkStream s, int code, string type, byte[] body)
    {
        var reason = code switch { 200 => "OK", 204 => "No Content", 404 => "Not Found", _ => "Error" };
        var hdr = $"HTTP/1.1 {code} {reason}\r\nContent-Type: {type}\r\nContent-Length: {body.Length}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        s.Write(Encoding.ASCII.GetBytes(hdr));
        s.Write(body);
        s.Flush();
    }

    // ---- Windows' own file dialogs (on an STA thread of their own; Wine draws them)

    static Task<T> Sta<T>(Func<T> f)
    {
        var tcs = new TaskCompletionSource<T>();
        var t = new Thread(() =>
        {
            try { tcs.SetResult(f()); } catch (Exception e) { tcs.SetException(e); }
        });
        t.SetApartmentState(ApartmentState.STA);
        t.IsBackground = true;
        t.Start();
        return tcs.Task;
    }

    static string Filter((string Name, string[] Patterns)[] filters) =>
        string.Concat(filters.Select(f => f.Name + " (" + string.Join(";", f.Patterns) + ")\0" + string.Join(";", f.Patterns) + "\0")) + "\0";

    public Task<string[]?> PickFile(string title, (string Name, string[] Patterns)[] filters) => Sta<string[]?>(() =>
    {
        var buf = Marshal.AllocHGlobal(4096 * 2);
        try
        {
            Marshal.WriteInt16(buf, 0);
            var ofn = new OPENFILENAME { lStructSize = Marshal.SizeOf<OPENFILENAME>(), lpstrFilter = Filter(filters), lpstrFile = buf, nMaxFile = 4096, lpstrTitle = title, Flags = 0x00081800 /* EXPLORER | FILEMUSTEXIST | PATHMUSTEXIST */ };
            return GetOpenFileNameW(ref ofn) ? [Marshal.PtrToStringUni(buf)!] : null;
        }
        finally { Marshal.FreeHGlobal(buf); }
    });

    public Task<string?> PickSaveFile(string title, string name, (string Name, string[] Patterns)[] filters) => Sta<string?>(() =>
    {
        var buf = Marshal.AllocHGlobal(4096 * 2);
        try
        {
            var init = Encoding.Unicode.GetBytes(name + "\0");
            Marshal.Copy(init, 0, buf, Math.Min(init.Length, 4096 * 2 - 2));
            var ofn = new OPENFILENAME { lStructSize = Marshal.SizeOf<OPENFILENAME>(), lpstrFilter = Filter(filters), lpstrFile = buf, nMaxFile = 4096, lpstrTitle = title, Flags = 0x00080002 /* EXPLORER | OVERWRITEPROMPT */, lpstrDefExt = "zip" };
            return GetSaveFileNameW(ref ofn) ? Marshal.PtrToStringUni(buf) : null;
        }
        finally { Marshal.FreeHGlobal(buf); }
    });

    public Task<string[]?> PickFolder(string title, string? start) => Sta<string[]?>(() =>
    {
        OleInitialize(IntPtr.Zero);
        var bi = new BROWSEINFO { lpszTitle = title, ulFlags = 0x0041 /* RETURNONLYFSDIRS | NEWDIALOGSTYLE */ };
        var pidl = SHBrowseForFolderW(ref bi);
        if (pidl == IntPtr.Zero) return null;
        try
        {
            var path = new StringBuilder(1024);
            return SHGetPathFromIDListW(pidl, path) ? [path.ToString()] : null;
        }
        finally { Marshal.FreeCoTaskMem(pidl); }
    });

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct OPENFILENAME
    {
        public int lStructSize;
        public IntPtr hwndOwner, hInstance;
        public string lpstrFilter;
        public IntPtr lpstrCustomFilter;
        public int nMaxCustFilter, nFilterIndex;
        public IntPtr lpstrFile;
        public int nMaxFile;
        public IntPtr lpstrFileTitle;
        public int nMaxFileTitle;
        public string? lpstrInitialDir, lpstrTitle;
        public int Flags;
        public short nFileOffset, nFileExtension;
        public string? lpstrDefExt;
        public IntPtr lCustData, lpfnHook;
        public string? lpTemplateName;
        public IntPtr pvReserved;
        public int dwReserved, FlagsEx;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct BROWSEINFO
    {
        public IntPtr hwndOwner, pidlRoot, pszDisplayName;
        public string lpszTitle;
        public uint ulFlags;
        public IntPtr lpfn, lParam;
        public int iImage;
    }
    [DllImport("comdlg32.dll", CharSet = CharSet.Unicode)] static extern bool GetOpenFileNameW(ref OPENFILENAME ofn);
    [DllImport("comdlg32.dll", CharSet = CharSet.Unicode)] static extern bool GetSaveFileNameW(ref OPENFILENAME ofn);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SHBrowseForFolderW(ref BROWSEINFO bi);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern bool SHGetPathFromIDListW(IntPtr pidl, StringBuilder path);
    [DllImport("ole32.dll")] static extern int OleInitialize(IntPtr reserved);

    /// <summary>Wine (and so Proton) adds wine_get_version to ntdll.</summary>
    public static bool UnderWine()
    {
        if (!OperatingSystem.IsWindows()) return false;
        try
        {
            var ntdll = GetModuleHandleW("ntdll.dll");
            return ntdll != IntPtr.Zero && GetProcAddress(ntdll, "wine_get_version") != IntPtr.Zero;
        }
        catch { return false; }
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module, string name);

    /// <summary>Opens the page in the system's browser (under Wine: the Linux desktop's, through winebrowser).</summary>
    public void OpenBrowser()
    {
        if (Environment.GetEnvironmentVariable("FM3_LAUNCHER_NO_OPEN") == "1") return; // (tests: the URL file is enough)
        try
        {
            if (UnderWine())
            {
                try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("winebrowser.exe", Url) { UseShellExecute = false }); return; }
                catch { } // (no winebrowser: Windows' link handling, which Wine sends the same way)
            }
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(Url) { UseShellExecute = true });
        }
        catch (Exception e) { Console.Error.WriteLine("Open " + Url + " in a browser: " + e.Message); }
    }
}
