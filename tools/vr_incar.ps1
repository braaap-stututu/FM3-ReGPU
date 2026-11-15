# VR tests on the frozen grid (session 38): put the player's camera director on the in-car camera (CCamDriver), as
# session 37 did by hand, so FM3_VR_FAKE poses the view without a headset. Dot-source after tools/fm3ctl.ps1, with the
# game on the grid and vr=cockpit|stereo:  Set-Fm3InCarCamera  (returns the director's address, or $null).
# How: CCamDriver objects start with vtable 0x8200206C (findhex); the director lists the player's cameras from +0x18
# and holds the active one at +0x14; on the grid the active one is the chase camera (CCamFollowHigh, 0x82001DB4).
function Set-Fm3InCarCamera {
    $cams = (Fm3 "findhex 8200206C" | ConvertFrom-Json).hits
    foreach ($cam in $cams) {
        $refs = (Fm3 "findhex $cam" | ConvertFrom-Json).hits
        foreach ($ref in $refs) {
            $list = [Convert]::ToUInt32($ref, 16)
            foreach ($k in 0..7) {
                $director = $list - 0x18 - 4 * $k
                $active = (Fm3 ("peek {0:X8} 4" -f ($director + 0x14)) | ConvertFrom-Json).hex
                if (-not $active -or $active -notmatch '^[0-9A-F]{8}$') { continue }
                $vt = (Fm3 "peek $active 4" | ConvertFrom-Json).hex
                if ($vt -in @('82001DB4', '82001FF4', '82034FC4', '8200206C')) {
                    Fm3 ("poke {0:X8} {1}" -f ($director + 0x14), $cam) | Out-Null
                    return ('{0:X8}' -f $director)
                }
            }
        }
    }
    return $null
}
