# CPU use per thread of the running fm3.exe over -Seconds (session 24): OS thread times, named by the thread
# description (SetThreadDescription) where there is one. Nothing in the game changes, so it measures without cost.
#   tools\perf\threads.ps1 [-Seconds 5] [-Top 15]
param([int]$Seconds = 5, [int]$Top = 15)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class ThreadInfo {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern int GetThreadDescription(IntPtr h, out IntPtr desc);
    [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr p);
    public static string Name(uint id) {
        IntPtr h = OpenThread(0x0800, false, id); // THREAD_QUERY_LIMITED_INFORMATION
        if (h == IntPtr.Zero) return "";
        IntPtr d; string s = "";
        if (GetThreadDescription(h, out d) >= 0 && d != IntPtr.Zero) { s = Marshal.PtrToStringUni(d); LocalFree(d); }
        CloseHandle(h); return s;
    }
}
"@
$p = Get-Process fm3 -ErrorAction Stop
$t0 = @{}; foreach ($t in $p.Threads) { try { $t0[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {} }
$w0 = [Diagnostics.Stopwatch]::StartNew()
Start-Sleep $Seconds
$p.Refresh()
$wall = $w0.Elapsed.TotalMilliseconds
$rows = foreach ($t in $p.Threads) {
    try { $ms = $t.TotalProcessorTime.TotalMilliseconds } catch { continue }
    $before = if ($t0.ContainsKey($t.Id)) { $t0[$t.Id] } else { 0 }
    [pscustomobject]@{ Tid = $t.Id; Name = [ThreadInfo]::Name([uint32]$t.Id); Core = [math]::Round(100 * ($ms - $before) / $wall, 1) }
}
$sum = ($rows | Measure-Object Core -Sum).Sum
$rows | Sort-Object Core -Descending | Select-Object -First $Top | Format-Table -AutoSize | Out-String
"total: {0:N0}% of a core ({1:N2} cores), {2} threads" -f $sum, ($sum / 100), $rows.Count
