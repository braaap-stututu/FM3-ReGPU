# Session 23, L11: ring wait modes (FM3_RING_WAIT 0 spin, 1 yield, 2 sleep 100 us), live on the frozen grid:
# fps and the game's CPU use (process CPU time over the sample window, in cores), interleaved 3 rounds.
param([switch]$Boot)
. "G:\claude code projects\FM3-ReGPU recomp\tools\perf_suite.ps1" -Run s23
Write-Note '=== L11 ring wait modes'
if ($Boot -or -not (Get-Process fm3 -ErrorAction SilentlyContinue)) { if (-not (Start-Grid)) { Write-Note 'L11: boot failed'; exit 1 } }
$res = @{}
for ($r = 0; $r -lt 3; $r++) {
    foreach ($mode in '0', '1', '2') {
        Set-Live env FM3_RING_WAIT $mode; Start-Sleep 3
        $p = Get-Process fm3; $c0 = $p.TotalProcessorTime.TotalSeconds; $t0 = Get-Date
        $m = Get-Fps 10
        $p.Refresh(); $cores = ($p.TotalProcessorTime.TotalSeconds - $c0) / ((Get-Date) - $t0).TotalSeconds
        if (-not $res[$mode]) { $res[$mode] = @() }
        $res[$mode] += [pscustomobject]@{ Fps = $m.Fps; Cores = [math]::Round($cores, 2) }
        if ($r -eq 0) { $s = Save-Shot "L11-mode$mode" }
    }
}
Set-Live env FM3_RING_WAIT 0
foreach ($mode in '0', '1', '2') {
    $f = [math]::Round(($res[$mode].Fps | Measure-Object -Average).Average, 1)
    $c = [math]::Round(($res[$mode].Cores | Measure-Object -Average).Average, 2)
    Add-Result 'L11' "FM3_RING_WAIT=$mode" ([pscustomobject]@{ Fps = $f; Samples = ($res[$mode].Fps -join ' '); Draws = '' }) "$($Suite.Out)\L11-mode$mode.png" "CPU $c cores ($($res[$mode].Cores -join ' '))"
    Write-Note "L11 FM3_RING_WAIT=$mode : $f fps ($($res[$mode].Fps -join ' ')), game CPU $c cores ($($res[$mode].Cores -join ' '))"
}
Write-Note "L11 shots mode0 vs mode2: $(Get-PixelDiff "$($Suite.Out)\L11-mode0.png" "$($Suite.Out)\L11-mode2.png")"
Write-Note '=== L11 done'
