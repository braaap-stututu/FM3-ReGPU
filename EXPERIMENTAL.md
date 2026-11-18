# Experimental system (branch `experimental`, session 45)

The deep ReXenon and ReGPU reworks from session 44's "big performance improvements" question live here, apart
from the normal build, so a player can pick either one.

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
| X4 | **Constants on the GPU.** The guest constant file goes to the GPU as register-write deltas, and shaders read their constants from a per-draw snapshot index, so the CPU stops packing about 1 KB a draw into write-combined memory. | ReGPU | **Next.** Constants are 33% of the draw thread, and the write-combined ring copy is part of submit's 31% |
| X5 | **Optimising shader translator.** Values known per draw are folded in (beyond the fetch and bool specialisations, which were noise on their own), plus dead outputs, register pressure and the AI-car material shaders. Checked against the old output pixel by pixel. | ReGPU `hlsl.cpp` | Later; RGA statistics first |
| X7 | **Per-frame debug variables read only after a change** (`REGPU_EXP_ENV_GENERATION`). The CRT's getenv took ~10 us a call, and 1.8% of the draw thread went on five once-a-frame reads. | ReGPU patch `regpu-exp-env-generation` | Done: on 2 cores +0.9%, 6/6 rounds, 0 pixels; on by default in the experimental build |
| X8 | **Small vertex uploads without hashing on repeat use.** ReGPU's own `REGPU_UPLOAD_STABLE_REUSE` (sampled check): frozen grid on 2 cores +0.9%, 4/5 rounds, 0 pixels. It is not exact, though: a never-changed buffer first rewritten between two draws of a frame can be missed once. Made exact with the vertex arena's page write watch (`REGPU_EXP_UPLOAD_WATCH`, built and measured, then removed): -0.7%, 0/6 rounds, because the game's writes to armed pages fault | ReGPU | Exact version dropped; sampled version left opt-in (picture risk) |
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
