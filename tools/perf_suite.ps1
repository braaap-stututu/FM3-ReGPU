<#
  perf_suite.ps1 - hands-off performance/picture tests on the frozen test grid (session 23).
  Dot-source it, then call the functions; results go to out\perf\<Run>\results.csv and shots beside it.

    . .\tools\perf_suite.ps1 -Run s23
    Start-Grid @{ REGPU_LEAN_FETCH = '2' }     # (re)boot with the grid route + start-time env; waits for the freeze
    Get-Fps 12                                 # average of N `status` fps samples (0.7 s apart)
    Save-Shot 'name'                           # out\perf\<Run>\name.png (full resolution, next frame)
    Test-Live 'I4' 'REGPU_INDEX_HASH_ONCE' -Kind switch -Rounds 4   # interleaved live A/B, shots, pixel diff, CSV row

  Every boot is a full restart of play.cmd with FM3_GRID_TEST=1 (src/hooks/grid_test.cpp): the world freezes at the
  same countdown moment each time, so the GPU work is constant and frozen shots of one game session are identical.
#>
param([string]$Run = 's23')
. (Join-Path $PSScriptRoot 'fm3ctl.ps1')
$Suite = @{
    Root = $Fm3Root
    Out  = Join-Path $Fm3Root "out\perf\$Run"
    Log  = Join-Path $Fm3Root 'out\run\fm3.log'
}
New-Item -ItemType Directory -Force $Suite.Out | Out-Null
$Suite.Csv = Join-Path $Suite.Out 'results.csv'
if (-not (Test-Path $Suite.Csv)) { 'time,test,variant,fps,samples,draws,shot,note' | Set-Content -Encoding utf8 $Suite.Csv }

function Write-Note([string]$Text) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $Text
    $line
    Add-Content -Encoding utf8 (Join-Path $Suite.Out 'journal.txt') $line
}

function Reset-Fm3Conn {
    if ($script:Fm3Sock) { try { $script:Fm3Sock.Close() } catch {} }
    $script:Fm3Sock = $null
}

function Stop-Game {
    $p = Get-Process fm3 -ErrorAction SilentlyContinue
    if ($p) {
        $p | ForEach-Object { $_.CloseMainWindow() | Out-Null }
        for ($i = 0; $i -lt 40 -and (Get-Process fm3 -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
        if (Get-Process fm3 -ErrorAction SilentlyContinue) { Write-Note 'fm3 did not close in 40 s: killing it'; Get-Process fm3 | Stop-Process -Force; Start-Sleep 2 }
    }
    Reset-Fm3Conn
}

# Boot with the grid route and $Env (start-time switches); $true once frozen (and settled), else $false.
function Start-Grid([hashtable]$Env = @{}, [int]$Settle = 15) {
    Stop-Game
    $env:FM3_GRID_TEST = '1'
    if (-not $Env.ContainsKey('FM3_MINIMIZED')) { $env:FM3_MINIMIZED = '1' } # game window minimized (session 24)
    foreach ($k in $Env.Keys) { Set-Item "env:$k" $Env[$k] }
    Start-Process cmd.exe "/c `"$($Suite.Root)\play.cmd`"" -WindowStyle Hidden
    foreach ($k in $Env.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
    Remove-Item env:FM3_GRID_TEST -ErrorAction SilentlyContinue
    Remove-Item env:FM3_MINIMIZED -ErrorAction SilentlyContinue
    Start-Sleep 4
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 180) {
        Start-Sleep 2
        if (-not (Get-Process fm3 -ErrorAction SilentlyContinue)) { Write-Note "fm3 exited during boot ($(($Env.Keys | % { "$_=$($Env[$_])" }) -join ' '))"; return $false }
        $hit = Get-Content $Suite.Log -ErrorAction SilentlyContinue | Select-String 'grid test: (frozen|no race)'
        if ($hit) {
            if ("$hit" -match 'no race') { Write-Note "route failed: $hit"; return $false }
            Start-Sleep $Settle
            Reset-Fm3Conn
            $bad = Get-Content $Suite.Log | Select-String 'failed to compile' | Measure-Object
            if ($bad.Count) { Write-Note "warning: $($bad.Count) 'failed to compile' lines in the log" }
            return $true
        }
    }
    Write-Note 'route timed out (180 s)'
    return $false
}

function Get-Fps([int]$N = 12) {
    $f = @(); $d = 0
    for ($i = 0; $i -lt $N; $i++) {
        Start-Sleep -Milliseconds 700
        $s = Fm3 'status' | ConvertFrom-Json
        $f += $s.fps; $d = $s.draws
    }
    [pscustomobject]@{ Fps = [math]::Round(($f | Measure-Object -Average).Average, 1); Samples = ($f -join ' '); Draws = $d }
}

function Save-Shot([string]$Name) {
    $p = Join-Path $Suite.Out "$Name.png"
    Fm3 "shot $p" | Out-Null
    for ($i = 0; $i -lt 50 -and -not (Test-Path $p); $i++) { Start-Sleep -Milliseconds 100 }
    Start-Sleep -Milliseconds 300
    $p
}

function Add-Result([string]$Test, [string]$Variant, $M, [string]$Shot, [string]$Note) {
    $leaf = if ($Shot) { Split-Path -Leaf $Shot } else { '' }
    $row = '{0},{1},{2},{3},"{4}",{5},{6},"{7}"' -f (Get-Date -Format 'HH:mm:ss'), $Test, $Variant, $M.Fps, $M.Samples, $M.Draws, $leaf, ($Note -replace '"', "'")
    Add-Content -Encoding utf8 $Suite.Csv $row
}

function Get-PixelDiff([string]$A, [string]$B) {
    $r = & python -I (Join-Path $Suite.Root 'tools\analysis\pngdiff.py') $A $B 2>&1 | Select-Object -First 1
    "$r"
}

function Get-MaskedDiff([string]$A, [string]$B, [string]$N1, [string]$N2) {
    $r = & python -I (Join-Path $Suite.Root 'tools\analysis\pngdiff_mask.py') $A $B $N1 $N2 2>&1 | Select-Object -First 1
    "$r"
}

function Set-Live([string]$Kind, [string]$Name, [string]$Value) {
    if ($Kind -eq 'switch') { Fm3 "switch $Name $Value" | Out-Null } else { Fm3 "env $Name $Value" | Out-Null }
}

# GPU ms a frame (and fps) from the light performance log (boot with REGPU_PERF_LITE=1; session 31): waits N+1 s,
# then averages the last N `perf lite:` lines. For GPU-side leads at 4K, where fps alone may sit at a limit.
function Get-LiteGpu([int]$N = 4) {
    Start-Sleep ($N + 1)
    $l = Select-String -Path $Suite.Log -Pattern 'perf lite: ([\d.]+) fps.*GPU ([\d.]+) ms/frame' | Select-Object -Last $N
    [pscustomobject]@{
        Gpu = [math]::Round(($l | ForEach-Object { [double]$_.Matches[0].Groups[2].Value } | Measure-Object -Average).Average, 3)
        Fps = [math]::Round(($l | ForEach-Object { [double]$_.Matches[0].Groups[1].Value } | Measure-Object -Average).Average, 1)
    }
}

# Let the frozen grid's race go with the throttle held for $Seconds (session 31: fps with moving cars; automatic
# gearbox). Once per boot: the race can't be put back on the grid. fps = 1 s `status` samples; frame times
# (average, p99) from `frametimes` over the same stretch. Leaves the race running.
function Test-Drive([string]$Id, [string]$Variant, [int]$Seconds = 10) {
    Fm3 'trigger R 1' | Out-Null
    Fm3 'env FM3_FREEZE_TIME 0' | Out-Null
    Fm3 'frametimes' | Out-Null
    $f = @(); $d = @()
    for ($i = 0; $i -lt $Seconds; $i++) {
        Start-Sleep -Milliseconds 1000
        $s = Fm3 'status' | ConvertFrom-Json
        $f += $s.fps; $d += $s.draws
    }
    $ft = Fm3 'frametimes' | ConvertFrom-Json
    Fm3 'trigger R 0' | Out-Null
    $m = [pscustomobject]@{ Fps = [math]::Round(($f | Measure-Object -Average).Average, 1); Samples = ($f -join ' '); Draws = [math]::Round(($d | Measure-Object -Average).Average) }
    Add-Result $Id "$Variant drive" $m '' "frame avg $($ft.avg) ms p99 $($ft.p99) ms max $($ft.max) ms; draws $($d -join ' ')"
    Write-Note "$Id $Variant drive ${Seconds}s: $($m.Fps) fps ($($m.Samples)), draws ~$($m.Draws), frame avg $($ft.avg) p99 $($ft.p99) max $($ft.max) ms"
    $m
}

# Interleaved live A/B: $Rounds x (off, on), N fps samples each after 3 s settling; shots of round 1's sides and
# their pixel diff (same game session, frozen: exact). Leaves the switch at $Restore.
function Test-Live([string]$Id, [string]$Name, [string]$Kind = 'switch', [string]$Off = '0', [string]$On = '1', [int]$Rounds = 4, [string]$Restore = $Off) {
    $offs = @(); $ons = @(); $draws = @()
    for ($r = 0; $r -lt $Rounds; $r++) {
        Set-Live $Kind $Name $Off; Start-Sleep 3; $m = Get-Fps 8; $offs += $m.Fps; $dOff = $m.Draws
        if ($r -eq 0) { $sOff = Save-Shot "$Id-off" }
        Set-Live $Kind $Name $On; Start-Sleep 3; $m = Get-Fps 8; $ons += $m.Fps; $dOn = $m.Draws
        if ($r -eq 0) { $sOn = Save-Shot "$Id-on" }
    }
    Set-Live $Kind $Name $Restore
    $aOff = [math]::Round(($offs | Measure-Object -Average).Average, 1)
    $aOn = [math]::Round(($ons | Measure-Object -Average).Average, 1)
    $wins = 0; for ($r = 0; $r -lt $Rounds; $r++) { if ($ons[$r] -gt $offs[$r]) { $wins++ } }
    $diff = Get-PixelDiff $sOff $sOn
    $pct = [math]::Round(100 * ($aOn - $aOff) / $aOff, 1)
    Add-Result $Id "$Name=$Off" ([pscustomobject]@{ Fps = $aOff; Samples = ($offs -join ' '); Draws = $dOff }) $sOff ''
    Add-Result $Id "$Name=$On" ([pscustomobject]@{ Fps = $aOn; Samples = ($ons -join ' '); Draws = $dOn }) $sOn "$pct% ($wins/$Rounds rounds faster); $diff"
    Write-Note "$Id $Name : off $aOff ($($offs -join ' ')) on $aOn ($($ons -join ' ')) = $pct%, on faster in $wins/$Rounds rounds, draws $dOff/$dOn; $diff"
}
