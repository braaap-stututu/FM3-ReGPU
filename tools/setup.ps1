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
    'regpu-cp-idle-sleep'            # REGPU_CP_IDLE_SLEEP_US: the command processor sleeps ~100 us steps on an empty ring (FM3: under a frame cap)
    'regpu-motion-flag'              # motion-vector pipelines get flag 0x800: flag 4 is also 'no depth clip' (those were built as motion variants)
    'regpu-seed-coverage'            # log 'seed coverage': pipelines drawn vs the host's pipeline seed
    'regpu-gpu-upload-ring'          # REGPU_GPU_UPLOAD_RING (live): the upload ring in a GPU upload heap (VRAM via resizable BAR)
    'regpu-sparse-fetch-table'       # per-draw fetch/texture entries written one by one (REGPU_FETCH_TABLE_BY_RANGE=1 old); REGPU_CONSTANT_REUSE, REGPU_TRACE_CONSTANT_BYTES
    'regpu-pass-epoch'               # PassScales' memo checked against one epoch (REGPU_PASS_SNAPSHOT_FULL_CHECK=1 old)
    'regpu-unread-block-parts'       # bool/loop constants, fetch layout, vuser written only for shaders that read them (REGPU_WRITE_UNREAD_PARTS=1 old)
    'regpu-packed-constants'         # float constants renumbered by rank, written as one run (REGPU_NO_PACKED_CONSTANTS=1 start-time: old)
    'regpu-perf-lite'                # light performance log: one record a frame through a lock-free queue to a log thread (REGPU_PERF_LITE=1)
    'regpu-pass-per-draw'            # PassScales: later asks in a draw compare only the epoch; ToHost asks once (REGPU_PASS_SNAPSHOT_PER_CALL=1 old)
    'regpu-fps-panel'                # fps counter + frame-time graph at the top centre (REGPU_FPS_PANEL=1, live)
    'regpu-long-frames'              # long-frame log with the light perf log (REGPU_LONG_FRAMES)
    'regpu-index-cache-sweep'        # index-list cache: stale lists freed gradually, not all at 8192 (once-a-lap 80 ms hitch)
    'regpu-texture-streaming'        # texture rows on helper threads, next texture heap made ahead, cubes in the pool, texture detail in long frames
    'regpu-4k-leads'                 # gamma pass drawn into the back buffer (default), VRAM + present-gap lines in the light log, test switches (plain multiply per shader, two-step resolve, draw-thread priority, present pacing)
    'regpu-surface-release'          # EDRAM surfaces unused for 3600 frames released (VRAM), REGPU_TRACE_SURFACE_USE
    'regpu-adapter-choice'           # REGPU_ADAPTER: the GPU to render on (launcher Graphics card); own pipeline library per non-default GPU
    'regpu-fetch-specialize'         # 2D fetches specialised by their fetch-constant words (car paint 117 -> 22 KB); draw-cost and plain-fetch traces; plain multiply in vertex shaders (off)
    'regpu-igpu-leads'               # L62 cube/3D fetches specialised too (TFetchK); L63 bilinear upscale (REGPU_UPSCALE_BILINEAR); 16-bit probe REGPU_EXP_MIN16 (off)
    'regpu-dxc'                      # REGPU_DXC=1 (off): translated shaders compiled by DXC to DXIL (dxc.cpp); 4K races +1.3%, iGPU races -16%
    'regpu-vs-corners'               # vertex shaders' point/rectangle path without a float4[3][16] array: no scratch (FXC), ~62 instead of ~240 VGPRs (DXC); REGPU_OLD_VS_CORNERS=1 old form
    'regpu-pso-lock'                 # REGPU_PSO_LOCK: pipeline-library loads/stores one at a time (default with DXC: AMD driver crashed on 5/16 cached DXC boots, 0/32 locked)
    'regpu-dxc-tuning'               # DXC -O1 default (same code, faster compiles), REGPU_DXC_SM; vertex-shader rectangle path with one GuestVS call site (VS compile -22% FXC, -43% DXC)
    'regpu-postaa'                   # anti-aliasing filters: FXAA 3.11 / SMAA / CMAA 2 (REGPU_AA_DIR sources), TAA, SSAA, EQAA (Config::postAA, ssaa, eqaa)
    'regpu-fsr4'                     # FSR contexts kept per setup (the FSR 3 driver timeout: contexts destroyed under the GPU), SDK 2.1 version descriptor, FSR 4 (Config::Upscaler::Fsr4)
    'regpu-motion-table'             # motion-vector bookkeeping in flat tables (FSR/TAA draw thread -4%), shader-pair memo, FSR 4 fallback message
    'regpu-ring-auto'                # REGPU_UPLOAD_RING_AUTO: upload ring in VRAM or system memory by measured frame time (opt-in, launcher)
    'regpu-scan-confirm'             # scanned shaders reported only once their bytes hold still for a second (no half-written junk compiles); REGPU_SCAN_NO_CONFIRM=1 old
    'regpu-cpu-cuts'                 # wake-ups between the command processor and the draw thread only for a waiter (CP -15% CPU), clock reads every 4096 calls, whole-buffer index views (REGPU_ALWAYS_NOTIFY / REGPU_NO_WIDE_INDEX_VIEWS: old)
    'regpu-variant-load'             # fetch-variant bytecode read from the disk cache by the compile worker, not mid-frame on the draw thread (REGPU_VARIANT_LOAD_NOW=1 old); REGPU_FRAME_HISTOGRAM=1 frame-time histogram in the light log
    'regpu-vr-screen'                # VR step 1 (REGPU_VR=screen, REGPU_OPENXR_DIR): OpenXR session on ReGPU's D3D12 device, the presented frame on a virtual screen in the headset
    'regpu-vr-headpose'              # VR step 2: regpu::vr::GetHeadPose / SubmitFramePose (host poses its camera; frames paired with their pose, shown as a mono projection layer), debug server findhex
    'regpu-vr-stereo'                # VR step 3: true stereo (REGPU_VR=stereo): the draw thread replays the frame from its first main-view draw for the second eye (clip x offset in the vertex shaders), both eyes to the headset
    'regpu-vr-hud'                   # VR: the HUD on its own panel in stereo (REGPU_VR_HUD_UNDER = the composite drawn under a HUD drawn first), regpu::vr::Recentre / F8
    'regpu-vr-pace-crop'             # VR: headset pacing (REGPU_VR_PACE: cap = headset rate, short wait for a due frame), each eye cropped to its lens, the HUD panel in mono cockpit, recommended-resolution log
    'regpu-vr-pose-tags'             # VR: frames paired with the poses they were drawn with by a tag in a main-view vertex constant (regpu::vr::SubmitTaggedPose), not by counting
    'regpu-triple-screen'            # Triple screen (REGPU_TRIPLE): the main view drawn three times (the stereo replay), turned per screen in the vertex shaders, side by side in a window three screens wide
    'regpu-vr-stereo-hold'           # VR: a pose tag older than 250 ms is not used (leftover constants), stereo/flat mixes counted in the log
    'regpu-triple-geometry'          # Triple screen: each screen's own projection (turn + off-axis: regpu::triple::View a b c d e) from the host's screen layout; bezel/gap/independent angles in the port
    'regpu-triple-span'              # Triple screen: fullscreen over the row of monitors beside the primary one (separate monitors, not only an Eyefinity/Surround desktop)
    'regpu-msaa-scale-64'            # A clear through a 4x view of a 64-bit colour target scaled to the layout its textured draws use (FM3 photo mode yellow with one guest tile)
    'regpu-compile-overlay-manifest'  # Shader cache loads no longer show the compile overlay; specialised pipelines written to the manifest once; seed coverage by base
    'regpu-big-pass-width'           # Big passes (surfaces wider than 1280) keep their own width, not pitch*64/65: vinyl edge gaps
    'regpu-resolve-into-own-base'    # A copy much narrower than the image at its own base is drawn into that image (FM3's CRX rear-bumper vinyl region at the livery's texel 0,0); trace of every copy's enclosing match
    'regpu-copies-by-range'          # A copy is drawn into every image whose memory holds its destination (tiled range, same pitch/format), not chosen by guessed rules; off-screen draws wait for their pipelines
    'regpu-depth-reset-color'        # A depth-only rectangle at the colour surface's EDRAM base also writes colour (shared EDRAM); debug dumps REGPU_DUMP_RESOLVES_AT / REGPU_DUMP_EACH_RESOLVE
    'regpu-hyper'                    # Hyper presets from the FM2 port's hyper-prototype (REGPU_HYPER, include/regpu/hyper.h): smart multiplies, swizzle selects, tail specialisation, culling with probe/hysteresis/pin, frozen-helper warm-up/refresh, resolve by size
    'regpu-igpu-intel'               # iGPU/Intel leads N2-N10 + compiles: UMA detection (REGPU_UMA_NO_ARENA, REGPU_UMA_TEXTURES), REGPU_ROOTSIG_11, REGPU_SPEC_CONSTANTS, REGPU_HALF_REGS (DXC 16-bit), background compile throttle, REGPU_PIPELINE_THREADS / _LIBRARY_TAG
    'regpu-hyper-mesh'               # Hyper's mesh decimation + vertex-cache order with meshoptimizer (REGPU_MESHOPT_DIR; REGPU_HYPER_DECIMATE, REGPU_HYPER_VCACHE)
    'regpu-xess'                     # Intel XeSS upscaler (Upscaler::Xess; libxess.dll loaded at run time; REGPU_XESS_DIR headers), one context per setup
    'regpu-less-ram'                 # Less RAM: memory report line, texture budget with eviction, upload ring's other place made only when used, pipeline library mapped from its file, k_8 kept as R8
    'regpu-exp-state-trace'          # Experimental system (session 45): REGPU_EXP_TRACE_STATE, how often a draw's state is unchanged but its float constants
    'regpu-exp-env-generation'       # Experimental system (session 45): REGPU_EXP_ENV_GENERATION, per-frame debug variables re-read only after the environment changed (g_envGeneration)
    'regpu-exp-gpu-constants'        # Experimental system (session 45): REGPU_EXP_GPU_CONSTANTS (start-time, off): draws' float constants rebuilt on the GPU from a register log (correct, slower: kept off)
    'regpu-resolve-dump-frame-env'   # REGPU_DUMP_EACH_RESOLVE read once a frame (FrameEnv), not by getenv per resolve (~1.5% of the draw thread; for main too)
    'regpu-exp-lazy-loops'           # Experimental system (session 45): REGPU_EXP_LAZY_LOOPS (loop constants only for shaders that read them, bools/loops byte-swapped 4 at a time); PassScales' check inlined
    'regpu-exp-gpu-constants-staging' # Experimental system (session 45): REGPU_EXP_GPU_CONSTANTS=2, the draw data staged in system memory (correct; CPU even, GPU +0.8 ms: off)
    'regpu-exp-repeat-trace'         # Experimental system (session 45): REGPU_EXP_TRACE_STATE also counts draws whose whole state repeats an earlier draw's in the frame (~40% in a race)
    'regpu-exp-trace-parts'          # Experimental system (session 46): REGPU_EXP_TRACE_STATE also counts repeats per state part, null-PS candidate draws and the draw thread's waits
    'regpu-exp-index-watch'          # Experimental system (session 46): index buffers not rehashed while no page was written (write watch); 2-core races +1.7%
)

function Invoke-Git { & git @args; if ($LASTEXITCODE -ne 0) { throw "git $args failed ($LASTEXITCODE)" } }

Invoke-Git submodule sync --recursive
Invoke-Git submodule update --init --recursive
# Patches and sources must be LF for `git apply`; the global core.autocrlf=true would make them CRLF.
foreach ($sm in @('thirdparty/XenonRecomp', 'thirdparty/ReGPU', 'thirdparty/ReXenon')) {
    Invoke-Git -C $sm config core.autocrlf false
}
Invoke-Git -C thirdparty/XenonRecomp submodule foreach --quiet 'git config core.autocrlf false'
Invoke-Git -C thirdparty/ReXenon submodule foreach --quiet 'git config core.autocrlf false'

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
# ReXenon (session 41): the other recompiler (tools\recomp.ps1 -Recompiler rexenon), with its own small stack.
Set-PatchStack 'ReXenon' @(
    'rexenon-vsldoi128'              # Primary opcode 4 with bit 4 set decoded as vsldoi128, not a PPC405 MAC form (FM3's 8284AA90)
)

if ($NoBuild) { return }
$vcenv = Join-Path $PSScriptRoot 'vcenv.cmd'
& cmd /c "`"$vcenv`" cmake -S tools/hosttools -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
if ($LASTEXITCODE -ne 0) { throw 'configuring build\tools failed' }
& cmd /c "`"$vcenv`" ninja -C build/tools"
if ($LASTEXITCODE -ne 0) { throw 'building build\tools failed' }
Write-Host 'host tools: build\tools\xenonrecomp\XenonRecomp\XenonRecomp.exe, ...\XenonAnalyse\XenonAnalyse.exe, build\tools\xexdump.exe'
& cmd /c "`"$vcenv`" cmake -S thirdparty/ReXenon -B build/tools-rexenon -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
if ($LASTEXITCODE -ne 0) { throw 'configuring build\tools-rexenon failed' }
& cmd /c "`"$vcenv`" ninja -C build/tools-rexenon XenonRecomp XenonAnalyse"
if ($LASTEXITCODE -ne 0) { throw 'building build\tools-rexenon failed' }
Write-Host 'ReXenon tools: build\tools-rexenon\XenonRecomp\XenonRecomp.exe, ...\XenonAnalyse\XenonAnalyse.exe'
