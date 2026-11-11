# One-time (and idempotent) setup of the build tree:
#   1. submodules (XenonRecomp + its thirdparty, ReGPU) at the pinned commits, LF line endings;
#   2. our XenonRecomp and ReGPU patches, applied in order (skipped when already applied);
#   3. the host tools (XenonRecomp, XenonAnalyse, xexdump) built into build\tools.
# Usage (PowerShell, from anywhere):  & "<project>\tools\setup.ps1" [-NoBuild]
param([switch]$NoBuild)
# Native tools report on stderr; failures are checked through $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Order matters: each patch is made against the tree the previous ones leave.
$patches = @(
    'xenonrecomp-fm2',              # divide-by-zero semantics, MSR[EE] -> PPC_MSR_CHANGED interrupt lock
    'xenonrecomp-modules',          # function-name prefix per image, visible no-op thunks for unnamed imports
    'xenonrecomp-fm4',              # jump-table back-trace through registers, fallthrough count, XEX image-size fix
    'xenonrecomp-fm4-instructions', # extra/fixed VMX128 and CR-bit instructions
    'xenonrecomp-switch-u32'        # switch on the u32 index; vcmpbfp implemented
)
# ReGPU (our own, game-neutral Xenos fixes; see patches/README.md).
$regpuPatches = @(
    'regpu-fixed16-fetch',          # resolved 16-bit fixed images read with the fetch's number format and sign
    'regpu-zpd-tile-spans',         # occlusion queries count only inside each tile's ZPD event pair
    'regpu-resolve-into-image',     # a full-width resolve into an existing image is part of it, not a tile
    'regpu-timer-overlay',          # REGPU_TIMER: the log clock in the top-left corner (test aid)
    'regpu-cube-2101010',           # k_2_10_10_10 textures decoded; cube faces read from memory staged at their own offsets
    'regpu-resolve-into-enclosing', # a resolve into a tile of an earlier resolved image is also drawn into it, at its place
    'regpu-capture-vertex-floats',  # captures log small vertex buffers in full, as floats (debugging)
    'regpu-enclosing-writeback',    # a resolve drawn into a written-back image is written back too (its rectangle, deferred)
    'regpu-enclosing-big-pass',     # big-pass tiles find their image (pass width), last tiles clipped to it
    'regpu-writeback-safety',       # pass width only for big passes; late write-backs skip reused memory; stale write-back marks cleared
    'regpu-guest-lod-short-mips',   # short mip chains (separate images per level) sampled at the guest's mip choice above 1x
    'regpu-early-fence-writebacks', # early fences still wait for a deferred resolve write-back before them
    'regpu-worker-queries', # occlusion queries with draw workers: counting spans drawn on the command processor
    'regpu-zpd-scale',      # REGPU_ZPD_SCALE: live multiplier for occlusion query answers (FM3 sun glare size)
    'regpu-draw-thread-pieces', # draw thread runs the parallel segments; occlusion queries with the draw thread
    'regpu-livery-enclosing-recent', # resolves skip enclosing images left from earlier loads; write-back check per texel
    'regpu-msaa-color-clear-scale',  # colour clears through an MSAA view of a small 1x surface scaled (FM3 shade flicker)
    'regpu-debug-shot-dump'          # debug-server shots after a capture no longer dump every resolve
    'regpu-turbo-hyper'              # speed pieces for the launcher's presets (lean fetch v2, cull, helper scale, ...)
    'regpu-vertex-arena'             # large vertex buffers in VRAM, changes seen by the port's write watch (from FM2)
    'regpu-split-alpha-test'         # pixel shaders without the alpha-test discard when the test is off (early depth)
    'regpu-gpu-stats'                # REGPU_GPU_PASSES=2: pipeline statistics per draw (overdraw by pass and pixel shader)
    'regpu-seed-variants'            # first boot: seed pipelines use the alpha-test-free variant; don't wait for single slow shaders
    'regpu-fetch-variants'           # REGPU_FETCH_VARIANTS: pixel-shader variants with plain texture fetches (L30); REGPU_DUMP_ROOTSIG
    'regpu-draw-thread-cuts'         # draw thread: no mutex in IsPsEditAlias without live edits, fetch-variant lookups memoised per thread
    'regpu-shader-repeat-skip'       # CP: a shader load repeating the last of its type is not sent to the draw thread (REGPU_NO_SHADER_REPEAT_SKIP)
    'regpu-reg-skip'                 # CP: register writes that change nothing on the draw thread are not sent (L28, REGPU_NO_REG_SKIP)
    'regpu-async-present'            # frame end (close, submit, Present, fence) on the replay thread; the draw thread goes on (L12, REGPU_ASYNC_PRESENT)
    'regpu-draw-thread-memos'        # draw thread: vertex-arena answers memoised per thread/frame, one pass-scale snapshot, timer/overlay recorded (no replay wait)
    'regpu-window-fixup-union'       # REGPU_WINDOW_FIXUP_UNION: predicated per-tile window fixups merged outside a tiling pass (FM3 one guest tile)
)

function Invoke-Git { & git @args; if ($LASTEXITCODE -ne 0) { throw "git $args failed ($LASTEXITCODE)" } }

Invoke-Git submodule sync --recursive
Invoke-Git submodule update --init --recursive
# Patches and sources must be LF for `git apply`; the global core.autocrlf=true would make them CRLF.
foreach ($sm in @('thirdparty/XenonRecomp', 'thirdparty/ReGPU')) {
    Invoke-Git -C $sm config core.autocrlf false
}
Invoke-Git -C thirdparty/XenonRecomp submodule foreach --quiet 'git config core.autocrlf false'

# Idempotent check of a whole stack: build the tree "pinned commit + all patches" in a scratch
# index and compare it with the working tree (patches can overlap, so reverse-checking one at a
# time does not work once later ones are applied).
function Get-Tree([string]$repo, [scriptblock]$fill) {
    $idx = Join-Path ([IO.Path]::GetTempPath()) ("fm3-patch-" + [guid]::NewGuid() + ".idx")
    $env:GIT_INDEX_FILE = $idx
    try { & $fill | Out-Null; $t = (& git -C $repo write-tree); if ($LASTEXITCODE -ne 0) { throw 'write-tree failed' }; return $t }
    finally { Remove-Item Env:GIT_INDEX_FILE; Remove-Item $idx -ErrorAction SilentlyContinue }
}
function Set-PatchStack([string]$name, [string[]]$stack) {
    $repo = Join-Path $root "thirdparty/$name"
    $expected = Get-Tree $repo {
        & git -C $repo read-tree HEAD
        foreach ($p in $stack) {
            & git -C $repo apply --cached (Join-Path $root "patches/$p.patch")
            if ($LASTEXITCODE -ne 0) { throw "patch $p does not apply to the pinned $name commit" }
        }
    }
    $current = Get-Tree $repo { & git -C $repo read-tree HEAD; & git -C $repo add -A }
    $clean = Get-Tree $repo { & git -C $repo read-tree HEAD }
    if ($current -eq $expected) {
        Write-Host "${name}: all $($stack.Count) patches already applied"
    } elseif ($current -eq $clean) {
        foreach ($p in $stack) {
            & git -C $repo apply (Join-Path $root "patches/$p.patch")
            if ($LASTEXITCODE -ne 0) { throw "patch $p failed (CRLF checkout? see NOTES.md gotchas)" }
            Write-Host "patch $p : applied"
        }
    } else {
        throw "thirdparty/$name has changes that are neither upstream nor exactly our patch stack: inspect with git -C thirdparty/$name diff"
    }
}
Set-PatchStack 'XenonRecomp' $patches
Set-PatchStack 'ReGPU' $regpuPatches

if ($NoBuild) { return }
$vcenv = Join-Path $PSScriptRoot 'vcenv.cmd'
& cmd /c "`"$vcenv`" cmake -S tools/hosttools -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
if ($LASTEXITCODE -ne 0) { throw 'configuring build\tools failed' }
& cmd /c "`"$vcenv`" ninja -C build/tools"
if ($LASTEXITCODE -ne 0) { throw 'building build\tools failed' }
Write-Host 'host tools: build\tools\xenonrecomp\XenonRecomp\XenonRecomp.exe, ...\XenonAnalyse\XenonAnalyse.exe, build\tools\xexdump.exe'
