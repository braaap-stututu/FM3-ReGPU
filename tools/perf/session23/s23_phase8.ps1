# Session 23, phase 8: L11 frame pacing. FM3_RING_WAIT 0 (spin) vs 2 (sleep), 4 interleaved rounds of 8 s on the frozen
# grid, frame-time spread from the debug server (`frametimes`: avg, p50, p99, p99.9, max, frames over 20/33 ms).
$root = "G:\claude code projects\FM3-ReGPU recomp"
. "$root\tools\perf_suite.ps1" -Run s23
Write-Note '=== phase 8: L11 frame pacing'
if (-not (Start-Grid)) { Write-Note 'phase 8: boot failed'; exit 1 }
for ($r = 1; $r -le 4; $r++) {
    foreach ($mode in '0', '2') {
        Set-Live env FM3_RING_WAIT $mode; Start-Sleep 3
        Fm3 'frametimes' | Out-Null
        Start-Sleep 8
        $ft = Fm3 'frametimes'
        Add-Content -Encoding utf8 $Suite.Csv ('{0},P8-L11,"FM3_RING_WAIT={1} r{2}",,,,,"{3}"' -f (Get-Date -Format 'HH:mm:ss'), $mode, $r, ($ft -replace '"', "'"))
        Write-Note "P8 L11 mode $mode round $r : $ft"
    }
}
Set-Live env FM3_RING_WAIT 0
Stop-Game
Write-Note '=== phase 8 done'
