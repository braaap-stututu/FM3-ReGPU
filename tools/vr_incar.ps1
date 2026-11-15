# VR tests on the grid (session 38): switch the player's view to the in-car camera, so FM3_VR_FAKE poses it without a
# headset. Dot-source after tools/fm3ctl.ps1, with the game on the grid and vr=cockpit|stereo:
#   Set-Fm3InCarCamera   (returns the camera director's address once its active camera is the in-car one, or $null)
# The director (src/hooks/vr_camera.cpp) holds its active camera at +0x0C; its address comes from the hook's log line
# "vr: view 0 (director X) ..." (a memory search finds other objects pointing at the same cameras). Views change only in
# a running race and with RB held ~0.5 s (virtual pad), so the world runs while RB is pressed until the active camera is
# CCamDriver (vtable 0x8200206C), then it is frozen again. The race clock moves on by those seconds.
function Set-Fm3InCarCamera([string]$Want = '8200206C') {
    $log = Join-Path $Suite.Root 'out\run\fm3.log'
    $line = Get-Content $log | Select-String 'vr: view 0 \(director ([0-9A-F]{8})\)' | Select-Object -Last 1
    if (-not $line) { return $null }
    $director = $line.Matches[0].Groups[1].Value
    $activeAt = '{0:X8}' -f ([Convert]::ToUInt32($director, 16) + 0x0C)
    Fm3 "env FM3_FREEZE_TIME 0" | Out-Null
    $found = $null
    for ($i = 0; $i -lt 8; $i++) {
        $cam = (Fm3 "peek $activeAt 4" | ConvertFrom-Json).hex
        $vt = if ($cam -match '^4') { (Fm3 "peek $cam 4" | ConvertFrom-Json).hex } else { '' }
        if ($vt -eq $Want) { $found = $director; break }
        Fm3 "press RB 500" | Out-Null
        Start-Sleep 2
    }
    Fm3 "env FM3_FREEZE_TIME 1" | Out-Null
    Start-Sleep 1
    return $found
}
