# Session 23, phase 6: which preset piece breaks the race picture (white rectangles in Mild/Potato/Low/Extreme).
$root = "G:\claude code projects\FM3-ReGPU recomp"
. "$root\tools\perf_suite.ps1" -Run s23
$o = $Suite.Out
$sets = [ordered]@{
    'helper050'          = @{ REGPU_HELPER_SCALE = '0.5' }
    'helper025'          = @{ REGPU_HELPER_SCALE = '0.25' }
    'freeze3'            = @{ REGPU_FREEZE_HELPERS = '3' }
    'mild-nohelper'      = @{ FM3_PERF_PRESET = 'mild'; REGPU_HELPER_SCALE = '1' }
    'potato-nohelper'    = @{ FM3_PERF_PRESET = 'potato'; REGPU_HELPER_SCALE = '1' }
    'potato-nohelper-nofreeze' = @{ FM3_PERF_PRESET = 'potato'; REGPU_HELPER_SCALE = '1'; REGPU_FREEZE_HELPERS = '0' }
    'defaults'           = @{}
}
Write-Note '=== phase 6: preset pieces vs the white rectangles'
foreach ($name in $sets.Keys) {
    if (-not (Start-Grid $sets[$name])) { Write-Note "P6 $name : boot failed"; continue }
    $m = Get-Fps 12
    $s = Save-Shot "P6-$name"
    $d = Get-MaskedDiff "$o\P2-BASE-defaults-12.png" $s "$o\boot1.png" "$o\boot2.png"
    Add-Result 'P6' $name $m $s "vs defaults-12: $d"
    Write-Note "P6 $name : $($m.Fps) fps; vs defaults: $d"
}
Stop-Game
Write-Note '=== phase 6 done'
