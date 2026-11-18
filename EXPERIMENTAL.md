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
| X1 | **ReXenon register optimisations.** r14-r31, f14-f31, v14-v31 and v64-v127 are kept in host locals (`non_volatile_as_local`; ReXenon's dataflow keeps the 196 functions that read them before writing on the context). Save/restore helpers are inlined, and calls to the 1,257 empty functions and the return-LR stores are left out. `sub_821F6AF8`/`sub_821F5DE8` stay on the context, because the frame-time hook hands the frame time back in r30/f31. | `tools/recomp.ps1 -Experimental` | Built, boots, frozen grid fine; measuring (below) |
| X2 | **Measure the draw thread per draw in a race** (line profile, 2 cores and full CPU): how much of DrawImpl is state derivation that repeats every frame, how much is constants, hashing, the WC ring store and pass scales. | `FM3_SAMPLE_THREAD=name:gpu draw worker`, `FM3_SAMPLE_LINES` | Next |
| X3 | **Draw-state cache.** The pipeline, descriptor tables, index/vertex views and pass scales built once for a draw's state and reused while that state (registers, fetch constants, texture serials) is unchanged. Only the constants are written per draw. | ReGPU `REGPU_EXP_STATE_CACHE` | Planned after X2 |
| X4 | **Constants on the GPU.** The guest constant file goes to the GPU as register-write deltas, and shaders read their constants from a per-draw snapshot index, so the CPU stops packing about 1 KB a draw into write-combined memory. | ReGPU | Design after X3 |
| X5 | **Optimising shader translator.** Values known per draw are folded in (beyond the fetch and bool specialisations, which were noise on their own), plus dead outputs, register pressure and the AI-car material shaders. Checked against the old output pixel by pixel. | ReGPU `hlsl.cpp` | Later; RGA statistics first |
| X6 | **More ReXenon:** condition-flag liveness, byte-swap elimination, cross-function inlining, better VMX128 mapping. The guest threads only limit weak CPUs (Steam Deck, 4 cores). | `thirdparty/ReXenon` (patches) | Later |

## Results

(see NOTES.md session 45 for the measurements as they come in)
