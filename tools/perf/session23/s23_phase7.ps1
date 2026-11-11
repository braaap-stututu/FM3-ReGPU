# Session 23, phase 7: rebuild with the presets' helper-pass pieces out, then each preset once more (picture + fps).
$root = "G:\claude code projects\FM3-ReGPU recomp"
. "$root\tools\perf_suite.ps1" -Run s23
$o = $Suite.Out
Stop-Game
Write-Note '=== phase 7: presets without the helper-pass pieces'
$b = cmd /c "`"$root\tools\vcenv.cmd`" ninja -C `"$root\build\rel`" fm3 2>&1"
if ($b | Select-String 'error|FAILED') { Write-Note "phase 7: BUILD FAILED: $(($b | Select-String 'error|FAILED' | Select-Object -First 5) -join ' | ')"; exit 1 }
Write-Note 'phase 7: build ok'
foreach ($p in 'defaults', 'turbo', 'mild', 'potato', 'low', 'extreme') {
    $envs = if ($p -eq 'defaults') { @{} } else { @{ FM3_PERF_PRESET = $p } }
    if (-not (Start-Grid $envs)) { Write-Note "P7 $p : boot failed"; continue }
    $m = Get-Fps 15
    $s = Save-Shot "P7-$p"
    $d = if ($p -ne 'defaults') { Get-MaskedDiff "$o\P7-defaults.png" $s "$o\boot1.png" "$o\boot2.png" } else { '' }
    Add-Result 'P7' $p $m $s "after the helper-pass fix; vs defaults: $d"
    Write-Note "P7 $p : $($m.Fps) fps ($($m.Samples)); vs defaults: $d"
}
Stop-Game
& "$root\tools\perf\sheet.ps1" "$o\sheet-p7.png" @('defaults', 'turbo', 'mild', 'potato', 'low', 'extreme' | ForEach-Object { "$o\P7-$_.png" }) | Out-Null
Write-Note '=== phase 7 done'
