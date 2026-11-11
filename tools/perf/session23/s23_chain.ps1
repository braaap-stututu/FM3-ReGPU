# Session 23: after phase 1, build the new code (L15 exports, L11 ring wait, L4 pipeline statistics) and run the
# rest unattended: phase 3, L11, fixed-dt check, phase 2.
$root = "G:\claude code projects\FM3-ReGPU recomp"
. "$root\tools\perf_suite.ps1" -Run s23
Stop-Game
Write-Note '=== chain: building'
$b = cmd /c "`"$root\tools\vcenv.cmd`" ninja -C `"$root\build\rel`" fm3 2>&1"
$err = $b | Select-String 'error|FAILED'
if ($err) { Write-Note "chain: BUILD FAILED, stopping: $($err | Select-Object -First 5)"; exit 1 }
Write-Note "chain: build ok ($(($b | Select-String 'Linking') -join ' '))"
& "$root\out\perf\s23_phase3.ps1"
& "$root\out\perf\s23_ringwait.ps1" -Boot
& "$root\out\perf\s23_fixeddt.ps1"
& "$root\out\perf\s23_phase2.ps1"
Stop-Game
Write-Note '=== chain done'
