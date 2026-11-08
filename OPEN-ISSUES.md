# Open issues

State after session 9 (2026-11-08). The vinyl issues B (vinyls cut along a straight line) and B2 (races: a car with vinyls
covered in noise) were fixed in session 11, with issue H (a vanishing car); everything vinyl-related, including the
small leftover gaps, is in [VINYLISSUES.md](VINYLISSUES.md). This file
lists everything else that is known to be open, so it can be picked up in any order.

Each entry gives: what you saw (with the on-screen timer value of your screenshot, which is the `[time]` in the log of
that run, `out/logs/session9-explore.log`), what the log and the code already show, what the FM2/FM4 ports did, and a
first step. "Live" means it can be tried with `tools/fm3ctl.ps1` while the game runs, without a rebuild.

Fixed in session 9 and **not** in this list: A (matte paint, black chrome), C (My Cars picture and thumbnail), the
race-load crash. See NOTES.md for those.

## Rendering

### D. Upper third of the screen slightly washed out (pre-race menu, photo mode)
- **Seen:** pre-race menu (timer 119.3) and photo mode (1219.9). A lighter band over the top of the 3D view, with a
  straight lower edge at about row 262 of 720. Not seen while racing.
- **Known:** nothing in the log yet. Both screens show the 3D scene behind a menu, or a paused, re-rendered scene,
  so the scene is probably drawn or composed differently from racing.
- **Sibling ports:** FM4 had photo mode's 64-bit picture accumulation sharing EDRAM with the 32-bit scene, which bled
  into the photo. It fixed this with `REGPU_COLOR_SURFACE_PER_BPP`, which exists only in FM4's own ReGPU and would
  need porting. FM2 fixed several photo-mode tile problems.
- **First step:** in the pre-race menu (easier to reach than photo mode), pause, then `Fm3 "capture"`. Find the
  pass that writes rows 0 to about 262 (FM3's scene tiles are rows 0–511 and 512–719, so 262 is not a tile edge;
  check for a 64-bit surface sharing an EDRAM base). Live A/B: `switch REGPU_NO_TILE_MERGE 1`,
  `switch REGPU_NO_CLEAR_EXTEND 1`. Measure the band with `tools/analysis/boxes.py` (one box above the edge, one
  below), not by eye.

### E. Flickering shadow on the player's car from other cars
- **Seen:** intro race (266.2). A shadow patch flickers on the right rear of the R8 when other cars are near. It
  shouldn't be there.
- **Known:** session 8 fixed the car's own soft shadow (no square any more). The cars' shadow cascades have not been
  checked against other cars.
- **Sibling ports:** FM4 fixed a close match. Its soft car shadow has three tiles, and the tile holding other cars'
  silhouettes is built from a depth copy that the shader reads as packed 8888 bytes. It needed `REGPU_DEPTH_AS_8888`
  plus `REGPU_CASCADE_*` switches, which exist only in FM4's ReGPU (`Fuzzy-Rittersport-4-ReGPU/src/gpu_host.cpp`
  lines 113–125, and its vendored `renderer.cpp`). FM4 also fixed a "stray shadow" from depth bases shared between a
  1× caster pass and a 4× pass.
- **First step:** in a race with cars close by, pause, `Fm3 "capture"`. Find the car-shadow shader and its textures,
  and check whether one is a depth resolve fetched as k_8_8_8_8. ReGPU already returns a depth image's guest D24S8
  words when it's fetched as 8888 (`REGPU_NO_DEPTH_PACK`), so check that path works for this copy before porting
  FM4's switches.

### F. Dithering / stipple on foliage and fences
- **Seen:** 365.8, on trees and fences; it looks like anti-aliasing.
- **Known:** our build renders at host MSAA 1× (log: `renderer: anti-aliasing 1x`). At 1×, upstream ReGPU emulates
  alpha-to-coverage with a 2×2 dither, as Xenia does. That is the stipple.
- **Sibling ports:** FM4 uses plain D3D alpha-to-coverage (a hard cut at alpha 0.5, no dither). FM2 the same.
- **First step (live, 1 minute):** in a race near trees, `switch REGPU_NO_ALPHA_DITHER 1` and compare screenshots.
  Also try a higher host MSAA (`set FM3_MSAA=4` before starting, or the `msaa` key in the settings file read by
  `src/settings.cpp`; needs a restart): with real MSAA the coverage is smooth. Decide between hard edges (`REGPU_NO_ALPHA_DITHER=1` as an FM3 default in `src/gpu_host.cpp`) and MSAA.

### Smaller rendering notes
- The k_2_10_10_10 decoder (session 9) keeps 8 of 10 bits. If reflections ever show banding, add a native
  `R10G10B10A2_UNORM` upload (the cube path always decodes to 8-bit today).
- The car paint also binds a second reflection cube (`1832E000` in the session-9 showroom capture) that is all
  zero in memory and never resolved into while in the showroom. It may be unused there, or only live in races. The
  showroom looks right without it; worth a look if race reflections ever look wrong.

### C1. Crash on some race loads: `std::out_of_range` while picking AI drivers (session 14)
Twice on Camino Viejo de Montserrat (the sun-flare test track; once before and once after the draw-thread change, so not caused by it); a retry loaded fine once. Log `out/logs/session14-aiplayers-crash.log` (guest stack now logged by `src/kernel/imports_rtl.cpp`): `FATAL: guest raised exception E06D7363`, ThrowInfo `8219A290` = `std::out_of_range`. Stack: `sub_82538110` (_CxxThrowException) ← `std::_Xout_of_range` (ends at `825450B0`) ← `sub_821F1420` (`std::string::assign(src, pos, n)`, `pos` > length) ← `sub_828AFD40` (takes a string from a virtual call at vtable+108, `.substr(2)`, then `sscanf("%u")`) ← `sub_82777AF8` ← `sub_822A1150` (AI driver setup: `SELECT Name FROM AIPlayers WHERE Id=`, `UPDATE AIPlayers SET isUsed = 1 WHERE Id=`) ← `822E0960` ← `822E18F0` ← `8231C010` ← `8237F818`. So a database value (an Id string with a 2-character prefix) came back shorter than 2 characters. **Next:** hook `sub_828AFD40` to log the string (and the query) when it is short; check whether the AIPlayers `isUsed` flags run out over many races (a save-side state), or whether the database read itself is wrong (our file/threading layer); compare with hl360 on the same save.

## Audio

### G. Engine sound cuts out while holding RPM
- **Seen:** 1027.1, holding revs in a solo race. You mentioned it's known across the 360 recomps.
- **Known (strong lead):** our XMA stats (`xma: 5 s:` log lines) show **0 "discontinuity flushes" in menus,
  110–200 per 5 s in a solo race, 500–730 in the 6-car intro race**. Each one comes with codec reopens and 4–11k
  decoded samples dropped at "stream restarts". So our seek guard flushes the decoder about 30 times a second while
  driving and throws away audio already decoded. Both behaviours are ours (session 7) and have live switches.
- **Sibling ports:** we already carry FM4's queue depth, top-up, decoder boost and "consumed means delivered". FM2
  additionally decodes engine loops with a fresh codec per restart, plus a seamless-loop table and crossfades
  (`Forza-Motorsport-2-ReGPU/src/audio/xma.cpp` lines 81–111 and 413–459, `seamless_loops.h`).
- **First step (live):** in a race, hold revs, and listen with `switch FM3_NO_XMA_DROP_AHEAD 1`, then also
  `switch FM3_NO_XMA_SEEK_GUARD 1`. Watch the `xma: 5 s` lines. If cut-outs stop with drop-ahead off, the guard's
  "this packet doesn't follow" test is too strict for engine loops (`src/audio/xma.cpp` around line 825).
  `FM3_TRACE_XMA=1` (set before starting) logs each flush with its buffer and packet numbers.

## Content and system (Phase 4)
- **Disc 2 DLC cars** (FM3-QUIRKS Q11–Q14): Disc 2 content must be installed (no disc swap), the licence mask must
  grant all bits, header-less packages listed twice, `fileName` may be unterminated. Not tested yet. The log shows
  `XamSwapDisc` / `XamSwapCancel` unimplemented (timer 2070): the game asked for a disc swap.
- **Profile rules** (Q15: XUID and gamertag constraints) and **language/country** (Q16): not checked.
- **Unimplemented imports seen:** `XamParseGamerTileKey`, `XamReadTileToTexture` (gamer picture, 1598.9),
  `XamShowMarketplaceUI` (2077). Harmless so far; stub them properly when Phase 4 gets there.
- **Title update files:** every race load logs `vfs: no mapping for "Update:\media\cars\driver\*.gr2"` (steering
  lock and shifter models). We don't mount an `Update:` drive. Check whether a title update exists for this
  version and whether these files matter (driver-view animations?).
- Career and menus beyond what session 9 visited: not explored.

## Performance and parity (Phase 5)

### P. Frame rate drops near other cars above 60 fps (session 13)
**Test 1 done, now the default:** `REGPU_NO_FENCE_ORDER=1` live A/B near cars (1440p 4x MSAA, 120 cap, ~2,800–3,400 draws): off 69–77 fps, on **100–120** (two rounds), slowest frame 17–19 → 11–13 ms, CP wait on `18C27014` ~840 → ~410 ms per 2 s. No visual change (the spectator LOD popping you saw is the same with it off). `src/gpu_host.cpp` sets it via `regpu::driver::SetSwitch` (a static global, env too late); `FM3_FENCE_ORDER=1` keeps the ordering. **Session 13 later: early fences fixed and made default; per-draw switches default.**
- `REGPU_TEXTURE_CHECK_STAGGER` + `REGPU_DIRECT_DYNAMIC_INDICES` (live A/B, paused grid 3,516 draws): 108 → 120 fps; driving clean. Defaults (`FM3_NO_DRAW_SHORTCUTS=1` undoes). `REGPU_UPLOAD_SAMPLED`, `REGPU_DYNAMIC_EXPIRE`: no effect, not used.
- Draw workers (`REGPU_DRAW_WORKERS=3`, live `REGPU_NO_DRAW_WORKERS`): +2–5% alone (2.45x parallel translation, but translation was no longer the bound); occlusion answers a fixed 1000 → **no sun flare**. Not used.
- Render thread profile with those defaults: still ~78% in D3D's block-on-fence (`sub_824B5048` → `sub_824B4F78`; device `*(0x400005D0)` = `0x40100080`, +0x2A90 → fence word `B8C26000`, +0x2A9C issued fence number; completed fence lagged 34–134 behind). **720p 1x = same fps as 1440p 4x** (114–127 vs 117–118): fence latency, not the GPU.
- **The earlier "early fences +5–8%" A/B was invalid:** `REGPU_EARLY_FENCES` at start also makes `gpu.cpp` `WriteBack` write every fence at once (`RendererEarlyFences()`, read once), whatever the live switch says. Real numbers (paused grid): late 117–118, early 141–142, early + workers 150.
- **Livery bug under early fences = a ReGPU bug:** `RendererFenceSubmit` returns a real fence when a deferred write-back (our livery) must land first, and `WriteBack` replaced it with 0. New patch **`regpu-early-fence-writebacks`** (12th) uses the returned fence (`REGPU_EARLY_FENCES_SKIP_WRITEBACKS=1` = old). Result: livery intact, paused grid **147–153 fps** (vs 117–118), driving clean. **Early fences are now FM3's default** (`FM3_LATE_FENCES=1` undoes). The same line is in upstream ReGPU, FH1's fork and PGR4 (which works around it with truthful fences): worth reporting upstream.
- **Draw workers now on by default** (`REGPU_DRAW_WORKERS=3`, your call: performance over the sun flare): behind an AI car (3,670 draws, paused) 141–142 → 153.5–156 fps (+9%), livery intact with early fences. No sun flare/glare with them (occlusion answers a fixed 1000). Option **`accurate_sun=1`** in settings.ini / launcher Graphics "Accurate sun flare" turns them off (tooltip: ~10% fps near cars). Fixing occlusion with workers (PGR4 3cbd0f4 vs our `regpu-zpd-tile-spans`) would give both.
- Now the command processor is the bound (t9 99%, never idle). You: fps drops as an AI car comes close to the camera (its detailed model = many draws). Next lever: draw workers, after porting PGR4's logged occlusion queries (3cbd0f4) onto our `regpu-zpd-tile-spans` patch; then per-draw CP cost (constants are the biggest bucket).
- **Session 14: sun flare works with draw workers** (ReGPU patch `regpu-worker-queries`: a counting query's draws go to the command processor's list, workers between tile spans; `REGPU_NO_WORKER_QUERIES=1` = old). Paused grid 1440p 4x: 225 fps (vs 219 workers off, 232 fixed answer). The launcher's "Accurate sun flare" option is removed. Open: flare possibly stronger than console (compare with hl360). Next lever: per-draw CP cost.
- **Session 14, per-draw CP profile** (paused behind AI cars, 3,560 draws, 1440p 4x; logs `out/logs/session14-cp-profile*.log`, `session14-t7-profile.log`): the render thread t7 spends ~74% in D3D's ring-space wait (`sub_824B5048` → `sub_824B4F78` → `sub_824AAA68`), so the **command processor (t9, 89–95% busy) is the bound**. **`REGPU_PERF` itself costs ~14%** (151 → 175 fps with it off: per-section QPC timers); keep the launcher's `perf_log` off except for measuring. Clean t9 profile (no perf log): no single hotspot. ~25% translating draws on t9 (small direct segments + its own pieces), ~20% PM4 parsing + `WriteRegisters` (incl. logging registers for the workers), ~10% waiting (piece join `SleepConditionVariableSRW` ~5.5%, `SleepEx` ~5%), ~4% hashing guest data (`FullHashAvx2`, `ContentHash`), ~3% small per-packet overheads (`GameSettled` QPC per draw via `RendererParallelReady`, `RendererTracing` atomic load per packet, a `getenv` in a `GpuStart` lambda). Segment sizes per frame (`REGPU_TRACE_SEGMENTS`): 491 70 25 1 7 5 1 55 1 10 1 4 18 1 **1712 536** 26 1×13 24 **403** 96 24: ~3,140 of 3,560 draws are in logged segments. `REGPU_DRAW_PIECES` 3/6/8/12 (live): all 174–175 fps, so the parallel part is not the limit; the serial CP work is. (Note: the `perf: segments` line counts only logged draws.) Ideas: cache `GameSettled`/`RendererTracing` (~3%); cheaper register logging/parsing; let the CP go on while pieces translate (no join per segment; structural).
- **Session 14 result: the draw thread is the default.** `REGPU_DRAW_THREAD` alone 181–184 fps on the cars grid (workers 175), Montserrat 240 (cap). Patch `regpu-draw-thread-pieces` also runs the segments on the draw thread with workers (720p 1x 175 → 202), **but the sun flare is gone that way** (seen in a race; the queries' draws run direct on the draw thread via `seg::g_query`, cause not found): opt-in `FM3_DRAW_THREAD_PIECES=1`. 1440p 4x MSAA is now GPU-bound (~90%+). Next: find the flare bug with pieces (trace `REGPU_TRACE_ZPD_RESULTS` at the Montserrat spot with pieces on), then make pieces the default.
- **Session 15: fixed, pieces are the default.** The command processor cleared the draw thread's `seg::g_query` at a query's end, ahead of the draw thread (sun draws went to worker lists). Montserrat spot 1440p 4x MSAA: ~257 fps, flare right. `FM3_DRAW_THREAD_SERIAL=1` = the serial draw thread.

**Test 2 (superseded above; the A/B was invalid):** `REGPU_EARLY_FENCES=1` (env at start + live A/B, 240 cap, 480 Hz vblank, driving near cars): only ~5–8% (e.g. ~3,100 draws 146 vs 138 fps, ~2,720 draws 156 vs 144) on top of no-fence-order, and it **breaks the car liveries back into noise (B2)**: the game compresses the livery on the CPU once it sees a fence, and an early fence lands before ReGPU's Deferred livery write-back (ReGPU only holds fences behind "small" write-backs). Left off. To revisit: hold fences behind our livery/picture write-backs too, or PGR4's truthful fences. With fence order off and early fences off, the 240 cap gives ~135–170 fps near cars, ~200 with fewer draws.

**Symptom:** with the frame-rate unlock at 120 (1440p, 4x MSAA), drawn fps ≈ 1/(2.7 ms + ~3.5 µs × draws): ~3,200 draws (cars near) 71–75 fps, ~1,900 (alone) 105. The main loop stays at 120 with even dt (smooth).
**Measured (logs `out/logs/session13-t7profile.log`, `session13-drawthread.log`):** render thread t7 ~86% in D3D's ring-buffer wait (`sub_824B4F78` → `sub_824AAA68`, read pointer at device +10896); ReGPU's command processor (t9) about half its time in WAIT_REG_MEM (fn 3) on guest `18C27014` (+ `18C27000/04`); with `REGPU_DRAW_THREAD=1` fps unchanged, worker ~42% busy, CP 40% spinning + 32% blocked (drains). Nothing saturated: a turn-taking chain. Blocked time (WaitOnAddress/condition variables) is invisible to the sampler.
**Lead:** our pin already has the switches the faster ports turn on; `src/gpu_host.cpp` sets none of them. At every INTERRUPT packet the CP runs `work::Drain()` and `PendingWrites::Drain()` (waits until every queued fence has passed the **host GPU**, `gpu.cpp:1151-1156`), and by default fences are written only after the host GPU (`renderer.cpp` `FenceSubmitImpl`, fence-writer thread). If the game's write to `18C27014` follows such a fence/interrupt, every turn includes host-GPU latency.

Ranked tests (none needs a rebuild):
1. **`REGPU_NO_FENCE_ORDER=1`** (live: `Fm3 "switch REGPU_NO_FENCE_ORDER 1"`, needs `REGPU_PERF=1` at start for numbers). PGR4 9ff4042: London grid 86 → 147 fps, p99 16 → 8.1 ms ("draining them on the command processor made it and the GPU take turns"). Risk: the game sees an interrupt/scratch value before an earlier fence (PGR4's `REGPU_TRACE_OVERWRITE`: 0). Ordering came from the FM4 port (upstream 6207e10), no measured symptom recorded.
2. **`REGPU_EARLY_FENCES=1`** (env at start; the `LiveSwitch` in `FenceSubmitImpl` is live, `RendererEarlyFences()` is read once). FH1 59 → 80 fps; FH1R ships it on (FX free roam 18 → 34). Risk: PGR4 torn/exploded geometry (title frees memory the GPU still reads) → PGR4-only `REGPU_TRUTHFUL_FENCES` (bc471e8; buffers copied, fences after the GPU, a hook skips only Present's fence wait; FM3 analogue: the present path `sub_824AF280`). Watch vinyl/livery write-backs (`REGPU_NO_WRITEBACK_FENCES`).
3. **`REGPU_DRAW_WORKERS=3`** (only with 2; excludes the draw thread; off with an upscaler). PGR4 56143c4: 116 → ~155 fps start, 111 → 137 driving; the fixes that made it work are in our pin. **Blocker:** with workers every ZPD query answers 1000 → our sun-glare fix breaks; PGR4 3cbd0f4 logs queries into segments, but conflicts with our `regpu-zpd-tile-spans.patch` (same functions).
4. **`REGPU_SUBMIT_DRAWS=512`**: PGR4 FX 35.7 → 43.9; FM2 (GPU-bound) lost 4–17%.
5. Smaller per-draw/frame switches in the pin: `REGPU_TEXTURE_CHECK_STAGGER` (FM4 ships on), `REGPU_UPLOAD_SAMPLED`/`REGPU_DYNAMIC_EXPIRE`/`REGPU_DIRECT_DYNAMIC_INDICES`, `Config::lowPerformance` (constants straight into the ring; constants are FM3's biggest bucket, 1.37 ms/frame), GPU-side `REGPU_LEAN_FETCH` (FH1R on) and `REGPU_OPTIMIZE_SHADERS`. ~1 ms/frame together at most.
6. A/B 720p/1x vs 1440p/4x in the same scene, to rule out the host GPU ("GPU time" in the perf log is a begin-to-end span, not busy time).
7. Find who writes `18C27014` and when (graphics interrupt callback, or after a D3D fence wait): decides between 1/2 and a game hook.

Later/CPU-saving (helps 4-core CPUs more than fps here): FH1R's `gpu_wait.cpp` idea (D3D's GPU wait spins 20 µs, yields to 200 µs, then blocks on the GPU-written word with WaitOnAddress; needs ReGPU to `WakeByAddressAll` after each guest word it writes, which our pin never does; FH1R: render thread 86 → 45% of a core, +10% fps on a 4-core laptop, no fps change on 8-core desktops); render-thread idle hook (races unchanged); ideas from FH1R's own CP for ReGPU's per-draw path (reuse unchanged render-target setup, skip NOP padding, avoid per-draw locked atomics: one `xchg` per draw was 3.6–3.9% of their CP); a PGO build of ReGPU (+6% in FH1R); `-march=x86-64-v3` for the recompiled code (FH1R #220: 6.9% fewer instructions). Thread pinning/affinity: FH1R measured no gain (−1.3%).
Sources: upstream ReGPU (`reference/framework/ReGPU`: `main` = our pin; `engine-optimisation` is an older, separately squashed history, nothing newer for the CP; its log explains the pin's perf code), FH1R's ReGPU fork (`reference/framework/ReGPU-fh1`, `fh1`: correctness only), PGR4's vendored ReGPU (the only newer copy: truthful fences, logged occlusion queries, VRAM budget), FH1R `docs/perf.md`, `docs/framerate.md`, `forza-recomp/regpu-plugin/src/regpu_graphics_system.cpp:782-842` (the switches it sets), `forza-recomp/src/hooks/gpu_wait.cpp`, `render_idle.cpp`. FH1R is GPL-3: port ideas, not code.
- **Audio queue headroom** (Q26): a check beyond issue G.
- **Post-race finish menu at 30.0 fps:** hl360 locks it at 30 (probably the game's own behaviour); check ours.
- **Frame rate over a full lap**, after the shader cache is warm (the first seconds after a cache rebuild run at 30–36
  fps).
- **ReGPU-side frame dump** in hl360's `index.txt`/`tex_*.bin` format, so `tools/dumpdecode.py diff` can compare
  hl360 and our build directly. That would speed up finding parity bugs like D and E.
- Frame-rate unlock (Phase 6) only after FM3's simulation timing is confirmed (Q24: FM3 paces itself on guest vblank).

### L. LOD / render distances (future research, session 13 note)
**Seen:** while testing the frame-rate unlock you noticed animated spectators swapping detail levels (LOD popping) near the track; it is the same with `REGPU_NO_FENCE_ORDER` on or off, so it's the game's own LOD, not a ReGPU bug. At 1440p and above the console's LOD distances (tuned for 720p) are more visible. Idea: push LOD/draw distances out (an option, like FH1R's "World detail beyond the console", #361), now that there is CPU/GPU headroom.
**What FM3 has (strings in `out/fm3.bin`):** `SpriteDistance` (crowd sprites; in FH1 `TrackSettings <Crowds lod0-3 SpriteDistance>`), `AnimLODValue`, `GrassLODDist`/`GrassLODFadeRange`, `MipMapLodBiasModifier`/`MipMapLodBias`/`LODBias`, `StartLOD`/`EndLOD`/`NumLODs`/`LODCount`/`LODType`/`LODError`, `Lod1EventGroup`/`Lod2EventGroup`, car models `LOD0`..`LOD2` (`HasLOD0`, `CCarBodyModelLOD0`, `CCarPresentationDeferred` load/unload LOD0). **Not** present: FH1's `CarLODTransDist`/`LODdist_*`, `CarLODThresholds`, `CrowdLODCountScales`, `CrowdSpriteDistScale` — FM3's car/crowd LOD selection is set up differently (find it from the strings above: which function reads `SpriteDistance`/`AnimLODValue`, and where car LOD0 is loaded/unloaded).
**First steps:** (1) find where the crowd's LOD/sprite distance is read (track settings XML in the game data, e.g. `SpriteDistance` per track) and whether it's a value we can scale by hook or data; (2) the car LOD selection (distance thresholds, the "only the N nearest cars get LOD0" rule FH1 has); (3) texture LOD bias (`MipMapLodBiasModifier`, FH1 +1404 in its render settings). Measure the cost (draws/frame) since frame rate near cars is draw-bound (issue P).
**Reference:** FH1R `docs/graphics-detail.md` (World detail beyond the console, #361: texture LOD bias, model LOD/zone switches, grass, crowd LOD, car detail with `sub_8248CBD0`/`sub_824351D8`/`sub_8247C5C8`/`sub_82477368` in FH1 — FM4 engine; GPL-3, ideas only), `reference/sibling-recomps/ForzaHorizon1-ReImagined/docs/render-tests.md`.

## Leftovers
- `sub_82A918F0` blrl trap and the SEH policy (not hit yet).
- `githubfileignorepls` at the project root: delete it? (It's yours; I haven't touched it.)
