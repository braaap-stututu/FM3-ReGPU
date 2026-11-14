# AI-driven benchmark race (session 33). FM3's own race AI drives the player's car (src/hooks/ai_player.cpp,
# FM3_AI_PLAYER=1) in the test-grid career race (CrownJewel, 3 laps, 7 opponents); the light performance log measures.
#
#   . .\tools\benchmark.ps1; Invoke-Benchmark [-Label name] [-Seconds 120] [-Settle 10] [-Env @{REGPU_X='1'}]
#
# - Never on the user's save: each run copies out\user\profiles\Baseline to a fresh "Benchmark" profile and races on
#   that copy (a finished race would otherwise write a result into the Baseline save).
# - The window: Settle seconds after the race is released, then Seconds (default 120: ends before the 3-lap finish,
#   ~2:20). Settings come from out\user\settings.ini as for any play.cmd run (resolution, MSAA, cap, options).
# - Results: out\bench\<label>-<time>\ (result.json, result.txt, fm3.log), one line per run in out\bench\runs.csv.
# - The AI varies from run to run (racing lines, contact): compare several runs per variant, interleaved.
# Boots minimized like every automated run (FM3_MINIMIZED=1). The game is closed afterwards.
$BenchRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Benchmark([string]$Label = 'run', [int]$Seconds = 120, [int]$Settle = 10, [hashtable]$Env = @{}) {
    . (Join-Path $BenchRoot 'tools\perf_suite.ps1') -Run 'bench'
    $profiles = Join-Path $BenchRoot 'out\user\profiles'
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { Stop-Game }
    $bench = Join-Path $profiles 'Benchmark'
    if (Test-Path $bench) { Remove-Item -Recurse -Force $bench }
    Copy-Item -Recurse (Join-Path $profiles 'Baseline') $bench
    $vars = @{ FM3_PROFILE = 'Benchmark'; FM3_AI_PLAYER = '1'; REGPU_PERF_LITE = '1' }
    foreach ($k in $Env.Keys) { $vars[$k] = $Env[$k] }
    if (-not (Start-Grid $vars)) { Write-Warning 'benchmark: the grid route failed'; Stop-Game; return }
    Fm3 'env FM3_FREEZE_TIME 0' | Out-Null
    Start-Sleep ($Settle + $Seconds + 2)
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $dir = Join-Path $BenchRoot "out\bench\$Label-$stamp"
    New-Item -ItemType Directory -Force $dir | Out-Null
    Copy-Item (Join-Path $BenchRoot 'out\run\fm3.log') (Join-Path $dir 'fm3.log')
    # Closed without the exit save: the Benchmark copy is thrown away anyway.
    Get-Process fm3 -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 2
    $summary = & python -I (Join-Path $BenchRoot 'tools\analysis\bench.py') (Join-Path $dir 'fm3.log') $Settle $Seconds (Join-Path $dir 'result.json')
    $summary | Set-Content (Join-Path $dir 'result.txt') -Encoding ascii
    $r = Get-Content (Join-Path $dir 'result.json') -Raw | ConvertFrom-Json
    $csv = Join-Path $BenchRoot 'out\bench\runs.csv'
    if (-not (Test-Path $csv)) { 'time,label,env,resolution,msaa,fps_avg,fps_lowest_second,p99_ms,worst_ms,long_frames,draws,gpu_ms,draw_busy' | Set-Content $csv -Encoding ascii }
    $envText = ($Env.Keys | ForEach-Object { "$_=$($Env[$_])" }) -join ' '
    "$stamp,$Label,$envText,$($r.resolution),$($r.msaa),$($r.fps_avg),$($r.fps_lowest_second),$($r.frame_ms_p99_median),$($r.frame_ms_worst),$($r.long_frames),$($r.draws_avg),$($r.gpu_ms_avg),$($r.draw_thread_busy_avg)" | Add-Content $csv -Encoding ascii
    $summary
    "results: $dir"
}
