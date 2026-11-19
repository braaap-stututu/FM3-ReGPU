# Experimental system (branch `experimental`, session 45)

The deep ReXenon and ReGPU reworks from session 44's "big performance improvements" question live here, apart
from the normal build, so a player can pick either one.

## Where to continue (end of session 46)
Session 46 ran Fable's quick measurements and the FM2 cuts that applied (results X11-X17 below). **Nothing proved
out:** floats on the replay thread (X11, kill test: no gain), the spinning ring wait (X12, -4%), a null pixel shader
for depth-only draws (X13, only 3-4% of draws), FM2's binding revival, recent-pipeline table and plain frame counters
(X16, noise on this CPU), and index buffers by write watch (X15, +1.5% once, -1.1% in another boot). Kept: the trace
counters (patch `regpu-exp-trace-parts`). On this CPU the draw thread has no single cost left above ~4% (X17 line
profile and call stacks): the pipeline lookup ~5.7% in all, index hashing ~5% (ceiling +1.3-2.9% with a sampled,
inexact hash), LiveSwitch loads ~3.6%, WindowScissor/pass scales ~2.9%. Next leads, each small: an exact but
cheaper index hash, Fable's "retail DrawImpl" (switches read once a frame), and a pipeline key built only when its
registers changed (pipeline part new in only 13-17% of draws, X14).

## How to choose

- **Launcher:** Settings > Testing > **Experimental build** (`experimental=1` in `settings.ini`). The launcher then
  starts `build\exp\fm3.exe` (source checkout) or `experimental\fm3.exe` (an install) instead of the normal exe. If
  the experimental exe isn't there, the normal one runs. Saves, settings, shader caches and logs are shared.
- **play.cmd:** `set FM3_BUILD=exp` before starting it.
- **Benchmarks:** `tools\bench_matrix.ps1 -Variants "rel:FM3_BUILD=rel|exp:FM3_BUILD=exp"`. With
  `tools\perf\affinity_watch.ps1 -Mask 0x0F` running beside it, the game is held to 2 cores (4 logical CPUs). That
  stands in for a weaker CPU, so CPU-side changes show in fps.
- The log's second line says `fm3: experimental build (...)` when the experimental exe runs.

## How to build

1. `tools\recomp.ps1 -Experimental` writes `out\ppc-rexenon-exp` from `config\fm3.toml` with the experimental
   recompiler options (config `config\fm3_rexenon_exp.toml`, log `out\recomp-rexenon-exp.log`). It takes about 6 s.
2. The first time, configure `build\exp` like `build\rel`, plus `-DFM3_EXPERIMENTAL=ON`:
   `tools\vcenv.cmd cmake -S . -B build/exp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link -DCMAKE_AR=llvm-lib -DCMAKE_MT=llvm-mt -DFM3_PGO=use -DFM3_RECOMPILER=rexenon -DFM3_EXPERIMENTAL=ON`
3. `tools\vcenv.cmd ninja -C build/exp fm3` (the game must be closed).

`FM3_EXPERIMENTAL` (CMake) builds from `out/ppc-rexenon-exp` and defines `FM3_EXPERIMENTAL=1` for the game sources.
Experimental renderer code goes into ReGPU behind `REGPU_EXP_*` switches (ReGPU's rule: game-neutral, switchable).
`src/gpu_host.cpp` turns them on by default only in the experimental build, so the normal build is unchanged and
each piece can still be A/B'd live in the experimental one.

## Work list

| # | Piece | Where | Status |
|---|---|---|---|
| X1 | **ReXenon register optimisations.** r14-r31, f14-f31, v14-v31 and v64-v127 are kept in host locals (`non_volatile_as_local`; ReXenon's dataflow keeps the 196 functions that read them before writing on the context). Save/restore helpers are inlined, and calls to the 1,257 empty functions and the return-LR stores are left out. `sub_821F6AF8`/`sub_821F5DE8` stay on the context, because the frame-time hook hands the frame time back in r30/f31. | `tools/recomp.ps1 -Experimental` | Built; races on 2 cores +1.75% (+4.4% at equal draws), p99 -3% (below); a drive is still to be done |
| X2 | **Measure the draw thread per draw in a race** (line profile, 2 cores and full CPU): how much of DrawImpl is state derivation that repeats every frame, how much is constants, hashing, the WC ring store and pass scales. | `FM3_SAMPLE_THREAD=name:gpu draw worker`, `FM3_SAMPLE_CALLERS`, ReGPU `REGPU_EXP_TRACE_STATE` | Done (below) |
| X3 | **Draw-state cache.** The pipeline, descriptor tables, index/vertex views and pass scales built once for a draw's state and reused while that state (registers, fetch constants, texture serials) is unchanged. Only the constants are written per draw. | ReGPU | **Dropped in this form:** only 10.5% of a race's draws keep the previous draw's state (X2). A cache across frames (the same draw in the next frame) would be the only version worth building; not yet designed |
| X4 | **Constants on the GPU.** The guest constant file goes to the GPU as register-write deltas, and shaders read their constants from a per-draw snapshot index, so the CPU stops packing about 1 KB a draw into write-combined memory. | ReGPU patch `regpu-exp-gpu-constants` (`REGPU_EXP_GPU_CONSTANTS`, off) | **Stage 1 built and correct, but slower** (results below). Kept off |
| X5 | **Optimising shader translator.** Values known per draw are folded in (beyond the fetch and bool specialisations, which were noise on their own), plus dead outputs, register pressure and the AI-car material shaders. Checked against the old output pixel by pixel. | ReGPU `hlsl.cpp` | Later; RGA statistics first |
| X7 | **Per-frame debug variables read only after a change** (`REGPU_EXP_ENV_GENERATION`). The CRT's getenv took ~10 us a call, and 1.8% of the draw thread went on five once-a-frame reads. | ReGPU patch `regpu-exp-env-generation` | Done, on by default in the experimental build. **Correction:** its +0.9% was probably noise. The getenv cost stayed (1.5%) and came from the resolve path, fixed by `regpu-resolve-dump-frame-env`: 2-core grid draw thread 1.28-1.31 vs 1.32-1.36 us a draw, and it applies to main too |
| X8 | **Small vertex uploads without hashing on repeat use.** ReGPU's own `REGPU_UPLOAD_STABLE_REUSE` (sampled check): frozen grid on 2 cores +0.9%, 4/5 rounds, 0 pixels. It is not exact, though: a never-changed buffer first rewritten between two draws of a frame can be missed once. Made exact with the vertex arena's page write watch (`REGPU_EXP_UPLOAD_WATCH`, built and measured, then removed): -0.7%, 0/6 rounds, because the game's writes to armed pages fault | ReGPU | Exact version dropped; sampled version left opt-in (picture risk) |
| X9 | **Loop constants only for shaders that read them** (`REGPU_EXP_LAZY_LOOPS`, from a line profile: 1.7% of the draw thread), bools/loops byte-swapped 4 at a time, and PassScales' check inlined. | ReGPU patch `regpu-exp-lazy-loops` | Done: +1.0%, 6/6 rounds, 0 pixels; on by default in the experimental build |
| X10 | **PGO of the recompiled game code** (CMake `FM3_PGO_PPC=gen/use`, profile `pgo/ppc.profdata` from one AI benchmark race). Session 37 had tried it before ReXenon (+0.4%). | CMakeLists.txt | **No gain, dropped:** 3 rounds on 2 cores, -0.24% at equal draws (plain -5.0% from different draw counts). The option stays, without a profile |
| X11 | **Floats and ring writes on the replay thread** (Fable's idea 1). Kill test `REGPU_EXP_SKIP_CONSTANTS=1` (draw thread copies no float constants; picture wrong). | ReGPU | **Ruled out:** 2-core races 1.33 us a draw (base) vs 1.32-1.35 (no floats), under Fable's 0.15 us bar. A second level that also skipped the rest of the blocks hung the GPU (garbage fetch layouts: two driver timeouts during the matrix); removed with the switch |
| X12 | **Ring wait** (Fable's idea 2): `FM3_RING_WAIT=0` (spin) vs the default sleep. | `src/hooks/ring_wait.cpp` | **Default kept:** spin 1.37-1.39 vs 1.33 us a draw on 2 cores (-4% fps); on 8 cores every mode hits the 360 cap. The draw thread's waits for the command processor (`exp waits`, trace on): 0.3% of the time |
| X13 | **Null pixel shader for depth-only draws** (Fable's idea 3): counter `exp null ps`. | ReGPU patch `regpu-exp-trace-parts` | **Not worth building:** depth-only draws are 3-4% of race draws (96-98% of them would qualify), so at most ~1% |
| X14 | **Repeats per state part** (Fable's idea 4): `exp state groups` lines. | ReGPU patch `regpu-exp-trace-parts` | Measured (race, 8 cores): surface/viewport same as the draw before 95-96%, earlier in the frame 1.5%; pipeline 51-55% / 29-34%, new 13-17%; fetch constants 20-25% / 21-28%, **new 49-56%**; bool/loop 99.8%; other registers 80-85% / 9-14%. Most parts already have memos (pass scales, last pipeline, the slot memo at 89% hits), which is why X16 found nothing |
| X15 | **Index buffers by write watch:** an index buffer none of whose pages was written since its last hash keeps that hash (the vertex arena's `Host::watchArm/watchClean`). Session 31 built the same on 8 cores (+0.6%, removed). | ReGPU (built, removed) | **Not proven, removed.** The ceiling is real: a sampled hash (`REGPU_INDEX_SAMPLED_HASH`, not exact) gave +2.9% (8/8) and +1.3% (6/6) on the 2-core grid. The watch: +1.5% (6/8), then -1.1% (10 rounds) in a later boot; 2-core races +1.7% (sd 5-9 fps); watching the index-list path too (hashed per draw) -0.2%. The watch's own check (`WriteWatchClean`, a lock and a page walk) costs most of what the hash did; a verify race found 0 misses. Leave it; an exact cheaper hash or a lock-free page check would be the way back |
| X16 | **FM2's per-draw cuts** (`bee437d9c`, `0b2a10831`): texture bindings revived after a resolve (2% of slot lookups), a 64-entry recent-pipeline table, plain adds for the frame counters (the profile's 7.6% "closing brace" of DrawImpl). | ReGPU | **All within noise on the 2-core grid** (-0.3%, +0.1%, -0.7%; null A/B -0.1/+0.8%), pictures identical; removed. FM2 measured them on an FX-8350, whose locked ops and 256-bit stores are slow; this CPU doesn't pay that. Our code already had most of the rest (shader front cache, lazy bools/loops, shader-load repeat skip, edit lock) |
| X17 | **Draw-thread line profile** (2-core grid, session 46). | `FM3_SAMPLE_LINES=1` | Flat: DrawImpl's end 7.6% (where the write-combine drain lands), index hashing ~5% (`FullHashAvx2` + `GetIndexBuffer`; X15), `ContentHash` 2.6%, atomic bool loads 3.6% (LiveSwitch checks), the float copy 2.6%, RingAlloc's compare-exchange 1.3%, D3D12 0.9-1.3% |
| X6 | **More ReXenon:** condition-flag liveness, byte-swap elimination, cross-function inlining, better VMX128 mapping. The guest threads only limit weak CPUs (Steam Deck, 4 cores). | `thirdparty/ReXenon` (patches) | Later |

## Results

- **X1, session 45:** benchmark races, the user's 1440p 4x settings, 2 rounds each, game held to 2 cores
  (`affinity_watch.ps1 -Mask 0x0F`; `out/bench/matrix-exp-2cores-20261118-220048`):

  | Build | fps | p99 | Long frames |
  |---|---|---|---|
  | Normal | 295.6 | 5.68 ms | 92.5 |
  | Experimental | 300.7 | 5.50 ms | 69.5 |

  That is **+1.75% fps, and +4.4% at equal draw counts**. GPU time is the same (2.06 vs 2.09 ms). On the full CPU
  the frozen 720p grid sits at the 360 cap with both builds (exe 37.8 vs 40.1 MB). Picture: the frozen grid is
  normal. A drive is still to be done.
- **X2, draw-thread split** (session 37's full perf log, 1440p, 2,688 draws, 3.40 ms a frame):

  | Part | ms a frame | Share |
  |---|---|---|
  | Constants (building vc/pc from the registers) | 1.13 | 33% |
  | Submit (ring copy into write-combined memory + D3D12 recording) | 1.07 | 31% |
  | Indices | 0.33 | |
  | Textures | 0.32 | |
  | Pipeline | 0.19 | |
  | Shaders | 0.14 | |

  Inside constants: floats 0.23, bools 0.10, vertex fetches 0.31, viewport 0.12, index buffer 0.06, rest 0.18.
  Recorded calls a frame: about 2 root CBVs a draw, a pipeline change every second draw.
- **X2, session 45 profile** (experimental build, a full-CPU benchmark race, `FM3_SAMPLE_CALLERS=2`, no perf log;
  `out/bench/matrix-exp-profile2-*`):

  | Where the draw thread's time goes | Share |
  |---|---|
  | DrawImpl's own code | 34.7% |
  | Waiting for work | 11.8% |
  | Index buffers (GetIndexBuffer) | 4.4% |
  | Content hashing | ~6% |
  | Pipelines (GetPipeline + key) | ~3.4% |
  | getenv | 1.8% (fixed: X7) |

  Everything else is under 1.1% each (pass sizes, scissor, ring allocation, resolves). The full perf log by itself
  is ~30% of the samples (timer reads and atomic adds), so draw-thread profiles must be taken with it off.
- **Same state as the draw before** (`REGPU_EXP_TRACE_STATE`, race, ~2,400 draws a frame):

  | What changed since the draw before | Share of draws |
  |---|---|
  | Only the float constants | 10.5% |
  | Fetch constants only | 24% |
  | Other registers (with or without fetch/bool/loop) | 24% |
  | A new shader | 42% |

  Bool/loop constants alone never change. Registers 0x2080-0x2082 are written about 1.7 times a draw, then 0x057F
  and 0x2180 (37% each) and the 0x2200-0x2208 block. A previous-draw fast path is therefore weak.
- **Command processor** (ReGPU's PM4 thread, guest thread 9, race, `out/bench/matrix-exp-cpprof-*`): waiting or
  sleeping ~47%, ExecutePacket's own code ~31%, memcpy 5%, atomic stores 4%. No large single cost.
- **Where this leaves the deep work (session 45):** every measured draw-thread and CP cost is now 1-5%. The
  remaining big lever is X4 in full: the guest's constants on the GPU and the per-draw blocks no longer packed by
  the CPU. That means a shader preamble and root signature change, every shader recompiled, and weeks of work. The
  user decides between that, more small wins, or ReXenon (X6) for weak CPUs.
- **X4 stage 1, GPU constants** (session 45):
  - **How it works:** the draw thread logs the float-constant runs it applies, plus an 8 KB copy every 128 draws, and
    writes a 64-byte record per draw. Per command list, an ExecuteIndirect compute pass rebuilds every draw's whole
    VS/PS block in a GPU buffer, and the shaders are unchanged.
  - **Correctness:** the CPU check (`REGPU_EXP_GPU_CONSTANTS_CHECK`) found 0 differences in 25 M draws, and the grid
    picture is right (cross-boot diff only in the crowd).
  - **Speed, 2-core frozen grid, alternating boots:** off 265 / 272 fps, on 257 / 258 (about -4%).
    - Draw thread: 1.33 vs 1.40 us a draw.
    - GPU: 2.2 vs 2.67 ms. With a group per draw-state invalidation it was ~30 groups and 3.08 ms; every group's two
      output-buffer transitions drain the GPU.
    - The bookkeeping per draw (list lookup, log copy, record) costs as much as the float packing it replaces. The
      ring bytes it saves (966 -> ~560 a draw) don't show as time.
  - **What a stage 2 would need to pay off:** the rest of each block (fetch, viewport, flags) built on the GPU too.
    That logic is ReGPU's most game-specific code (MSAA view scaling, fetch translation). Reading the floats through a
    second CBV would drop the tail copy (a preamble change, all shaders recompiled). Not started: the user decides.
- **One unexplained crash** (session 45, experimental build, GPU constants off, the draw-thread sampler on): two
  shader-compiler threads jumped through a corrupted C++ locale table while opening a file (`std::ifstream` ->
  `locale::locale`), 15 s into the boot. Not seen again in the next 2 boots; watch for it.
- **X4 mode 2, draw data staged in system memory** (`REGPU_EXP_GPU_CONSTANTS=2`, session 45):
  - **Why it was tried:** a line profile put DrawImpl's closing brace at 8.4% of the draw thread, the stores waiting
    for the write-combined ring writes to drain to VRAM.
  - **Correctness:** 15.5 M checked draws, 0 differences.
  - **Speed:** the stall fell to 4.7%, but the bookkeeping takes the same time back. Draw thread 1.29-1.37 vs
    1.33-1.35 us a draw; GPU +0.8 ms (each block crosses PCIe once more). Off.
  - **Conclusion for X4:** neither mode pays. The GPU-constants code stays in, off, for later ideas.
- **Benchmark races, 2 cores, after the resolve fix** (also on main; 2 rounds, but the experimental build's first
  race was closed by a WM_CLOSE from outside the game 4 s after GO, cause unknown, not seen since):
  - Experimental 268.4 fps (one race) vs normal 285.1 (sd 8.6).
  - At equal draw counts +1.6%; GPU +3.6%; long frames 57 vs 146.
  - Inconclusive: rerun with 3 rounds (`out/bench/matrix-exp3-2cores-*`).
- **Rerun, 3 rounds each, all 6 races complete** (`out/bench/matrix-exp3-2cores-20261119-001912`; experimental =
  X1 + X7 + X9, both builds with the resolve fix):

  | | Normal | Experimental |
  |---|---|---|
  | fps | 290.6 (sd 20.4) | 296.2 (sd 25.1) |
  | p99 | 6.03 ms | 5.84 ms |
  | Long frames | 143 | 99 |
  | GPU | 2.15 ms | 2.16 ms |

  That is **+1.9% fps and +5.1% at equal draw counts**, the same direction as the first matrix (+1.75% / +4.4%).
  ReXenon's README warns that code placement alone moves fps 3-10% between relinks, so the plain figure is within
  that. The equal-draw figure and the p99 / long-frame improvement held in both matrices.
