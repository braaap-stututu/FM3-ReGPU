# Session 23, phase 3: measurements before code (PERF-PLAN step 3): GPU time per pass/resolve (L26, first form),
# then per shader (the AI-car material price list), each on the frozen grid; the log blocks are saved.
. "G:\claude code projects\FM3-ReGPU recomp\tools\perf_suite.ps1" -Run s23
$o = $Suite.Out
Write-Note '=== phase 3: GPU time by pass and by shader'
foreach ($v in @(@('passes', @{ REGPU_GPU_PASSES = '1' }), @('shaders', @{ REGPU_GPU_SHADERS = '1' }),
                  @('stats', @{ REGPU_GPU_PASSES = '2'; REGPU_DRAW_WORKERS = '0' }))) {
    if (-not (Start-Grid $v[1] 20)) { Write-Note "phase 3 $($v[0]): boot failed"; continue }
    $m = Get-Fps 10
    Start-Sleep 6
    # The last complete block: from the second-to-last "ms/frame over" line on (the last may still be printing).
    $all = @(Get-Content $Suite.Log | Where-Object { $_ -match 'gpu passes:' })
    $heads = @(for ($k = 0; $k -lt $all.Count; $k++) { if ($all[$k] -match 'ms/frame over') { $k } })
    $start = if ($heads.Count -ge 2) { $heads[-2] } elseif ($heads.Count) { $heads[-1] } else { 0 }
    $block = @($all | Select-Object -Skip $start -First 31)
    $block | Set-Content -Encoding utf8 "$o\P3-gpu-$($v[0]).txt"
    if ($v[0] -eq 'stats') {
        # The pipeline statistics block (by pass, by pixel shader): the last full one.
        $st = @(Get-Content $Suite.Log | Where-Object { $_ -match 'gpu stats:' })
        $h = @(for ($k = 0; $k -lt $st.Count; $k++) { if ($st[$k] -match 'invocations/frame') { $k } })
        if ($h.Count -ge 2) { $st | Select-Object -Skip $h[-2] -First 45 | Set-Content -Encoding utf8 "$o\P3-gpu-stats-counts.txt" }
        else { 'no gpu stats block' | Set-Content "$o\P3-gpu-stats-counts.txt" }
    }
    Add-Result 'P3' "gpu-$($v[0])" $m '' "with timing on; block in P3-gpu-$($v[0]).txt"
    Write-Note "phase 3 $($v[0]): $($m.Fps) fps with timing on; $(if ($block.Count) { $block[0] } else { 'no gpu passes lines' })"
}
Stop-Game
Write-Note '=== phase 3 done'
