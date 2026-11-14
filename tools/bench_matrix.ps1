# Benchmark matrix (session 33): several variants of the game's own FM3_BENCHMARK race, interleaved round by round
# (round 1: every variant once, round 2: again...), each run MINIMIZED on a fresh copy of Baseline.
#   powershell -File tools\bench_matrix.ps1 -Label name -Rounds 2 -Seconds 120 -Variants "base|msaa2:FM3_MSAA=2|fsr:FM3_UPSCALER=fsr1;FM3_MSAA=1"
# A variant is "name" or "name:VAR=value;VAR=value"; FM3_RESOLUTION, FM3_MSAA, FM3_UPSCALER, FM3_UPSCALE_QUALITY, FM3_MAX_FPS
# and REGPU_* switches all go in this way (settings.ini stays as it is). -Resolution sets FM3_RESOLUTION for all.
# At the end: a table per variant (mean fps, sd, p99, worst, long frames, GPU ms, VRAM peak) and each variant against the
# first with tools/analysis/bench_compare.py (plain and at equal draw counts). Output: out\bench\matrix-<label>-<time>\.
# -Common "VAR=v;VAR=v": set for every variant (a variant's own value wins), e.g. REGPU_ADAPTER and the settings.ini
# options pinned so the user's launcher choices don't leak in (session 33, iGPU: one tile and half rates were on).
param([string]$Label = 'matrix', [int]$Rounds = 2, [int]$Seconds = 120, [string]$Resolution = '', [string]$Common = '', [Parameter(Mandatory)][string]$Variants)
$root = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$dir = Join-Path $root "out\bench\matrix-$Label-$stamp"
New-Item -ItemType Directory -Force $dir | Out-Null
$profiles = Join-Path $root 'out\user\profiles'
$list = foreach ($v in ($Variants -split '\|')) {
    $name, $envText = $v -split ':', 2
    $vars = @{}
    foreach ($kv in (($envText + '') -split ';' | Where-Object { $_ -match '=' })) { $k, $val = $kv -split '=', 2; $vars[$k.Trim()] = $val.Trim() }
    [pscustomobject]@{ Name = $name.Trim(); Vars = $vars; Env = $envText }
}
"variants: " + (($list | ForEach-Object { "$($_.Name) [$($_.Env)]" }) -join '; ')
$first = $true
for ($round = 1; $round -le $Rounds; $round++) {
    foreach ($v in $list) {
        $name = "$($v.Name)-r$round"
        if (Get-Process fm3 -ErrorAction SilentlyContinue) { "${name}: fm3 already running, stopping"; return }
        $bench = Join-Path $profiles 'Benchmark'
        if (Test-Path $bench) { Remove-Item -Recurse -Force $bench }
        Copy-Item -Recurse (Join-Path $profiles 'Baseline') $bench
        # (The long-frame stack sampler off unless a variant turns it on: it stops the draw thread every millisecond.)
        $vars = @{ FM3_BENCHMARK = "$Seconds"; FM3_PROFILE = 'Benchmark'; FM3_MINIMIZED = '1'; FM3_LONG_STACKS = '0' }
        if ($Resolution) { $vars.FM3_RESOLUTION = $Resolution }
        foreach ($kv in ($Common -split ';' | Where-Object { $_ -match '=' })) { $k, $val = $kv -split '=', 2; $vars[$k.Trim()] = $val.Trim() }
        foreach ($k in $v.Vars.Keys) { $vars[$k] = $v.Vars[$k] }
        if (-not $first) { Start-Sleep 15 } # (the previous run's video memory released: session 33's out-of-memory boot)
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
        if ($r) { "${name}: $($r.resolution) msaa $($r.msaa): $($r.fps_avg) fps, p99 $($r.frame_ms_p99_median) ms, worst $($r.frame_ms_worst) ms, long $($r.long_frames), draws $($r.draws_avg), GPU $($r.gpu_ms_avg) ms, busy $($r.draw_thread_busy_avg)%, VRAM $($r.vram_max_mb) MB" }
        else { "${name}: no result (see $log)" }
    }
}
$bench = Join-Path $profiles 'Benchmark'
if (Test-Path $bench) { Remove-Item -Recurse -Force $bench }
"`nSummary (mean over rounds):"
"{0,-14} {1,8} {2,6} {3,8} {4,9} {5,6} {6,8} {7,7} {8,8} {9,9}" -f 'variant', 'fps', 'sd', 'p99 ms', 'worst ms', 'long', 'GPU ms', 'busy%', 'VRAM MB', 'pres p99'
foreach ($v in $list) {
    $rs = @(Get-ChildItem $dir -Filter "$($v.Name)-r*.json" | ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json })
    if (-not $rs.Count) { "{0,-14} no results" -f $v.Name; continue }
    $fps = $rs | ForEach-Object fps_avg
    $mean = ($fps | Measure-Object -Average).Average
    $sd = if ($fps.Count -gt 1) { [math]::Sqrt((($fps | ForEach-Object { ($_ - $mean) * ($_ - $mean) }) | Measure-Object -Sum).Sum / ($fps.Count - 1)) } else { 0 }
    "{0,-14} {1,8:N1} {2,6:N1} {3,8:N2} {4,9:N1} {5,6:N1} {6,8:N2} {7,7:N0} {8,8:N0} {9,9:N3}" -f $v.Name, $mean, $sd,
        ($rs | ForEach-Object frame_ms_p99_median | Measure-Object -Average).Average, ($rs | ForEach-Object frame_ms_worst | Measure-Object -Maximum).Maximum,
        ($rs | ForEach-Object long_frames | Measure-Object -Average).Average, ($rs | ForEach-Object gpu_ms_avg | Measure-Object -Average).Average,
        ($rs | ForEach-Object draw_thread_busy_avg | Measure-Object -Average).Average, ($rs | ForEach-Object vram_max_mb | Measure-Object -Maximum).Maximum,
        ($rs | ForEach-Object present_p99_ms_median | Measure-Object -Average).Average
}
$base = $list[0].Name
$a = @(Get-ChildItem $dir -Filter "$base-r*.series.csv" | ForEach-Object FullName)
foreach ($v in $list | Select-Object -Skip 1) {
    $b = @(Get-ChildItem $dir -Filter "$($v.Name)-r*.series.csv" | ForEach-Object FullName)
    if (-not $a.Count -or -not $b.Count) { continue }
    "`n=== $($v.Name) vs $base ==="
    & python -I (Join-Path $root 'tools\analysis\bench_compare.py') --a @a --b @b | Select-String 'B vs A'
}
"results: $dir"
