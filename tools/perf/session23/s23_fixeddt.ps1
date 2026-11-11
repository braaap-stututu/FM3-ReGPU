# Session 23: do fixed frame steps (FM3_FIXED_DT=1: dt = 1/360 every main frame) make two boots freeze on the same
# picture (the crowd, the AI cars' sub-pixel settling)? 2 boots, shots, pixel diffs, against the default boots.
. "G:\claude code projects\FM3-ReGPU recomp\tools\perf_suite.ps1" -Run s23
$o = $Suite.Out
Write-Note '=== fixed-dt check: 2 boots with FM3_FIXED_DT=1'
for ($b = 1; $b -le 2; $b++) {
    if (-not (Start-Grid @{ FM3_FIXED_DT = '1' })) { Write-Note "fixed-dt boot $b failed"; continue }
    $frz = Get-Content $Suite.Log | Select-String 'grid test: frozen'
    $s = Save-Shot "fixeddt$b"
    $m = Get-Fps 10
    Add-Result 'FIXEDDT' "boot$b" $m $s "$frz"
    Write-Note "fixed-dt boot $b : $($m.Fps) fps, draws $($m.Draws); $frz"
}
Stop-Game
Write-Note "fixed-dt boot1 vs boot2: $(Get-PixelDiff "$o\fixeddt1.png" "$o\fixeddt2.png")"
Write-Note "default boot2 vs boot3 (for comparison): $(Get-PixelDiff "$o\boot2.png" "$o\boot3.png")"
Write-Note '=== fixed-dt check done'
