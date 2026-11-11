# Session 23: is the frozen test grid the same on every boot? 3 boots: shots, fps, AI roster from the log.
. "G:\claude code projects\FM3-ReGPU recomp\tools\perf_suite.ps1" -Run s23
Write-Note '=== boot check: 3 boots of the test grid'
for ($b = 1; $b -le 3; $b++) {
    if (-not (Start-Grid @{ FM3_AI_POOL_LOG = '1' })) { Write-Note "boot $b failed"; continue }
    $frz = Get-Content $Suite.Log | Select-String 'grid test: frozen'
    $shot = Save-Shot "boot$b"
    Start-Sleep 2
    $shot2 = Save-Shot "boot$b-b"
    $m = Get-Fps 15
    Get-Content $Suite.Log | Select-String 'ai drivers|grid test: race start' | ForEach-Object { $_.Line -replace '^\[\s*[\d.]+\]\s*', '' } |
        Set-Content -Encoding utf8 (Join-Path $Suite.Out "boot$b-roster.txt")
    Add-Result 'BOOT' "boot$b" $m $shot "$frz; same-boot shots: $(Get-PixelDiff $shot $shot2)"
    Write-Note "boot $b : $($m.Fps) fps ($($m.Samples)), draws $($m.Draws); $frz"
}
Stop-Game
$o = $Suite.Out
Write-Note "boot1 vs boot2: $(Get-PixelDiff "$o\boot1.png" "$o\boot2.png")"
Write-Note "boot1 vs boot3: $(Get-PixelDiff "$o\boot1.png" "$o\boot3.png")"
Write-Note "boot2 vs boot3: $(Get-PixelDiff "$o\boot2.png" "$o\boot3.png")"
Write-Note "masked boot2 vs boot3 (noise from boot1/boot2): $(Get-MaskedDiff "$o\boot2.png" "$o\boot3.png" "$o\boot1.png" "$o\boot2.png")"
foreach ($p in @(@(1, 2), @(1, 3))) {
    $d = Compare-Object (Get-Content "$o\boot$($p[0])-roster.txt") (Get-Content "$o\boot$($p[1])-roster.txt")
    Write-Note "roster boot$($p[0]) vs boot$($p[1]): $(@($d).Count) differing lines"
}
Write-Note '=== boot check done'
