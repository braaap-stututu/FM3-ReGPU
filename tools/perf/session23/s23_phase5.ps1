# Session 23, phase 5: the promising start-time pieces alone and stacked, 3 boots each, rotated with defaults
# (A B C D A B C D ...), so drift spreads over all of them. 1440p 4x (the user's settings).
$root = "G:\claude code projects\FM3-ReGPU recomp"
. "$root\tools\perf_suite.ps1" -Run s23
$o = $Suite.Out
$sets = [ordered]@{
    'defaults'       = @{}
    'lean1'          = @{ REGPU_LEAN_FETCH = '1' }
    'lean2'          = @{ REGPU_LEAN_FETCH = '2' }
    'plainmul'       = @{ REGPU_PLAIN_MUL_PS = '1' }
    'lean2+plainmul+copies' = @{ REGPU_LEAN_FETCH = '2'; REGPU_PLAIN_MUL_PS = '1'; REGPU_COPIES_1010102 = '1' }
    'lean1+plainmul+copies' = @{ REGPU_LEAN_FETCH = '1'; REGPU_PLAIN_MUL_PS = '1'; REGPU_COPIES_1010102 = '1' }
}
Write-Note '=== phase 5: stacked start-time pieces, 3 rotated rounds'
$fps = @{}
for ($r = 1; $r -le 3; $r++) {
    foreach ($name in $sets.Keys) {
        if (-not (Start-Grid $sets[$name])) { Write-Note "P5 $name round $r : boot failed"; continue }
        $m = Get-Fps 15
        $s = Save-Shot ("P5-{0}-r{1}" -f ($name -replace '\+', '_'), $r)
        if (-not $fps[$name]) { $fps[$name] = @() }
        $fps[$name] += $m.Fps
        $note = if ($name -ne 'defaults' -and (Test-Path "$o\P5-defaults-r$r.png")) { "masked vs defaults r$r : $(Get-MaskedDiff "$o\P5-defaults-r$r.png" $s "$o\boot1.png" "$o\boot2.png")" } else { '' }
        Add-Result 'P5' "$name r$r" $m $s $note
        Write-Note "P5 $name round $r : $($m.Fps) fps ($($m.Samples)); $note"
    }
}
Stop-Game
$base = ($fps['defaults'] | Measure-Object -Average).Average
foreach ($name in $sets.Keys) {
    $a = ($fps[$name] | Measure-Object -Average).Average
    Write-Note ("P5 summary {0}: {1:N1} fps ({2}) = {3:+0.0;-0.0}% vs defaults" -f $name, $a, ($fps[$name] -join ' '), (100 * ($a - $base) / $base))
}
Write-Note '=== phase 5 done'
