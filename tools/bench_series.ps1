# A series of benchmark races (session 33): N runs of the game's own FM3_BENCHMARK race, started MINIMIZED (as every
# automated boot), each on a fresh copy of Baseline, results through tools/analysis/bench.py (+ a per-second series).
#   powershell -File tools\bench_series.ps1 -Runs 5 -Seconds 120 -Resolution 3840x2160 -Label 4k [-Env "REGPU_X=1;FM3_Y=2"]
# A/B: add -EnvB "..." (B's variables; A gets -Env): the runs alternate A, B, A, B... (-Runs per side), and at the end
# tools/analysis/bench_compare.py compares them plainly and at equal draw counts (the AI races differently every run).
# Resolution goes in through FM3_RESOLUTION (settings.ini stays as it is). Output: out\bench\series-<label>-<time>\.
param([int]$Runs = 5, [int]$Seconds = 120, [string]$Resolution = '', [string]$Label = 'series', [string]$Env = '', [string]$EnvB = $null)
$root = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$dir = Join-Path $root "out\bench\series-$Label-$stamp"
New-Item -ItemType Directory -Force $dir | Out-Null
$profiles = Join-Path $root 'out\user\profiles'
$ab = $PSBoundParameters.ContainsKey('EnvB')
$plan = @()
for ($i = 1; $i -le $Runs; $i++) {
    if ($ab) { $plan += @{ Side = 'A'; N = $i; Env = $Env }, @{ Side = 'B'; N = $i; Env = $EnvB } }
    else { $plan += @{ Side = ''; N = $i; Env = $Env } }
}
$first = $true
foreach ($p in $plan) {
    $name = if ($p.Side) { "$($p.Side)$($p.N)" } else { "run$($p.N)" }
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { "${name}: fm3 already running, stopping"; break }
    $bench = Join-Path $profiles 'Benchmark'
    if (Test-Path $bench) { Remove-Item -Recurse -Force $bench }
    Copy-Item -Recurse (Join-Path $profiles 'Baseline') $bench
    $vars = @{ FM3_BENCHMARK = "$Seconds"; FM3_PROFILE = 'Benchmark'; FM3_MINIMIZED = '1' }
    if ($Resolution) { $vars.FM3_RESOLUTION = $Resolution }
    foreach ($kv in ($p.Env -split ';' | Where-Object { $_ -match '=' })) { $k, $v = $kv -split '=', 2; $vars[$k.Trim()] = $v.Trim() }
    # The previous run's video memory released first: a run started seconds after the last one failed at 4K with
    # out-of-memory creating a colour surface (session 33).
    if (-not $first) { Start-Sleep 15 }
    $first = $false
    foreach ($k in $vars.Keys) { Set-Item "env:$k" $vars[$k] }
    Start-Process cmd.exe "/c `"$root\play.cmd`"" -WindowStyle Hidden
    foreach ($k in $vars.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
    Start-Sleep 10
    $t0 = Get-Date
    while ((Get-Process fm3 -ErrorAction SilentlyContinue) -and ((Get-Date) - $t0).TotalSeconds -lt ($Seconds + 240)) { Start-Sleep 3 }
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { "${name}: timed out, killing"; Get-Process fm3 | Stop-Process -Force; Start-Sleep 2 }
    $log = Join-Path $dir "$name.log"
    Copy-Item (Join-Path $root 'out\run\fm3.log') $log
    & python -I (Join-Path $root 'tools\analysis\bench.py') $log 10 $Seconds (Join-Path $dir "$name.json") | Out-Null
    $r = Get-Content (Join-Path $dir "$name.json") -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
    if ($r) { "${name}: $($r.resolution) msaa $($r.msaa): $($r.fps_avg) fps, lowest $($r.fps_lowest_second), p99 $($r.frame_ms_p99_median) ms, worst $($r.frame_ms_worst) ms, long $($r.long_frames), draws $($r.draws_avg), GPU $($r.gpu_ms_avg) ms, draw busy $($r.draw_thread_busy_avg)%" }
    else { "${name}: no result (see $log)" }
}
$bench = Join-Path $profiles 'Benchmark'
if (Test-Path $bench) { Remove-Item -Recurse -Force $bench }
if ($ab) {
    $a = @(Get-ChildItem $dir -Filter 'A*.series.csv' | ForEach-Object FullName)
    $b = @(Get-ChildItem $dir -Filter 'B*.series.csv' | ForEach-Object FullName)
    "A: $Env"
    "B: $EnvB"
    if ($a.Count -and $b.Count) {
        $cmp = & python -I (Join-Path $root 'tools\analysis\bench_compare.py') --a @a --b @b
        $cmp | Set-Content (Join-Path $dir 'compare.txt') -Encoding ascii
        $cmp
    }
    else { 'compare: a side has no results' }
}
"results: $dir"
