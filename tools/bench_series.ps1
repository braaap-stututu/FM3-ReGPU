# A series of benchmark races (session 33): N runs of the game's own FM3_BENCHMARK race, started MINIMIZED (as every
# automated boot), each on a fresh copy of Baseline, results through tools/analysis/bench.py.
#   powershell -File tools\bench_series.ps1 -Runs 5 -Seconds 120 -Resolution 3840x2160 -Label 4k [-Env "REGPU_X=1;FM3_Y=2"]
# Resolution goes in through FM3_RESOLUTION (settings.ini stays as it is). Output: out\bench\series-<label>-<time>\.
param([int]$Runs = 5, [int]$Seconds = 120, [string]$Resolution = '', [string]$Label = 'series', [string]$Env = '')
$root = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$dir = Join-Path $root "out\bench\series-$Label-$stamp"
New-Item -ItemType Directory -Force $dir | Out-Null
$profiles = Join-Path $root 'out\user\profiles'
for ($i = 1; $i -le $Runs; $i++) {
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { "run ${i}: fm3 already running, stopping"; break }
    $bench = Join-Path $profiles 'Benchmark'
    if (Test-Path $bench) { Remove-Item -Recurse -Force $bench }
    Copy-Item -Recurse (Join-Path $profiles 'Baseline') $bench
    $vars = @{ FM3_BENCHMARK = "$Seconds"; FM3_PROFILE = 'Benchmark'; FM3_MINIMIZED = '1' }
    if ($Resolution) { $vars.FM3_RESOLUTION = $Resolution }
    foreach ($kv in ($Env -split ';' | Where-Object { $_ -match '=' })) { $k, $v = $kv -split '=', 2; $vars[$k.Trim()] = $v.Trim() }
    foreach ($k in $vars.Keys) { Set-Item "env:$k" $vars[$k] }
    # The previous run's video memory released first: a run started seconds after the last one failed at 4K with
    # out-of-memory creating a colour surface (session 33).
    if ($i -gt 1) { Start-Sleep 15 }
    Start-Process cmd.exe "/c `"$root\play.cmd`"" -WindowStyle Hidden
    foreach ($k in $vars.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
    Start-Sleep 10
    $t0 = Get-Date
    while ((Get-Process fm3 -ErrorAction SilentlyContinue) -and ((Get-Date) - $t0).TotalSeconds -lt ($Seconds + 240)) { Start-Sleep 3 }
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { "run ${i}: timed out, killing"; Get-Process fm3 | Stop-Process -Force; Start-Sleep 2 }
    $log = Join-Path $dir "run$i.log"
    Copy-Item (Join-Path $root 'out\run\fm3.log') $log
    & python -I (Join-Path $root 'tools\analysis\bench.py') $log 10 $Seconds (Join-Path $dir "run$i.json") | Out-Null
    $r = Get-Content (Join-Path $dir "run$i.json") -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
    if ($r) { "run ${i}: $($r.resolution) msaa $($r.msaa): $($r.fps_avg) fps, lowest $($r.fps_lowest_second), p99 $($r.frame_ms_p99_median) ms, worst $($r.frame_ms_worst) ms, long $($r.long_frames), GPU $($r.gpu_ms_avg) ms, draw busy $($r.draw_thread_busy_avg)%" }
    else { "run ${i}: no result (see $log)" }
}
$bench = Join-Path $profiles 'Benchmark'
if (Test-Path $bench) { Remove-Item -Recurse -Force $bench }
"results: $dir"
