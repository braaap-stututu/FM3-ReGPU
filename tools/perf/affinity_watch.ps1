# Keeps every running fm3.exe on a CPU mask while it runs (session 45; session 41 used the same idea for its
# "2 cores" ReXenon races): a stand-in for a weaker CPU, so CPU-side changes show in fps.
#   tools\perf\affinity_watch.ps1 [-Mask 0x0F] [-Minutes 60]      (0x0F = 4 logical CPUs = 2 cores with SMT)
# Run it beside tools\bench_matrix.ps1; it stops by itself after -Minutes.
param([string]$Mask = '0x0F', [int]$Minutes = 60)
$m = [IntPtr][Convert]::ToInt64($Mask, 16)
$end = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $end) {
    foreach ($p in Get-Process fm3 -ErrorAction SilentlyContinue) {
        try { if ($p.ProcessorAffinity -ne $m) { $p.ProcessorAffinity = $m } } catch {}
    }
    Start-Sleep -Milliseconds 500
}
