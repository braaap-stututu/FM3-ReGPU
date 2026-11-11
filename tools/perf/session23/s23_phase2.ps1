# Session 23, phase 2: start-time switches (PERF-PLAN step 2). Each variant: 2 boots (the first may compile new
# shader variants; the second is the clean measurement), a defaults boot between every two variants (drift).
# Shots of each boot; pixel diff vs the nearest defaults boot, masked by the crowd areas (boot check pair).
param([string[]]$Only = @())
. "G:\claude code projects\FM3-ReGPU recomp\tools\perf_suite.ps1" -Run s23
$o = $Suite.Out
$variants = [ordered]@{
    'L3-optimize'   = @{ REGPU_OPTIMIZE_SHADERS = '1' }
    'I2-lean1'      = @{ REGPU_LEAN_FETCH = '1' }
    'I2-lean2'      = @{ REGPU_LEAN_FETCH = '2' }
    'I2-leanv'      = @{ REGPU_LEAN_VFETCH = '1' }
    'I3-plainmul'   = @{ REGPU_PLAIN_MUL_PS = '1' }
    'I8-aniso'      = @{ REGPU_ANISO_FROM_FETCH = '1' }
    'L9-submit512'  = @{ REGPU_SUBMIT_DRAWS = '512' }
    'L13-pieces'    = @{ FM3_DRAW_THREAD_PIECES = '1' }
    'DT-off'        = @{ FM3_NO_DRAW_THREAD = '1' }
    'V1-turbo'      = @{ FM3_PERF_PRESET = 'turbo' }
    'V1-mild'       = @{ FM3_PERF_PRESET = 'mild' }
    'V1-potato'     = @{ FM3_PERF_PRESET = 'potato' }
    'V1-low'        = @{ FM3_PERF_PRESET = 'low' }
    'V1-extreme'    = @{ FM3_PERF_PRESET = 'extreme' }
}
$noise1 = "$o\boot1.png"; $noise2 = "$o\boot2.png"
$baseShot = "$o\boot1.png"

function Measure-Boot([string]$Id, [string]$Variant, [hashtable]$Env, [int]$Settle) {
    if (-not (Start-Grid $Env $Settle)) { Write-Note "$Id $Variant : boot failed (variant skipped)"; return $null }
    $m = Get-Fps 15
    $s = Save-Shot "$Id-$Variant"
    $comp = (Get-Content $Suite.Log | Select-String 'failed to compile' | Measure-Object).Count
    $note = "masked vs defaults: $(Get-MaskedDiff $baseShot $s $noise1 $noise2); compile failures $comp"
    Add-Result $Id $Variant $m $s $note
    Write-Note "$Id $Variant : $($m.Fps) fps ($($m.Samples)), draws $($m.Draws); $note"
    $m
}

Write-Note '=== phase 2: start-time switches'
$i = 0
foreach ($name in $variants.Keys) {
    if ($Only.Count -and $Only -notcontains $name) { continue }
    if ($i % 2 -eq 0) {
        $b = Measure-Boot 'P2-BASE' "defaults-$i" @{} 15
        if ($b) { $baseShot = "$o\P2-BASE-defaults-$i.png" }
    }
    $env1 = $variants[$name]
    $settle = if ($name -like 'L3*') { 45 } else { 15 }
    Measure-Boot $name 'boot1' $env1 $settle | Out-Null
    Measure-Boot $name 'boot2' $env1 $settle | Out-Null
    $i++
}
Measure-Boot 'P2-BASE' 'defaults-end' @{} 15 | Out-Null
Stop-Game
Write-Note '=== phase 2 done'
