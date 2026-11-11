# Session 23, phase 4: CPU-side leads where the CPU is the limit: 1280x720, MSAA 1x (settings.ini edited for the
# run, then restored). Live: L11 ring wait modes (with CPU use), I4a, I4b, I5, I1. Start-time: L9, L13, DT-off.
$root = "G:\claude code projects\FM3-ReGPU recomp"
. "$root\tools\perf_suite.ps1" -Run s23
$ini = "$root\out\user\settings.ini"
Copy-Item $ini "$ini.phase4-saved" -Force
(Get-Content $ini) -replace '^resolution=.*', 'resolution=1280x720' -replace '^msaa=.*', 'msaa=1' | Set-Content -Encoding ascii $ini
Write-Note '=== phase 4: CPU-bound (1280x720, msaa 1)'
try {
    if (-not (Start-Grid)) { Write-Note 'phase 4: boot failed'; return }
    $line = Get-Content $Suite.Log | Select-String 'gpu: ReGPU at' | Select-Object -First 1
    $m = Get-Fps 15; $s = Save-Shot 'P4-base'
    Add-Result 'P4-BASE' 'defaults 720p 1x' $m $s "$line"
    Write-Note "phase 4 base: $($m.Fps) fps ($($m.Samples)); $line"
    # The most expensive pixel shaders (phase 3) as translated HLSL, for L6/L30.
    foreach ($h in 'e41e183a423e6f1b', 'b117fbafcc1fa980', 'f7841c0a8245eabf', '510c814e1a4ef99e', '8be0d4632b7d3122') {
        $r = Fm3 "hlsl $h ../perf/s23/ps_$h.hlsl" # (relative to the game's folder out\run; the command takes no spaces)
        Write-Note "hlsl $h : $r"
    }
    # L35: textures whose content "changed" while the world is frozen (each one a reload), 5 s.
    $n0 = (Get-Content $Suite.Log).Count
    Fm3 'switch REGPU_TRACE_TEXCHANGE 1' | Out-Null; Start-Sleep 5; Fm3 'switch REGPU_TRACE_TEXCHANGE 0' | Out-Null; Start-Sleep 1
    $tc = @(Get-Content $Suite.Log | Select-Object -Skip $n0 | Where-Object { $_ -match 'texchange:' })
    $tc | Set-Content -Encoding utf8 "$($Suite.Out)\P4-texchange.txt"
    $addrs = @($tc | ForEach-Object { if ($_ -match 'texchange: ([0-9A-F]{8}) (\S+) (\d+x\d+)') { "$($Matches[1]) $($Matches[2]) $($Matches[3])" } } | Group-Object | Sort-Object Count -Descending)
    Write-Note "L35 frozen grid: $($tc.Count) texture content changes in 5 s, $($addrs.Count) textures; top: $(($addrs | Select-Object -First 6 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join '; ')"
    # L11 at 720p: fps and CPU per mode, interleaved.
    $res = @{}
    for ($r = 0; $r -lt 3; $r++) {
        foreach ($mode in '0', '1', '2') {
            Set-Live env FM3_RING_WAIT $mode; Start-Sleep 3
            $p = Get-Process fm3; $c0 = $p.TotalProcessorTime.TotalSeconds; $t0 = Get-Date
            $mm = Get-Fps 10
            $p.Refresh(); $cores = ($p.TotalProcessorTime.TotalSeconds - $c0) / ((Get-Date) - $t0).TotalSeconds
            if (-not $res[$mode]) { $res[$mode] = @() }
            $res[$mode] += [pscustomobject]@{ Fps = $mm.Fps; Cores = [math]::Round($cores, 2) }
        }
    }
    Set-Live env FM3_RING_WAIT 0
    foreach ($mode in '0', '1', '2') {
        $f = [math]::Round(($res[$mode].Fps | Measure-Object -Average).Average, 1)
        $c = [math]::Round(($res[$mode].Cores | Measure-Object -Average).Average, 2)
        Add-Result 'P4-L11' "FM3_RING_WAIT=$mode" ([pscustomobject]@{ Fps = $f; Samples = ($res[$mode].Fps -join ' '); Draws = '' }) '' "720p 1x; CPU $c cores"
        Write-Note "P4 L11 FM3_RING_WAIT=$mode : $f fps ($($res[$mode].Fps -join ' ')), CPU $c cores"
    }
    Test-Live 'P4-AA' 'FM3_NULL_AB' -Kind env
    Test-Live 'P4-I4a' 'REGPU_INDEX_HASH_ONCE' -Kind switch
    Test-Live 'P4-I4b' 'REGPU_COPIES_1010102' -Kind switch
    Test-Live 'P4-I5' 'REGPU_UPLOAD_STABLE_REUSE' -Kind switch
    Test-Live 'P4-I1' 'REGPU_DRAW_CULL' -Kind env
    Test-Live 'P4-L1' 'REGPU_NO_VERTEX_ARENA' -Kind switch
    # Start-time ones: 2 boots each, a defaults boot first.
    foreach ($v in @(@('P4-L9', @{ REGPU_SUBMIT_DRAWS = '512' }), @('P4-L13', @{ FM3_DRAW_THREAD_PIECES = '1' }), @('P4-DToff', @{ FM3_NO_DRAW_THREAD = '1' }),
                     @('P4-BASE2', @{}))) {
        for ($b = 1; $b -le 2; $b++) {
            if (-not (Start-Grid $v[1])) { Write-Note "$($v[0]) boot $b failed"; continue }
            $mm = Get-Fps 15; $ss = Save-Shot "$($v[0])-boot$b"
            Add-Result $v[0] "boot$b" $mm $ss "720p 1x"
            Write-Note "$($v[0]) boot $b : $($mm.Fps) fps ($($mm.Samples))"
        }
    }
}
finally {
    Stop-Game
    Copy-Item "$ini.phase4-saved" $ini -Force
    Write-Note "phase 4: settings.ini restored ($((Get-Content $ini | Select-String '^resolution|^msaa') -join ', '))"
    Write-Note '=== phase 4 done'
}
