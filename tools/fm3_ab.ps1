param([string]$Switch, [int]$Rounds = 4)
# Interleaved live A/B of a REGPU_* switch (session 20), on a paused scene with constant draws (a race grid):
# per round 7 `Fm3 "status"` fps samples with the switch off, then on (2 s settle each); screenshots of the first
# round's off/on to out\shots\s20_ab_{off,on}.png (compare with tools\analysis\pngdiff.py). Leaves the switch ON.
#   powershell -File tools\fm3_ab.ps1 -Switch REGPU_SPLIT_ALPHA_TEST -Rounds 4
# FM3_* switches: use `Fm3 "env NAME 0|1"` instead (edit the two switch lines). Run-to-run spread is ~7 fps at
# ~200 fps, so trust a result only when every "on" round beats every "off" round (or the reverse).
. (Join-Path $PSScriptRoot "fm3ctl.ps1")
function Sample {
    $f = @()
    for ($i = 0; $i -lt 7; $i++) { Start-Sleep -Milliseconds 800; $f += (Fm3 "status" | ConvertFrom-Json).fps }
    [math]::Round(($f | Measure-Object -Average).Average, 1)
}
$off = @(); $on = @()
for ($r = 0; $r -lt $Rounds; $r++) {
    Fm3 "switch $Switch 0" | Out-Null; Start-Sleep 2; $off += Sample
    if ($r -eq 0) { Save-Fm3Shot "s20_ab_off.png" | Out-Null }
    Fm3 "switch $Switch 1" | Out-Null; Start-Sleep 2; $on += Sample
    if ($r -eq 0) { Save-Fm3Shot "s20_ab_on.png" | Out-Null }
}
"off: $($off -join ' ')  avg $([math]::Round(($off | Measure-Object -Average).Average,1))"
"on:  $($on -join ' ')  avg $([math]::Round(($on | Measure-Object -Average).Average,1))"
"draws: $((Fm3 'status' | ConvertFrom-Json).draws)"
