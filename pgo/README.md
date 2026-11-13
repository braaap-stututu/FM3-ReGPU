# ReGPU profile-guided optimisation (PERF-PLAN L23, session 32)

`regpu.profdata` is a clang instrumentation profile of ReGPU (the `regpu` library only) recorded on the frozen test
grid and a 12 s drive. Building ReGPU with it made the draw-thread-bound case about **3% faster** (1440p 4x with the
full perf log as the load: 262.5 -> 270.9 fps, 4 boots each, interleaved; PERF-RESULTS "Session 32"). The picture is
unchanged (same code, different layout/inlining).

## Use it
```
tools\vcenv.cmd cmake -S . -B build/rel -DFM3_PGO=use
tools\vcenv.cmd ninja -C build/rel fm3
```
`FM3_PGO=off` (the CMake default) builds without it. The profile was written by LLVM 23's `llvm-profdata`; an older
clang may not read it: then record your own (below) or leave it off.

## Re-record it (after larger ReGPU changes; stale functions are simply built without profile data)
1. `tools\vcenv.cmd cmake -S . -B build/rel -DFM3_PGO=gen`, then build. ReGPU is instrumented (slower).
2. Start the game with `LLVM_PROFILE_FILE=<folder>\regpu-%p.profraw` set and play what should be fast (the session-32
   profile: `Start-Grid` in `tools/perf_suite.ps1`, the cockpit camera, `Test-Drive`).
3. `Fm3 "env FM3_PGO_WRITE 1"` writes the profile (`src/pgo.cpp`; the game's exit may skip the runtime's own write).
4. `"C:\Program Files\LLVM\bin\llvm-profdata.exe" merge "-output=pgo\regpu.profdata" <folder>\*.profraw`
5. Configure with `-DFM3_PGO=use` and build.
