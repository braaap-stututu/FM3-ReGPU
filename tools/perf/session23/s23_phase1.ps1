# Session 23, phase 1: live switches on the frozen test grid, one boot (PERF-PLAN step 1).
. "G:\claude code projects\FM3-ReGPU recomp\tools\perf_suite.ps1" -Run s23
Write-Note '=== phase 1: live switches'
if (-not (Start-Grid)) { Write-Note 'phase 1: boot failed'; exit 1 }
$m = Get-Fps 15; $s = Save-Shot 'P1-base'
Add-Result 'P1-BASE' 'defaults' $m $s 'phase 1 boot, all defaults'
Write-Note "phase 1 base: $($m.Fps) fps ($($m.Samples)), draws $($m.Draws)"

# Null A/B: an unused variable flipped the same way, for the noise floor.
Test-Live 'AA' 'FM3_NULL_AB' -Kind env
# L1 vertex arena (on by default): on = arena OFF.
Test-Live 'L1' 'REGPU_NO_VERTEX_ARENA' -Kind switch
# L2 alpha-test-free pixel shaders (on by default): restore on.
Test-Live 'L2' 'REGPU_SPLIT_ALPHA_TEST' -Kind switch -Restore '1'
# I4 index lists hashed once a frame; 2_10_10_10 copies kept in 10 bits.
Test-Live 'I4a' 'REGPU_INDEX_HASH_ONCE' -Kind switch
Test-Live 'I4b' 'REGPU_COPIES_1010102' -Kind switch
# I5 stable upload reuse.
Test-Live 'I5' 'REGPU_UPLOAD_STABLE_REUSE' -Kind switch
# V1 piece: alpha to mask as a plain test.
Test-Live 'A2M' 'REGPU_A2M_AS_TEST' -Kind switch
# I8 live part (the samplers are made at start; the per-texture anisotropy choice follows live).
Test-Live 'I8live' 'REGPU_ANISO_FROM_FETCH' -Kind switch
# I1 hidden-part cull at 1 px (read each frame from the environment), then a flicker check: 6 shots in a row with
# it on; on a frozen world any difference between them is flicker.
Test-Live 'I1' 'REGPU_DRAW_CULL' -Kind env
Set-Live env REGPU_DRAW_CULL 1; Start-Sleep 3
$burst = @(); for ($i = 0; $i -lt 6; $i++) { $burst += Save-Shot "I1-burst$i" }
for ($i = 1; $i -lt 6; $i++) { Write-Note "I1 flicker check, shot 0 vs $i : $(Get-PixelDiff $burst[0] $burst[$i])" }
Set-Live env REGPU_DRAW_CULL 0
# Same-session sanity: base again at the end (drift over the phase).
Start-Sleep 3; $m = Get-Fps 15; $s = Save-Shot 'P1-base-end'
Add-Result 'P1-BASE' 'defaults (end)' $m $s "vs start: $(Get-PixelDiff (Join-Path $Suite.Out 'P1-base.png') $s)"
Write-Note "phase 1 base at the end: $($m.Fps) fps"
Write-Note '=== phase 1 done'
