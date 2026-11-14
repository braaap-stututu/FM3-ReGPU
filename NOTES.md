# NOTES.md — FM3 ReGPU recomp running log

## ▶ Current status / next step  (updated 2026-11-13, session 30)

### Session 29b (2026-11-12, cloud, research only, no code): PFP3, ray tracing, vendor speed-ups, Linux/Steam Deck
- User asked how to add **Project Forza Plus 3** (PFP3) as an option. Study: [RESEARCH.md](RESEARCH.md#project-forza-plus-3-as-an-option-session-29-2026-11-12).
  The mod is all data (`Media` overrides, 18 pre-extracted DLC packs, 4 career-mode `gamedb.slt`s); its xex is not needed.
  **Blockers in our build:** Q9 protected-file re-signing is not ported (any changed `gamedb.slt`/`.ini` = disc read error);
  mods can't replace a whole `.zip` or DLC files; PFP3's DLC needs licence mask FFFFFFFF; long paths (no `longPathAware`).
  Plan: re-signing in `loader.cpp` -> mod overlay fixes -> separate DLC root `dlc-pfp3` -> launcher tile installing from the
  player's own zip (career mode dropdown, FOV) -> separate profile. **First local step:** put the PFP3 zip(s) in the project
  root, list the layout, and check the xex MD5 (sources disagree whether the mod's xex is UC-based); ask the user.
- **Next on the main PC (user's choice):** PFP3 datamine, the ReBAR log line + L47 A/B, an RGP capture (L45), Proton boot
  on the main PC's Linux (the user runs it). Merged into `main`.
- **User's follow-up plan (agreed direction):** on the project PC, **datamine PFP3 vs the stock files** (file trees by
  hash, zip members, `gamedb.slt` schema + row diff with Python's sqlite3, ini/xml text diffs, DLC packs vs Disc 2) and
  build a **general mod system** from it (manifest per mod with options, load order, game/update/dlc overlays, Q9
  re-signing, conflict report, launcher Mods tab, DB changes as SQL patches where possible so mods combine). PFP3 = first
  mod. Later, as an option: **FM4 content / career / handling in FM3** (user's idea, FM3 is lighter): study feasibility
  per part first (one FM4 car's file versions and shaders vs FM3's; career = DB rows + UI flows; handling = data vs
  FM4's tyre-model code). Never commit game data: a mod made from the user's discs is a recipe applied at install.
  **User correction:** car and track ports are solved problems in the Forza modding community, and the FM4 sibling
  recomp's lead has backported Forza Horizon 6 cars into FM4. So the mod system should carry whole new cars and tracks
  (new DLC-style packages + DB rows), not only file swaps. Get the community's tools/method and the FM4 lead's car
  pipeline from the user before reverse-engineering anything ourselves. Related: "FM4 Imported" (446 FM4 cars in FH1,
  moddb) shows the 360-era car path; Reddit counts 243 FM4 cars missing in FM3.

### Session 33 (2026-11-14): stutters and frame drops
- **Long-frame log built** (ReGPU patch 42 `regpu-long-frames`, on with the light perf log: launcher "Light performance
  log" = `perf_log_light=1`; `REGPU_LONG_FRAMES=0` off, `=1` alone). Each frame over 2x the recent average (EMA over ~32
  frames, each frame capped at 3x it) and 1 ms over it: `long frame N: ms (avg) | draws | idle` (draw thread waited for
  the command processor = the game was late) `| gpu wait` (host GPU) `| builds/pipelines/created | textures new/changed
  KB ms | swap`. The light log's line gains `| N long`. No per-draw cost (idle timed only around the draw thread's waits).
- **First runs (4K 4x, grid + 20 s `Test-Drive`):** 0 long frames while driving. Long frames only at loading screens:
  idle 100-220 ms (the game loading) with 150-250 new textures (36-39 MB, 20-28 ms of the draw thread), and a few
  90-180 ms frames creating 45-98 committed resources (~1.5-2 ms each: L17/texture-heap lead, loading only).
- **User's drive 1 (1440p 4x, ~2 laps, ReLive recording, light log):** racing at 320-360 fps (draw thread 90-100% busy
  at ~2,700 draws), only small long frames (5-12 ms: 1-5 MB texture streams). **One real hitch: 78 ms at log 132.9 s**
  (video: 4-5 repeated frames, the race clock ran on: 1:32.499 -> 1:32.668), with no idle, GPU wait, builds, textures
  or swap time counted. Video vs log: frame-diff of the ReLive mp4 (`ffmpeg` gray 160x78 raw + a diff script; video
  offset read from the timer). Last 3 long frames (146.5-147 s, GPU waits 20-65 ms) = the recording being stopped.
- **Fields added for it:** `fences` (all fence waits), `replay` (waits for the replay thread / async Present), `cpu`
  (thread CPU time vs average). Grid check: loading frames' unexplained 90-170 ms were fence waits (42-78 ms).
- **User's drive 2 (no recording, 1440p 4x, ~2 laps):** again **one hitch, 82 ms at log 117.1 s** (the user felt it "a bit
  before finishing lap 2"; drive 1's was near the line too), and the new fields say: **cpu 76.9 ms** (average 2.1), idle,
  GPU wait, fences, replay, builds, textures all ~0. So **not ReLive and not a wait: the draw thread itself computed for
  77 ms.** Everything else in the race: 5-6 ms long frames from texture streams.
- **Long-frame stacks built** (`src/long_stacks.cpp`, settings.ini `long_frame_stacks=1` or `FM3_LONG_STACKS=1`/`=N` ms,
  default 15; turns on `REGPU_LONG_FRAMES`): a thread samples ReGPU's draw thread every 1 ms (RIP + 20 callers, unwound
  from a stack copy after resuming) into a 1.5 s ring; ReGPU calls the host at each long frame (new
  `regpu::driver::SetLongFrameCallback`, in patch 42) and the samples inside that frame are logged as `long stacks:` lines
  (top functions, then top call chains). No measurable fps cost on the grid. Grid boot already shows the loading frames:
  driver allocation waits under `CreateBuffer`/`GetIndexBuffer`/`EnsureResolved`, `DecodeTexture`, `ContentHash` +
  vector copy in `QueueDeferredWriteback`, and an 88 ms frame mostly `WaitFenceValue` in `WriteResolveToGuest`.
  `long_frame_stacks=1` was added to the user's settings.ini (backup `out/perf/s33-stacks/settings.ini.bak`).
- **Lesson:** only the launcher copies `fm3.log` to `fm3.prev.log`; `play.cmd`/`Start-Grid` boots overwrite it. **Copy
  the user's drive log away before booting anything** (drive 2's log was lost that way; its long-frame lines are above).
- **User's drive 3 (stacks on, 2 laps):** the hitch again at the start line (**79.7 ms at log 112.1 s, cpu 72.6 ms**),
  and the stacks name it: 55 samples, all in heap frees (`RtlFreeHeap`, `NtFreeVirtualMemory`) under a
  `std::unordered_map<uint64_t, ...>` called from `DrawImpl`. That is `CachedIndexList`'s thread-local cache of expanded
  index lists (one vector each), **`clear()`ed whole at 8192 entries**: the race adds keys as the track streams, so it
  filled about once a lap and freed every list in one draw. (ntdll names in the stacks are wrong export guesses; the
  callers are right.)
- **Fix: ReGPU patch 43 `regpu-index-cache-sweep`** (default): past 8192 entries, lists unused for 600 frames are freed
  gradually (a cursor looks at 512 buckets a frame, frees at most 128); a full clear only past 32768. `index lists:` log
  line every 3000 sweeps; `REGPU_INDEX_CACHE_CLEAR=1` (live) = old behaviour. Grid + 20 s drive: fine (359 fps, no long
  frames while driving; a straight drive doesn't reach 8192). Log of drive 3: `out/perf/s33-stacks/drive3.log`.
- **User's drive 4 (fix on, 2 laps): hitch gone** (user: "no longer red"). Longest race frame 11.6 ms (was 78-82 ms);
  `index lists: 8065 cached, 128 stale freed` at 104.9 s with no long frame. Left: 5.7-6.5 ms frames from texture streams
  (0.2-3.3 MB, 1-3 ms on the draw thread; leads: upload/decode off the draw thread), one 11.6 ms with 7.6 ms idle (the game
  late, at 69.8 s), the race start (19.5 ms). Log `out/perf/s33-stacks/drive4.log`.
- **Texture-stream frames (user's choice next):** the long-frame line now names the frame's slowest texture and the
  creation time. Automated drives + the grid boot's loading frames as the benchmark (same textures every boot): the cost
  is CPU untiling/decoding on the draw thread at 1-2 GB/s (upload ring in VRAM or not: same times), a 128 MB texture heap
  made on the draw thread when the pool ran out, and **cubes read from memory created as committed resources (~1.2 ms
  each)**. **ReGPU patch 44 `regpu-texture-streaming`** (default): texture rows on 3 helper threads for textures of
  256 KB+ (`REGPU_TEXTURE_THREADS`, `REGPU_NO_TEXTURE_THREADS=1`), next heap made ahead on another thread
  (`REGPU_NO_HEAP_AHEAD=1`), cubes in the pool (`REGPU_COMMITTED_CUBES=1`). Same loads, same boot: 18.9 MB DXT5 8.9 ->
  2.5 ms, 28.2 MB 19.2 -> 9.6 ms, 36.5 MB 34.7 -> 19.8 ms, 1.6 MB 1.4 -> 0.5 ms; 30 s drive max frame 7.3 -> 5.7 ms.
  Picture: cross-boot diffs only in the crowd (checked by crop). Logs and shots: `out/perf/s33-tex/`. (A `Save-Shot` PNG
  is written on the draw thread: a ~90 ms frame in test runs, not in play.)
- **User's drive 5 (patch 44, 2 laps, 1440p 4x):** race (27-127 s) had **3 long frames, all 5.6-5.8 ms** (drive 4: 8,
  up to 11.6 ms); texture time in them 0.6-2.2 ms (was up to 3.3 ms), the rest of each the draw thread's normal work. The
  biggest one loaded 42 small textures (4.3 MB, each under the 256 KB threading threshold). No other hitches. Log
  `out/perf/s33-tex/drive5-user.log`. **Stutter work done for now**: what is left is one 5-6 ms frame every ~20-50 s.
- **Next:** the other areas (4K+, iGPU, Steam Deck); user's choice.

### Session 33 (cont.): AI-driven benchmark (user's request)
- **The player's car driven by FM3's own race AI** (`src/hooks/ai_player.cpp`, `FM3_AI_PLAYER=1`): drives the whole race
  (3 laps of the test-grid career race, racing in the pack), HUD/camera/position/results still the player's.
  `engageaionstart` (dev option) does nothing in retail. How: `tools/analysis/rtti.py` (new: MSVC RTTI -> vtables)
  found `RacingAI::CAICar`, `CHumanDriver`, `CAIDriver`, `CMetaAIDriver`, `CAIDrivatar`, `CCrashRecoveryDriver`;
  `sub_828F8858(car, params)` builds each car's driver (car+104: 0 player, 2 opponent; params[0]: 3 player, 2
  opponent). The hook builds the player's car with +104 = 2 and params[0] = 2, then restores both. Dead ends (kept in
  the file's comment): an AI driver installed afterwards (pad still drives), with the four CCarDynamics calls, +104 = 1
  with type 3 (AI revs on the line, never launches). Also found: CCarDynamics +2744 throttle, +2748 brake; the
  control-state switch (CAICar slot 4, car+2288) is not used at the start. Watchpoints can now be set from code
  (`SetGuestWatch` in `crash.cpp`, with `FM3_WATCHW=0` at start).
- **Benchmark** (`tools/benchmark.ps1`: `. .\tools\benchmark.ps1; Invoke-Benchmark -Label x [-Env @{...}]`): copies the
  Baseline profile to a fresh `Benchmark` profile (a finished race would write into the user's save: **never run the AI
  on Baseline to the finish**; my first full test did, and was closed without the exit save, Baseline untouched),
  boots the grid route minimized with the AI and the light perf log, measures 120 s from 10 s after the release
  (`tools/analysis/bench.py`), kills the game (no exit save), results in `out/bench/<label>-<time>/` + `out/bench/runs.csv`.
  First runs at 1440p 4x (user settings, cap 360): **354.1 and 358.4 fps** avg, lowest second 302/333, p99 3.39/3.24
  ms, 6/3 long frames. At 1440p the cap squeezes differences: use 4K for GPU A/Bs. The AI varies run to run: average
  several interleaved runs.
- **Launcher button** (user's choice): Settings tab > **Benchmark** panel (measure 60/120 s, Run, Open results, a table
  of the last 20 results with avg fps, lowest second, p99, worst, long frames, GPU ms and the speed options that were
  on). `launcher/FM3Launcher/Benchmark.cs`: copies Baseline (else the active profile) to `profiles\Benchmark`, starts
  the game with `FM3_PROFILE=Benchmark` + `FM3_BENCHMARK=<s>`, shows progress, reads fm3.log, saves `<user>ench  <time>.json` + `.log` + `runs.csv`, deletes the copy. Also `FM3Launcher --benchmark [s]` (no window, result to the
  console). The game does the race by itself with **`FM3_BENCHMARK=<s>`** (`grid_test.cpp`: the route without the
  freeze, AI + light log on (`gpu_host.cpp`), "benchmark: measuring N s" 10 s after GO (`FM3_BENCHMARK_SETTLE`),
  "benchmark: done" and WM_CLOSE to its window); `bench.py` reads those markers too. Tested: `play.cmd` with
  `FM3_BENCHMARK=30` (357.9 fps, closed by itself after 65 s) and `FM3Launcher --benchmark 30` (358.5 fps, Benchmark
  profile removed afterwards, Baseline untouched). The panel's JS block compiled and rendered a sample in the browser;
  the panel's look in the real launcher is not checked yet (user).
- **Next:** the user tries the button; benchmark at 4K (repeatability over ~5 runs); then the other areas.

### ▶ Start here (session 33): state at the end of session 32 (2026-11-13/14)
- **In short:** session 32 measured where the frame goes and built the last big CPU/GPU leads. **Default changes:**
  PGO build of ReGPU (L23, +3.2% draw-thread bound; `build/rel` configured `FM3_PGO=use`). **New launcher options,
  all off by default (the user drove the half-rate ones: lag not noticeable at their frame rates):**
  | Option | Setting | Gain | Picture |
  |---|---|---|---|
  | Graphics "Anisotropy as the game asks" (I8) | `aniso_from_game=1` | GPU -3.8% at 4K | distant textures slightly softer |
  | Graphics "Lower detail for distant AI cars" (L60) | `car_lod_far=1` | +4.1% draw-thread bound | small details on mid-distance cars |
  | Graphics "Fps counter and frame-time graph" | `fps_panel=1` | -0.8% (noise) | panel at the top centre |
  | Testing "Half-rate car shadows" (L58) | `shadow_rate=2` | +8.2% draw-thread bound | shadows a frame late |
  | Testing "Half-rate car reflections" (V8) | `reflect_rate=2` | +2.1% draw-thread bound, GPU -2.4% at 4K | reflections a little later |
  Closed with measurements: L56 render-target formats (the scene is 7e3 float with alpha), V2 VRS on AI cars (their
  material shading is ~0.05-0.1 ms), hardware MSAA resolve (no big resolve qualifies), I2 lean fetch 2 (slower),
  FM3's dev options `numaicars`/`fastcastshadows`/`fastdropshadows`/`cubemap`/`fastenvmaprender` (no effect in
  retail). Parked: L59 per-pass MSAA (~3% at 4K for a large ReGPU change). ReGPU patches: 41 (new: 41 `regpu-fps-panel`).
- **Where the frame goes now (RX 7900 XTX, frozen CrownJewel grid, 8 cars):**
  - 1440p 4x: at the 360 cap; underneath, ReGPU's draw thread (~2.7 ms a frame, 1.02 us a draw, ~2,670 draws).
    Each car ~183 draws (~120 main scene in ~20 material layers, ~54 shadow map, rest small passes) = ~0.19 ms of
    draw thread + ~0.15 ms GPU; no cars (`FM3_CMDLINE=DontDrawCars=1`): 1,226 draws, 312 fps at 4K.
  - 4K 4x: GPU-bound ~229 fps, GPU ~4.1 ms: main scene draws ~1.5 ms (car paint layer 0.16), three 1280x720 colour
    resolves 0.63 ms (format conversions: shader resolves needed), FM3's clear rectangles 0.26 ms (L54/L37 closed),
    post shaders small; the post passes share base 0 and the scene's 4x depth.
- **What is left (the user's summary): 4K+ resolutions, stutters and frame drops, iGPU and Steam Deck.** Plan per
  area in PERF-PLAN.md "Next up (from session 33)". Suggested first steps:
  1. **Stutters / frame drops:** make them visible first. The fps panel shows them live; add a "long frame" log
     (frames over 2x the average: what ReGPU did in them: shader/pipeline compiles, texture uploads/decodes, resolve
     write-backs, ring growth) and drive a race with it (light log on). Then L32 (wait for missing pipelines at
     submit), L35 (texture checks only over the bytes a texture uses: needless reloads), L17 (texture budget hitches),
     L31 (frame pacing). Known: the first seconds after a shader-cache rebuild run at 30-36 fps (README).
  2. **4K+:** cheap measurements first at 4K with the light log: L3/I6 (`REGPU_OPTIMIZE_SHADERS` / level, start-time),
     L48 wave size, L5 sampler table (ReGPU root-signature change), I3 per-shader plain multiply (-2.5% GPU, needs the
     black-flag fix); L59 only if the user wants the ~3%.
  3. **iGPU:** the 9800X3D has a small RDNA2 iGPU: check whether it is enabled (`REGPU_ADAPTER`, adapter list in the
     log) for real tests; L15 laptop GPU pick, L19 native small texture formats, L17/L18/L21 memory, V5 SMAA, V7 render
     scale + FSR (in the launcher, untested on FM3), speed presets in a race (untested), a "low-end" preset bundling
     the session-32 options.
  4. **Steam Deck (the user has a Deck OLED):** L52 preset (1280x800 or 720p, MSAA 2x/1x, cap 40/45/60, 4C/8T worker
     counts), Proton run by the user; test 720p 1x here meanwhile (CPU-bound numbers). Read the session 29b research
     (Linux / Steam Deck, top of this file and RESEARCH.md) first.
- **How to test** (unchanged from session 31, plus): the fps panel (`fps_panel=1`) for drives; `REGPU_FPS_PANEL_DUMP=1`
  writes it to `out/run/fps_panel.png` (debug-server shots don't contain it). GPU leads at 4K with the light log
  (`Get-LiteGpu`), draw-thread leads at 1440p with the full perf log as the load (`Test-Live`), moving cars with
  `Test-Drive`. New live tools: `FM3_SCENE_SKIP=<hex>` (skip a scene step / car-draw call site in the main view),
  `FM3_CAR_LOD_FAR/FROM`, `FM3_SHADOW_RATE`, `FM3_REFLECT_RATE`; FM3's `DontDrawCars=1` (boot) for a no-cars frame.
  **Lesson (twice in session 32):** skipping a render pass's draws leaves its clear and resolve: the image goes empty.
  Skip the whole pass (begin, draws, end) and ReGPU keeps last frame's image.
- **PGO:** re-record the profile after larger ReGPU changes (`pgo/README.md`; the fps panel patch postdates it).
- **User's settings.ini now:** 3840x2160 (the user's choice at the end), msaa 4, cap 360, perf_log 0, perf_log_light 0, fps_panel 1, vram_upload_ring
  1, one_tile 1, mirror_rate 2, shadow_rate 1, reflect_rate 1, car_lod_far 0, aniso_from_game 0. Game (PGO build)
  and launcher built at HEAD, both closed.
- Session 32's details, in order: the next section.

### Session 32 details (2026-11-13 evening to 11-14 night)
- The user's settings.ini is now **3840x2160, msaa 4, light log on** (set by the user for the GPU tests; leave it).
- **L56 closed without code:** a grid `capture` (`out/logs/s32_grid_capture.log`) shows the main scene pass (1,804 of
  2,649 draws) on `2_10_10_10_FLOAT_AS_16_16_16_16` (7e3 float) writing alpha on every draw, 51 draws reading it; no
  32-bit host format holds 7e3 + alpha without a picture change. Table in PERF-RESULTS "Session 32".
- **4K re-check of start-time GPU leads** (2 boots each, `out/perf/s32k4/`): I2 `LEAN_FETCH=2` GPU +1.6% (slower),
  `LEAN_VFETCH` +0.2%: no; **I8 anisotropy from the fetch GPU -3.8% (fps 227 -> 235), 9.3% of pixels** (softer
  distant textures, as the game asks: the user's call); **I3 plain multiply GPU -2.5%**, but the crowd's flags turn
  black (session 23): would need the per-shader version (PERF-PLAN I3).
- **I8 is now a launcher option (user's request), off by default:** Graphics "Anisotropy as the game asks" =
  settings.ini `aniso_from_game=1`, read by the game (`src/gpu_host.cpp` -> `REGPU_ANISO_FROM_FETCH=1`, so `play.cmd`
  runs follow it too). Checked: a grid boot with it in settings.ini gave GPU 3.978 ms (I8 boots 3.97, base 4.12).
- **AI-car material cost at 4K (price list, PERF-RESULTS "Session 32"):** every car material's pixel shading together
  is ~0.27 ms of a 4.2 ms GPU frame; the paint/livery layer `e41e183a423e6f1b` 0.15-0.18 ms (noise-output check: not
  colour compression), the player's car ~half of it; AI cars ~0.05-0.1 ms. **V2 (VRS on AI cars) not worth building
  (< 1%).** Session 20's ~0.19 ms a car must be per-car work elsewhere (shadow maps, reflections, mirror, vertices,
  draw thread). The largest single GPU items now are full-screen passes (`fd6b...`, `5b7e...`, `9e50...`: ~0.1 ms each).
- Tool notes: `ps <hash> <expr>` names the pixel input `input` (e.g. `input.pos.xy`); black edits of full-screen post
  passes can make the frame slower (`8be0...` +0.42 ms): use them only on scene draws.
- **Where AI cars cost (user's pick):** `FM3_CMDLINE=DontDrawCars=1` (no cars at all) on the grid: draws 2,688 ->
  1,226, GPU 4.13 -> 2.92 ms, draw thread 2.74 -> 1.25 ms a frame (1.02 us a draw either way). **Per car ~183 draws,
  ~0.19 ms of draw thread, ~0.15 ms GPU** (matches session 20's 0.19 ms). Draws per car: ~120 in the main scene
  (~20 material layers), **~54 in the car shadow map** (first pass, 256x256, depth from the sun: 432 draws = 0.44 ms of
  draw thread a frame, 16%), the rest in small 256x256/512x512 passes. GPU: main pass. FM3's `fastcastshadows` /
  `fastdropshadows` change nothing; `numaicars=0` is ignored by career races; `DontDrawCars` is read at load only.
  At 1440p (draw-thread bound) the cars are a draw-count cost.
- Leads from this (not built, ask): **(a) car shadow map at half rate** (like the mirror, `src/hooks/mirror.cpp`: find
  the shadow-map render function; ~0.22 ms a frame of draw thread = ~8% below the cap; risk: shadows a frame late,
  ~0.2 m at 300 km/h at 360 fps); (b) shadow map only for cars near the camera (fewer draws, distant cars' shadows
  are tiny); (c) per-draw cost on the draw thread (1.02 us; no cheap cut left, NOTES session 31).
- **Half-rate car shadows (L58): BUILT, opt-in** (user: "build it", then "keep digging"). Launcher Settings > Testing
  "Half-rate car shadows" = settings.ini `shadow_rate=2` (off by default), live `FM3_SHADOW_RATE=N`;
  `src/hooks/car_shadows.cpp`. How the car shadows work: `sub_827B5088` drives them once a frame in two phases
  (r4 = 1, then 0; object +720 = phase): `sub_827B4D30` begins the pass (target, clear), the drop-shadow method
  `sub_82592168` (virtual slot +20) draws the cars (car renderer from 82592444, its shadow loop from 828379A0),
  `sub_827B4E40` ends it (two D3D resolves via `sub_824C3CF0`, which forms RB_COPY_CONTROL). Phase 1 = the cars' depth
  from the sun (256x256 depth copy), phase 0 = blur/projection into the images the ground-shadow passes
  (`fd6b3d2a...`, `9e50ba14...`, a 512x512 image filled in its lower half) and every car material (t2-t6, a 4x
  256x256 image) read. **First try skipped only the method: the passes were still cleared and resolved, empty, so the
  shadows blinked** (rate 2: every other frame 3.8% of pixels). **Skipping begin + method + end** leaves no clear and
  no resolve: ReGPU keeps last frame's images. Results (`out/perf/s32shadow/`): frozen grid 0 pixels at rates 2, 3,
  1000, no blinking in consecutive frames; **1440p 4x under the full perf log (draw-thread bound): +8.2% at rate 2
  (255 -> 276, 4/4 rounds), +9.3% at rate 3**; 4K: +2.2% (rate 2), +4.7% (1000); draws 2,673 -> ~2,140 when off.
  Without the perf log this PC is at the 360 cap at 1440p either way (grid and 12 s drives 352-360 both). Drive shots
  at rate 2 (60 km/h) show no visible shadow lag. **User drove it: the lag is not particularly noticeable; keep it
  off by default (user's decision), as an option.**
- New live tool kept: **`FM3_SCENE_SKIP=<hex>[,...]`** (`src/hooks/mirror.cpp`) skips scene steps or car-draw call
  sites in the main view too: `8283A8D0` the opaque cars, `8283AB40` their glass/lights, `828379A0` the cars in the
  shadow map, `82592444` the shadow method's car renderer call.
- **Fps counter and frame-time graph (user's request):** ReGPU patch 41 `regpu-fps-panel` (live `REGPU_FPS_PANEL`),
  launcher Graphics "Fps counter and frame-time graph" = settings.ini `fps_panel=1` (off by default; `src/gpu_host.cpp`).
  A 400x110 panel at the top centre: "N fps, avg ms, p99 ms" (last half second, text 4x a second) over the last 400
  frames' times (guide lines 360/240/144/60 fps; yellow > 1.5x average, red > 2x). Copied onto the back buffer like
  the timer, so debug-server shots don't show it: `REGPU_FPS_PANEL_DUMP=1` writes `fps_panel.png` (checked: grid
  "335 fps 2.98 ms p99 4.43 ms", drive "338 fps"). Cost under the full perf log load -0.8% (1/3 rounds, noise).
  The PGO profile predates this patch (its functions just have no profile data).
- **L23 PGO build of ReGPU (user's request): BUILT, our build uses it.** CMake `FM3_PGO=off|gen|use` (only the
  `regpu` target changes); profile `pgo/regpu.profdata` recorded with the instrumented build (`gen`) on the frozen
  grid, the cockpit camera and a 12 s drive (`src/pgo.cpp` writes it on `Fm3 "env FM3_PGO_WRITE 1"`: the game's
  exit may skip the profile runtime's write); `llvm-profdata merge`; how-to `pgo/README.md`. **1440p 4x under the
  full perf log (draw-thread bound), 4 boots each interleaved (exes swapped: `out/pgo/fm3-off.exe`,
  `fm3-use.exe`): 262.5 -> 270.9 fps (+3.2%, 3/4 rounds)**; 4K (GPU-bound, the user's settings then): draw thread
  2.67 -> 2.60 ms a frame, fps the same. build/rel is configured with `FM3_PGO=use`; CMake's default stays off.
  Re-record after larger ReGPU changes (stale functions just lose their profile).
- **L59 per-pass MSAA: studied, parked** (user asked to do it). The post passes share EDRAM base 0 and the scene's
  depth `2D0`; the biggest (pass 25, 117 draws: effects over the scene) depth-tests against the scene's 4x depth.
  Only the bloom chain and the final composite could be 1x: ~0.15 ms of 4.2 at 4K (~3%) for switching one base
  between sample counts within a frame. Asked the user whether to build it anyway.
- **V8 car reflections at half rate (user's request): BUILT, opt-in.** Launcher Settings > Testing "Half-rate car
  reflections" = settings.ini `reflect_rate=2` (off by default), live `FM3_REFLECT_RATE=N`; `src/hooks/car_reflections.cpp`.
  FM3's reflection cube for the cars: six 256x256 faces drawn with 4x MSAA, three a frame (two alternating sets: the
  4x 256x256 passes that exist only with cars), read by every car material. `sub_827FF238` renders a frame's three
  faces (per face: set-up, the scene renderer `sub_8283A220` called from 827FF4A0, resolve); found with a temporary
  trace of the scene renderer's callers (821FC964 = main view, once a frame; 827FDEB4 = mirror; 827FF4A0 = 3 calls a
  frame). Skipping only the scene draws: faces cleared and resolved empty (2.9% of pixels); skipping the whole
  function keeps them (0 pixels at rates 2/4). The face alternation lives inside the function: a resolve trace at
  rate 2 still shows all six faces (each every 4th frame). Dev options `cubemap=0/1`, `fastenvmaprender=1`: no
  effect. **1440p 4x under the full perf log: +2.1% (rate 2, 4/4 rounds), +2.0% (rate 4); 4K: GPU 4.04 -> 3.94 ms
  (rate 2, +2.3% fps), 3.90 ms (rate 4, +3.4%).** **User drove it: not noticeable at their frame rates; keep it as an
  option, off by default (user's decision).**
- **Post-processing at 4K (user: "do both"):** per-shader GPU times (4K, `out/perf/s32post/shaders4k.log`) mapped to
  passes (capture `out/logs/s32_post_capture.log`): main scene draws ~1.5 ms; the three 1280x720 colour resolves
  0.63 ms (`17F96000` 0.36: the scene, to 10:10:10:2 with exponent bias 0.25, clamp, 2-bit alpha; `17866000`,
  `17BFE000` 0.135 each: 16F samples to 8888); FM3's clear rectangles (`8be0d463...` + vs `b368b2cc`, 2 per pass)
  0.26 ms (L54, closed); the game's post shaders are small (`b0c87a00` 0.14, the rest < 0.07). The big full-screen
  shaders of the price list (`fd6b...`, `9e50...`) are the ground shadows inside the main pass. **Hardware resolve
  tried** (`ResolveSubresourceRegion` AVERAGE behind a switch, through a new RecordedList op): byte-identical but it
  applies to none of the big resolves (all convert formats), GPU unchanged: removed. **New lead (L59, not built):**
  with one guest tile the game draws every pass without MSAA, yet ReGPU draws all of them on its one 4x surface: the
  full-screen post passes pay 4x the sample writes and the resolves; the post segment is ~1.6 ms of 4.2 at 4K. Needs
  per-pass sample counts in ReGPU (surfaces alias EDRAM at one count): large.
- **Car detail levels in the main view (live experiment `FM3_CAR_LOD_FAR=N`, `FM3_CAR_LOD_FROM=M`,
  `src/hooks/mirror.cpp`):** the game ranks the cars by distance and sets each one's detail (+596): 0 for the nearest
  1-2, 1 to rank 3-4, 2 beyond; levels go to 4. Cars at level >= M are drawn at N. 1440p 4x, full perf log as the
  draw-thread load, interleaved 3 rounds: from 2 to 3: +1.9%, to 4: +1.2% (0.008% of pixels: only the furthest
  cars); **from 1 to 3: +4.1% (-89 draws, 0.15% of pixels), to 4: +5.1% (-119 draws, 0.25%: visible on the
  mid-distance cars: mirrors, badges, tail-light detail gone).** **Now a launcher option (user's request: barely
  visible at 720p, good for iGPU users):** Graphics "Lower detail for distant AI cars" = settings.ini `car_lod_far=1`
  (level 3 from level 1), off by default, read by the game (`src/hooks/mirror.cpp`; `FM3_CAR_LOD_FAR` overrides,
  0 = off). Checked: a grid boot with it logs `cars: distant cars drawn at detail level 3`, draws 2,673 -> 2,577.

### ▶ Start here (session 32): state at the end of session 31 (2026-11-13)
- **In short:** session 31 made the **upload ring in VRAM the default** (L47: frozen grid 1440p 4x +26%, 12 s
  drives +18% to the 360 cap, 4K +22%, 0 pixels; the user's races with the perf log off clearly faster), built the
  **light performance log** (ReGPU patch 39, near-free; the full log costs -18..-25% and is kept as a deliberate
  draw-thread load) and **PassScales per draw** (patch 40, +3.8% under load). Tested and dropped: constant reuse,
  index hash once, stable uploads, index buffers by write watch, **L55** barrier batching, **L54** fast clears; I8 left
  off (GPU -3..-5%, slight filtering change). 40 ReGPU patches. Write-up of the VRAM ring for other ports (asked by
  the FH1R lead): [docs/VRAM-UPLOAD-RING.md](docs/VRAM-UPLOAD-RING.md).
- **Where the limits are now (RX 7900 XTX):** 1440p 4x: the grid and drives sit at the user's 360 cap; ReGPU's draw
  thread ~94% busy is the limit underneath (GPU 2.5 of 2.8 ms). 4K 4x: grid 228.7 fps, GPU 4.10 of 4.38 ms =
  GPU-bound (draw thread ~60%).
- **How to test (agreed this session):**
  - **GPU-side leads at 4K 4x:** back up `out/user/settings.ini`, set `resolution=3840x2160`, boot the frozen grid with
    `REGPU_PERF_LITE=1`, read `perf lite:` GPU ms/frame (6 alternating live rounds, or 2 boots per side for start-time
    switches), restore settings.ini.
    `Get-LiteGpu` in `tools/perf_suite.ps1` (average GPU ms of the last N `perf lite:` lines).
  - **Draw-thread leads at 1440p 4x with the full perf log (`REGPU_PERF=1`) as the load:** the grid then runs ~267
    fps, draw-thread bound, below the cap: `Test-Live` A/B.
  - **Moving cars:** `Test-Drive <id> <variant> 12` after the grid numbers (one per boot).
  - **User drives:** launcher "Light performance log" on (settings.ini `perf_log_light=1`; it is 0 now), compare with
    `python -I tools/analysis/drives.py <logs>` (reads `perf lite:` lines).
  - Every lead stays behind a switch; check the picture with `Get-PixelDiff` (live) or `pngdiff_mask.py` (boots).
- **Next (ask the user which):**
  1. **L56 render-target formats** (the one real GPU lead left): every EDRAM surface is RGBA16F, 8 bytes a sample at
     4x MSAA. First measure, no code: which guest formats FM3's passes use (`Fm3 "capture"` on the grid, the
     `RB_COLOR_INFO` format per pass; `tools/analysis/passes.py`) and how much of the 4K GPU frame the main pass
     (base 500000, ~68% of GPU time) spends on bandwidth (RGP at 4K, method in PERF-RESULTS "Session 29"). Only if the
     main scene is 8888 / 2_10_10_10 (not 2_10_10_10_FLOAT or 16F) is a 32-bit host format worth the large change.
  2. **Re-check at 4K** what only ran at 1440p this session: I8 (GPU -3..-5% there) and the leads from session 23 that
     were GPU-side (I2 lean fetch 2, I3 plain multiply), now with the light log's GPU ms.
  3. **AI-car material cost** (session 20's open question: ~0.19 ms a car of pixel shading): per-shader price list at 4K
     (`ps <hash> float4(0,0,0,1)` on the grid, GPU ms per edit), then decide on V2 variable-rate shading.
  4. Draw thread (matters below the cap and on slower CPUs): DrawImpl itself 29%, upload hashing 3.5%; no cheap cut
     left that measured, so only with a new idea.
- **User's settings.ini now:** 2560x1440, msaa 4, cap 360, perf_log 0, perf_log_light 0, vram_upload_ring 1,
  one_tile 1, mirror_rate 2. Game and launcher built at HEAD.
- Details of session 31, in order, below.
- **Main result: the upload ring in VRAM (L47, launcher toggle "Upload ring in VRAM" = `vram_upload_ring=1`) now WINS
  big, 0 pixels differ.** Session 30's byte cuts (2,996 -> ~1,060 constant bytes a draw, written as one run) made
  writing them through resizable BAR cheap. Frozen grid 1440p 4x (user's settings: one tile, half-rate mirror),
  perf log off:
  - live A/B, 4 rounds: **281.6 -> 355.7 fps (+26.3%)**; cockpit view 283.4 -> 354.1 (+24.9%); 4/4 rounds each, 0 pixels.
  - separate boots (two each): grid 281.2/282.5 vs 355.2/354.3.
  - **12 s drives** (new: the race released, right trigger held, automatic gearbox; the first ~6 s are the countdown):
    ring off 300.9/298.4/296.0 fps, p99 frame 4.55-4.67 ms; ring on 356.6/352.9/352.3, p99 3.44-3.62 ms (the last
    half sits at the 360 cap). Cockpit drive 296.9 vs 352.6.
  - 720p 1x (CPU-bound; off side at the cap): 354.1 vs 351.1 (-0.8%; was -5.1% in session 29).
  - Draw thread a frame: ring off 74% busy at 282 fps = 2.62 ms; ring on 96.5% at 355 = 2.72 ms: the BAR penalty is
    ~4% now (the session-29 race loss came from it being ~10%+ with 3x the bytes).
- **Why the grid was stuck at ~282 with the ring off:** not the CPU. `tools/perf/threads.ps1` showed "command list
  replay" at 98% of a core, but its profile (`FM3_SAMPLE_THREAD=name:command list replay`, callers 4) is ~85% AMD's
  driver sleeping/polling inside `ExecuteCommandLists` (`SubmitLists`): it waits for GPU queue space. GPU time
  (`REGPU_GPU_PASSES=1`) 3.42 ms of a 3.54 ms frame = GPU-bound, i.e. the GPU reading ~26 MB of constants a frame
  over PCIe (what L47 fixes).
- **Next: the user drives Crown Jewel with "Upload ring in VRAM" on** (launcher, perf log on), compare with
  `out/run/fm3.drive30-packed.log` via `python -I tools/analysis/drives.py`. If it wins there too: make it the default
  where resizable BAR is on (and update the launcher tooltip, which still says "only when the graphics card is
  clearly the limit"). With it on, the draw thread is the limit again (~96%) but near the 360 cap on this PC.
- **User's drives with the ring in VRAM (Crown Jewel, 2 laps each):** with the perf log on vs yesterday's ring-off
  drive (`out/run/fm3.drive31-vramring.log` vs `fm3.drive30-packed.log`, `drives.py`): same fps at equal draw counts
  (+1-2% at 2,000-2,200 draws, -4..-6% at 2,600-2,800), draw thread 97% busy; the user's overlay showed GPU 99% -> 80%.
  The perf log loads the draw thread itself, so the race was draw-thread bound. **With the perf log off** (ring on,
  one tile on; `fm3.drive31-vramring-noperf.log`, no fps lines in it): user saw **300-360 fps, 99th percentile ~270 at
  worst** (vs ~285 average with the perf log). Still open: a ring-off drive with the perf log off, fps recorded through
  the debug server (`status` once a second, no cost), to tell the ring's share from the perf log's.
  **Done by the user: perf log off, ring off: fps "definitely worse" than with the ring on.** So in real play
  (no perf log) the ring in VRAM wins, as the 12 s drives showed (+18%). Next: make it the default where resizable
  BAR is on (`src/gpu_host.cpp` reads `vram_upload_ring`; launcher `Install.cs` default + tooltip): user's go-ahead.
- **Done (user's decision: keep the toggle, on by default for now):** `src/gpu_host.cpp` turns the ring on unless
  settings.ini says `vram_upload_ring=0`; launcher default on, tooltip rewritten (session 31 numbers, perf-log caveat).
  Checked on the grid: key absent -> "on", 348.7 fps; `vram_upload_ring=0` -> "off", 280.2 fps. **Perf tests now run
  with the ring on** (the old baseline was ring off): set `REGPU_GPU_UPLOAD_RING=0` for ring-off comparisons.
- **Light performance log (user's request; ReGPU patch 39 `regpu-perf-lite`, launcher Testing "Light performance
  log" = `perf_log_light=1`, read by the game so play.cmd too):** one line a second
  (`perf lite: fps, ms/frame (p99, max) | draws/frame | GPU ms/frame | draw thread N% busy`), no per-draw timing:
  the draw thread pushes one record a frame into a lock-free single-producer/single-consumer ring and a "perf lite
  log" thread does the sums and the writing (the ReGPU developer's suggestion). Grid: none 346.9, light 352.9 (noise),
  full `REGPU_PERF` 259.5 (-25%); drives 353.9 / 353.8 / 289.0. The full log stays as the deliberate load (launcher
  tooltip says so). `drives.py` reads both kinds. **Use the light log for race drives from now on.**
- **Leads tested after that (ring in VRAM, grid; "under load" = full perf log on, which makes the draw thread the
  limit at ~260 fps):** draw-thread profile (no log): DrawImpl 29%, PassScales 7.5%, hashing ~8% (vertex uploads
  3.5%, index buffers ~3%), GetIndexBuffer 3.2%.
  - **Built and default: ReGPU patch 40 `regpu-pass-per-draw`:** later PassScales asks in a draw compare only the
    epoch; ToHost asks once. **+3.8% under load (257.8 -> 267.6), +4.6% without (342.6 -> 358.3, at the cap)**, 5/5,
    0 pixels; drive 356.6 (cap). A/B `REGPU_PASS_SNAPSHOT_PER_CALL=1` (live).
  - Existing switches, live under load: `REGPU_CONSTANT_REUSE` -2.4%, `REGPU_INDEX_HASH_ONCE` -0.8%,
    `REGPU_UPLOAD_STABLE_REUSE` +0.7%: none adopted.
  - **Index buffers via the vertex arena's write watch** (skip the hash while no page was written): built, +0.6% under
    load (2/5), +0.2% without; a 20 s verified drive found no stale buffer. Not worth it: removed (not in a patch).
  - **I8 `REGPU_ANISO_FROM_FETCH=1`** (start-time; measured as GPU ms/frame from the light log, 2 boots each): GPU
    2.51 -> 2.39-2.44 ms (-3..-5%), fps at the cap either way; 12.9% of pixels differ (mean 3.9/255; same-boot noise
    0.5%), not visible side by side at normal size (distant texture filtering). Left off: this GPU is not the limit.
- **L55 end-of-frame barriers (user's pick): built, no gain, removed.** GPU frame (`REGPU_GPU_PASSES=1`, ring in
  VRAM) 2.53 ms: main scene pass 1.71 ms, all resolve/post passes ~0.8 ms together. Built: each resolve's barriers
  (colour surface + destination) issued in one call at the start and one at the end instead of two each (live
  `REGPU_SEPARATE_RESOLVE_BARRIERS`). GPU ms/frame from the light log, 6 alternating rounds: 2.531 -> 2.523 (-0.3%,
  noise), 0 pixels. AMD's driver already merges back-to-back barriers; the waits in the post chain are real data
  dependencies (each quad reads the previous resolve), so they can't be skipped. L55 closed.
- **GPU tests now run at 4K (user's decision):** at 1440p 4x the grid sits at the 360 cap and the draw thread is the
  limit, so GPU-side leads are measured at **3840x2160, 4x MSAA** (settings.ini `resolution=3840x2160`, backed up and
  restored around the run; the light log for GPU ms). **4K baseline (ring in VRAM, one tile, half-rate mirror, patch
  40):** frozen grid **228.7 fps**, GPU 4.10 of 4.38 ms a frame, draw thread 58-62% busy (replay thread ~97% = the
  driver waiting for the GPU): **GPU-bound**. 12 s drive 238.4 fps (p99 5.58 ms). Ring in VRAM at 4K: 186.8 -> 228.1
  (+22.1%, 3/3, 0 pixels). Journal `out/perf/s31k4/`.
- **L54 fast clears (user's pick): tested, no gain, removed.** The two big clears a frame are 2560x1440 rectangles on
  2560x2048 surfaces: colour `00085A16` then `0000FFFF` on base 500000, depth 0 (reversed depth) on 2D0. Experiment
  (live): a clear covering the whole pass clears the whole resource (no rectangle) so the driver may fast-clear it.
  GPU ms/frame (light log, 6 alternating rounds): colour + depth whole 2.525 -> 2.548 (+0.9%: the colours aren't the
  surface's optimized clear value, so it is still a full clear, now of 2,048 rows); depth only, with the depth
  surfaces created with clear value 0 to match, 2.513 -> 2.524 (+0.4%). 0 pixels both. The rectangle clears are
  already cheap here; session 29's 29-44 us per clear in RGP was pipeline time, not clear work. L54 closed.
- (Session 29's plan for L54 below is superseded:) **L54 fast clears** (per frame two full-screen colour clears 2560x1440 on the 2560x2048
  EDRAM surface + ~38 small ones; a fast clear needs the whole resource, whose lower rows may hold aliased EDRAM:
  risky, ceiling ~2-3%). GPU-side leads (I8, L55) matter only with the ring off now.
- New in `tools/perf_suite.ps1`: `Test-Drive <id> <variant> [seconds]` (unfreeze, hold RT, 1 s fps samples +
  `frametimes`); results `out/perf/s31/`.

### ▶ Start here (session 31): state at the end of session 30 (2026-11-12/13)
- **In short:** session 30 cut what ReGPU's draw thread writes per draw and found the race's limit moved.
  **User's race (Crown Jewel, 1440p 4x, ring in system memory): ~256 fps (session 29) -> ~285 fps (+11%).**
  Built and on by default (ReGPU patches 35-38, each with an A/B switch, all byte-identical or picture-unchanged):
  35 sparse fetch table (+5% in the race), 36 PassScales epoch (race 0), 37 unread block parts skipped (race 0..+4%),
  38 packed float constants (grid +3.4%, race 0). Tried and removed: whole-line writes, ring in cached system memory
  (race -3..-6%). Not built: GPU copy of constants into VRAM. VRAM ring still ~-11.5% in races: leave it off.
  New traces: `REGPU_TRACE_CONSTANT_BYTES`, `REGPU_TRACE_RING_KINDS`. New tool: `tools/analysis/drives.py`
  (race drives compared by draw-count bucket).
- **Next (user's plan for session 31): find what limits the race now.** The draw thread's busy share in races fell
  96% -> 92% over session 30 while fps held at ~280-285, so something else sets the pace. Method: while the user
  drives Crown Jewel (two laps, **started from the launcher**: the perf log is on only then), sample per-thread CPU with
  `tools/perf/threads.ps1` (no cost: CP t9, the game's render thread t7, "gpu draw worker", replay) and get GPU frame
  time (perf log `gpu` lines, or `REGPU_GPU_PASSES=1`, or RGP as in session 29); compare with
  `out/run/fm3.drive30-packed.log` (`python -I tools/analysis/drives.py <logs>`). Then cut where the time is.
- **How the race drives were compared:** fps and `renderer draw` ms / draws per perf line, bucketed by 100 draws a
  frame (drives differ in camera and traffic). Logs kept in `out/run/fm3.drive30-*.log` (ringoff, ringon, cached,
  passepoch, unread, packed). The frozen grid with the perf log on is draw-thread bound (good for draw-thread cuts);
  without it, it has slack and shows nothing.
- Details of each step, in order, below.
- **L57 done on the grid (details PERF-RESULTS "Session 30"):** measured first with the new live trace
  `REGPU_TRACE_CONSTANT_BYTES`: 2,996 constant bytes a draw, of which 1,796 "vs rest" = the vertex fetch table written
  from entry 0 up to the highest used one (FM3's fetch constants sit near 95). **ReGPU patch 35
  `regpu-sparse-fetch-table` (default): fetch and texture entries written one by one, ~1,470 bytes a draw (7.8 ->
  ~3.8 MB a frame), byte-identical.** Frozen grid fps neutral (system ring: write-combining hides the bytes); the VRAM
  ring at 720p 1x still -5.3% vs the system ring (one boot showed +0.1%: noise). **Block reuse** (`REGPU_CONSTANT_REUSE`, live) built
  as planned but **off**: vs repeats 28.5%, ps 42.2%, 1,144 bytes a draw, but fps -1.4..+0.6% (compares cost what
  they save). A/B of the new default: `REGPU_FETCH_TABLE_BY_RANGE=1` (live) = old.
- **User's race drives done (PERF-RESULTS "Session 30"):** ring in system memory **~+5% at equal draw counts**
  (whole drive 256 -> 280 fps; draw thread ~1.40 -> ~1.33 us a draw). Ring in VRAM barely moved and still loses
  ~-11.5% to it: its CPU cost is not mainly the constants. Toggle stays off by default, tooltip unchanged.
- **VRAM ring cost found (user's pick; PERF-RESULTS "What the VRAM ring costs"):** live trace `REGPU_TRACE_RING_KINDS`:
  constants are ~70% of the ~5.4 MB a frame the CPU writes (uploads ~1.3 MB, index lists ~0.5 MB), so a split ring
  would not help. Whole 64-byte line writes tested and removed (-5.5..-5.9% in VRAM): the cost grows with bytes. VRAM
  ring at 720p 1x still -5.3% (my earlier +0.1% was one noisy boot). The only route left to the VRAM ring's GPU gain
  without BAR writes: packed constant allocation (blocks are allocated whole, 30 MB a frame for 3.7 MB written), then
  a GPU copy of each segment's constants into VRAM. Not built.
- **Packed constants + GPU copy into VRAM: not built** (user agreed after the cost/gain: gain only when GPU-bound).
  (The packed float numbering alone was built later: patch 38 below.)
- **Draw-thread profile (PERF-RESULTS "Draw-thread profile (session 30)"):** a 16-byte store at `DrawImpl`'s end
  is 6-7% = the write-combined ring writes draining (13% with the VRAM ring). Tried the ring in cached system memory:
  stall gone, but the user's race -3..-6%: removed. My ring-kind counters cost 2.2% (now only while traced).
  Remaining targets: `PassScales` ~7% (its memo check runs ~25 times a draw), content hashing ~6% (upload safety
  net, I5's risk), GetIndexBuffer ~2.5%.
- **PassScales cut (user's pick; ReGPU patch 36 `regpu-pass-epoch`, default):** a per-draw cache was not safe
  (`WindowScissor` sets the tall-pass globals inside the draw), so every writer of the non-register inputs bumps
  `g_passEpoch` and the memo compares two registers + the epoch: `PassScales` 11.1% -> ~5.5% of the draw thread
  (live, same boot), 0 pixels; grid fps unchanged (the grid's draw thread has slack). A/B
  `REGPU_PASS_SNAPSHOT_FULL_CHECK=1`. **User's race drive: no change** (`out/run/fm3.drive30-passepoch.log` vs
  `fm3.drive30-ringoff.log` by draw-count bucket): the race is limited by the draw thread's ring writes (the byte
  cut gave +5%, this compute cut 0). Kept.
- **Unread block parts cut (user's pick; ReGPU patch 37 `regpu-unread-block-parts`, default):** bools, loops,
  fetch layout (`vfl`, unread with baked fetches) and `vuser` are written only for shaders whose HLSL reads them
  (scan after the game's rewrite). Grid: 1,437 -> 1,062 constant bytes a draw, 0 pixels, fps within noise.
  **User's race drive:** +0..+4% at 2,300+ draws vs the patch-36 drive (edge of noise; `out/run/fm3.drive30-unread.log`).
  Left after it: the float constants (~800 of
  ~1,060 bytes, written as scattered 16-byte stores), index lists, vertex uploads.
- **Packed float constants (user's pick; ReGPU patch 38 `regpu-packed-constants`, default):** a throwaway test (same
  bytes written packed) showed the scatter costs ~2-5% of the draw thread, so shaders' literal `vc[N]`/`pc[N]` are
  renumbered by rank in their constant mask after translation and a draw writes them as one run. Frozen grid with the
  perf log (draw-thread bound), two boots each: **1.18 vs 1.22 us a draw, 278.6 vs 269.5 fps (+3.4%)**; picture
  unchanged (packed vs old = old vs old: crowd only), 0 compile failures. First boot after it recompiles every shader
  (~3 min to the grid). Old numbering: `REGPU_NO_PACKED_CONSTANTS=1` at start. **Live `ps`/`vs` edits and `hlsl` dumps
  now use the packed numbers** (the capture log's `pc` lines and `REGPU_TRACE_VC` still show the game's).
  **User's race drive: no change** (`out/run/fm3.drive30-packed.log`). The draw thread's busy share in races fell
  96% -> 92% over today's cuts while fps stayed ~280-285: the race is no longer limited by the draw thread alone.
- **Next (ask the user):** find the race's new limit: per-thread CPU during a drive (`tools/perf/threads.ps1`,
  no cost: CP t9, game render thread t7, draw thread) and GPU frame time (`REGPU_GPU_PASSES=1` or RGP) while the
  user drives; then cut there. More draw-thread byte cuts will not show in races now.
- Launcher rebuilt (session 29's tooltip). settings.ini restored after the 720p runs (vram_upload_ring=0 there now).

### ▶ Start here (session 30): state at the end of session 29 (2026-11-12)
- **Next (user's request): L57, cut the bytes the draw thread writes per draw.** Why: racing on the user's PC is
  bound by ReGPU's draw thread (~1.41 us a draw, ~2,500-2,750 draws, 3.5-3.8 ms of a ~4 ms frame; user's drive logs
  `out/run/fm3.drive-ringoff.log` / `fm3.drive-ringon.log`, perf log on). Every byte it writes into the upload ring
  costs it time, and more so with the ring in VRAM (L47: -13% in the race, +12-19% on the frozen grid).
  - **What is written per draw today** (`thirdparty/ReGPU/src/gpu/renderer.cpp`, our patched tree): `VSConstants` /
    `PSConstants` structs (~11171); `RingAlloc` of both blocks (~11197); the float constants copied straight to the ring,
    **only those the shader reads** (`constantMask`, ~11218-11260; A/B `REGPU_CONSTANTS_BY_COUNT`, `REGPU_ALL_CONSTANTS`);
    the rest built in a cached stage and copied in pieces, **only the used fetch/layout/texture entries**
    (~12075-12116, A/B `REGPU_FULL_CONSTANT_BLOCKS`): VS b/l (160 B), used `vf`, info..vte, used `vfl`, vuser..vjitter
    (112 B); PS b/l/info/info2, `tinfo`/`tbias` up to the highest used slot. Then root CBVs at the two ring offsets.
    (My first guess, "non-float parts written whole", was wrong: that was fixed long ago for PGR4.)
  - **The lever: reuse instead of rewrite.** When a draw's VS (or PS) block would be byte-identical to the previous
    draw's (same shader, no constant/fetch/bool/loop/texture register written since), bind the previous draw's ring
    offset and write nothing. Plan:
    1. **Measure first** on the frozen grid (720p 1x = CPU-bound, settings.ini backed up and restored) and if possible in
       a race: `Fm3 "switch REGPU_TRACE_CONSTANTS_USED 1"` (live: average constants per draw), plus a temporary counter
       of bytes written to the ring per frame (constants vs uploads) and of how many draws' blocks equal the previous
       draw's (compare against the previous draw's written bytes, per block). This says how much reuse can save.
    2. **Build** behind a live switch (e.g. `REGPU_CONSTANT_REUSE`): dirty tracking on the register writes that feed
       each block (ALU constants, fetch constants, bool/loop, texture state) plus shader identity; on no change, reuse the
       last offset (valid within one frame's ring and one command list: reset at list/piece/frame boundaries and with
       draw workers). Cheaper than hashing the bytes. Keep the hooked (`onDraw`) and motion-vector paths writing.
    3. **A/B:** `Test-Live` on the grid at 720p 1x and 1440p 4x, both ring locations (`REGPU_GPU_UPLOAD_RING` 0/1),
       0-pixel check; then ask the user for a race drive (same start, perf log on, one boot each) as in session 29.
  - New ReGPU patch for it (scratch-index method in CLAUDE.md), row in `patches/README.md`, PERF-PLAN L57.
- **Session 29 in short (details below and in PERF-RESULTS "Session 29"):** L45 RGP capture (barriers 0.72%, no
  decompress, L46 parked; leads L54 fast clears, L55 end-of-frame barrier chain, L56 render-target format); **L47 built**
  (ReGPU patch 34, launcher toggle "Upload ring in VRAM" = `vram_upload_ring=1`, off by default): +12-19% frozen grid,
  **-13% in the user's race** (draw-thread bound). The user should leave it off for driving. Launcher not rebuilt since
  the tooltip change (it was open): `dotnet build launcher\FM3Launcher -c Release` first.
- **Paused by the user:** photo mode with one guest tile (OPEN-ISSUES D, `out/wip/photo-mode-session28.diff`).
  User's settings.ini now: 2560x1440, msaa 4, cap 360, perf_log 1, one_tile 1, mirror_rate 2, vram_upload_ring 1
  (the user may turn it off after the drive test).
- RGP tools: `G:/claude code projects/tools/RadeonDeveloperToolSuite-2026-10-02-1872` (headless capture method in
  PERF-RESULTS "Session 29": useful to see whether L57 shortens the draw thread's gaps on the GPU timeline).

### Session 29 (main PC, 2026-11-12): L45 RGP capture done
- Radeon Developer Tool Suite 2026-10-02 downloaded (user OK) to `G:/claude code projects/tools/RadeonDeveloperToolSuite-2026-10-02-1872`.
  Headless capture: `RadeonDeveloperPanelCLI.exe -m profiling -p fm3 -a frame:12000 -o <file>.rgp` started before
  `Start-Grid`; the GUI (`RadeonGPUProfiler.exe <file>`) is the only reader, so the user sends screenshots of the panes.
- **Results (PERF-RESULTS.md "Session 29"):** frame 3.70 ms, GPU idle 8%; **barriers 0.72%, no decompress / fast clear
  eliminate anywhere, compression on: L46 parked.** Found instead: rectangle clears run as AMD slow clears (29-44 µs
  each), end-of-frame quads serialised behind FULL-drain barrier pairs, every surface RGBA16F 4x. New PERF-PLAN rows
  **L54 fast clears, L55 end-of-frame barrier chain, L56 render-target format**.
- **L47 built (user chose it next):** ReGPU patch 34 `regpu-gpu-upload-ring`: OPTIONS16 log line (here: GPU upload heaps
  supported, ReBAR on) and a VRAM twin of the upload ring per frame slot, live `REGPU_GPU_UPLOAD_RING`. Frozen grid,
  byte-identical: **1440p 4x +12.9% / +11.9%** (6/6, 8/8), **720p 1x -5.1%** (CPU-bound). The premise in RESEARCH was
  off: S18's -21% was CPU writes; the GPU always read constants from the ring. **User's decision: a launcher toggle**
  (Settings > Testing "Upload ring in VRAM" = settings.ini `vram_upload_ring=1`, read by the game in `gpu_host.cpp`,
  so `play.cmd` too; off by default; log `gpu: upload ring in VRAM on`). Verified: the key puts the ring in VRAM from
  the first frame. The one-tile tooltip now says photo mode breaks with it.
  **User's drive test (same start, perf log on): ring in VRAM ~223 fps vs ~252 off = -13% while racing** (draw thread
  ~1.52 vs ~1.41 us a draw; racing is draw-thread bound here), while the frozen grid gains +19%. Stays off by default;
  tooltip updated. A split ring would not help (the constants are both the GPU win and the CPU cost); lead L57: reuse
  unchanged constant blocks instead of rewriting them (see "Start here (session 30)").

### ▶ Start here (session 29): state after session 28 (2026-11-12)
- **Shader and pipeline discovery, steps 1 and 3 done (step 2, the automated tour, is next):**
  - **Disc 2 in the boot scan** (`src/static_shaders.cpp`): installed packages (`<user>/dlc/<pkg>` with a `.license`)
    are searched like the game folder (their 4 track zips hold 241 `.fxobj`; car packs none); the stamp covers them,
    so an install rescans. Scan threads run below normal priority (as FM2). 784 -> **823 shaders** (+39), 24 newly
    compiled on that boot, 0 failed to compile.
  - **Coverage line** (ReGPU patch 33 `regpu-seed-coverage`): `seed coverage: N pipelines drawn, M not in the seed
    (P%), K built while drawing` with the 600-frame renderer line, plus the first 64 missing keys in manifest format.
  - **ReGPU bug found and fixed** (patch 32 `regpu-motion-flag`, upstream has it too: worth reporting): pipeline flag 4
    meant both "no depth clipping" and "motion-vector variant", so every no-depth-clip draw (FM3: rect clears + ~40
    others on the grid) was built with the motion root signature and two render targets, and never reached the
    manifest/seed (built while drawing on every boot). Motion variants now use 0x800. Frozen grid: picture the same
    (masked cross-boot diff 48 px, all animated spectators), fps in boot noise (274.8 vs 270.8/275.3).
  - **Seed regenerated:** 2,100 -> **2,556 pipelines + 89 vertex variants** (adds L30 plain-fetch variants and the
    no-depth-clip pipelines). Frozen test grid now: **769 drawn, 0 not in the seed, 0 built while drawing.**
  - `tools/pipeline_seed.py` now **merges the existing header** (`--fresh` = don't): this PC's manifest had lost its
    "V" lines, and a plain regeneration from it dropped all 86 vertex variants.
- **Is 823 low? (user asked, vs FM2's ~4,800 scanned / 7,847 seeded):** the scan is checked against what the game
  actually used on this PC (`shadercache/*.ucode`): pixel 343 of 345 found; vertex 123 of 139 match a scanned shader
  apart from the fetch words the title rewrites, 16 don't (at a loose 85%-equal-dwords test). FM3 shares `.fxobj`
  effect files; FM2 keeps shader copies inside every mesh file. The real gap to FM2 is the pipeline seed (2,556 vs
  7,847 from 212 manifests): the tour's job.
- **The 18 misses traced (user request):** a structural compare (same length and control flow, identical ALU slots,
  fetch slots by opcode only) matches 14 of the 16 vertex shaders exactly and one more in all but one slot: they
  are the title's per-layout copies. **3 real misses:** `ps_201e80f858c06c0e` (459 dw, no fetches; never in a
  capture), `ps_a0d8cbb7f2f8ce9a` (24 dw; rect draws with a 32x32 texture in session 10's vinyl captures: livery
  compositing) and `vs_6ee601730a7f9f87` (318 dw, 16 fetches; prim 4 draws in a 1280x256 scissor with 512x512 DXT1
  textures in session 18's pre-race capture). A temporary `FM3_SCAN_ALL` experiment (every member of every zip and
  every loose file, Disc 1 + Disc 2, 3,850 files, 14.5 s) found **none of the three anywhere** (first 24 bytes
  searched): the game builds them at run time. The scan heuristic, even with ReGPU's `ucode::Plausible`, accepts
  ~217,000 noise "shaders" in media/mesh data (.bik, .fsb, .xma, .xds, .carbin), so scanning more file types is no
  win: no shader the game used came from them. The three cost a one-time background compile at first sight (the
  vs: 726 ms); their pipelines are in the seed and build when the game makes them (`g_seedLater`). Finding the code
  that builds them (write watch on the shader memory) is possible but not worth it. Experiment code removed.
- **Step 2 done: the tour (`tools/tour.ps1`, user request).** A PowerShell driver over the debug server instead of
  the blind `FM3_TOUR` list: boot (minimized) > Free Play > Quick Race > Acura RSX > every one of the **25
  environments** (alphabetical list, 0 = Amalfi Coast ... 24 = Twin Ring Motegi; Disc 2's included) > first layout >
  50 s of the pre-race fly-over > start > 5 camera views (RB) > pause > photo mode > quit > post-race replay >
  Continue > Track select > B. Each step is checked on a shot by `tools/analysis/tour_state.py` (pixel probes:
  mainmenu, assists, envselect, trackselect, menu, other), retried, and the tour stops with a `stuck-*` shot rather
  than pressing on (the first version drifted one screen and wandered; a PowerShell return-value bug hid one stuck
  stop at Silverstone, fixed with `| Out-Host`). ~2.4 min a stop, ~1 h for all; journal + shots in
  `out/perf/tour/<run>/`. Menu facts: the pre/post-race menus hide after ~5 s idle and the next press only wakes
  them (LB wakes without side effects); Quick Race starts with the last car; `track=N` on the dev command line
  does not steer it.
  - **Coverage, first run (full1, seed of 2,556):** 1,891 pipelines drawn over the 23 stops, **644 not in the seed
    (34%)**; new tracks added 20-120 each (Le Mans, New York, Fujimi, Nurburgring most). Menus alone: 217 drawn, 2-4
    missing. **Seed regenerated: 3,274 pipelines + 91 vertex variants.**
  - **Photo mode is washed yellow on every track** (OPEN-ISSUES D, session 28 entry). **Investigated, then paused
    by the user:** the cause is one guest tile (`one_tile=1`; `FM3_ONE_TILE=0` = correct, twice). One tile for the
    race scenario only removes the yellow, but road markings then draw over the car; two more variants changed
    nothing. Code reverted to `93ba002` at the user's request; the work is in `out/wip/photo-mode-session28.diff`
    (local), findings and leads in OPEN-ISSUES D. Until fixed: `one_tile=0` if photo mode matters.
- **Next:** a true first-boot test (empty shadercache + pipeline library in a scratch user dir) with the new seed;
  more layouts / cars per track if coverage shows gaps; the yellow photo mode; report the flag-4 bug upstream (needs
  the user's OK). Old plan line: step 2, the automated tour (`FM3_TOUR`) over every track incl. Disc 2, showroom, photo mode, livery editor,
  replays, reading the coverage line per stop; and a true first-boot test (empty `shadercache` + pipeline library in
  a scratch user folder) to see what a new player's first race builds. Report the flag-4 bug upstream (needs the user's
  OK: it posts publicly). Then the audit list below.

### ▶ Start here (session 28): state at the end of session 27 (2026-11-11/12)
- **Built in session 27 (all pushed, details in "Session 27" below):**
  - **L40, command processor sleeps while idle under a frame cap**, default at `max_fps` <= 144 or vsync (ReGPU
    patch 31 `regpu-cp-idle-sleep`, `REGPU_CP_IDLE_SLEEP_US=200` + `REGPU_CP_SPIN_MS=1`; `FM3_CP_SPIN=1` off; log
    `gpu: command processor sleeps while idle (frame cap)`): 60 cap 2.1 -> 1.02 cores, fps held. Uncapped it cost ~1%
    CPU-bound, so the user's 360 cap is unchanged. Not yet seen by the user in a capped drive.
  - Mirror experiment switches in `src/hooks/mirror.cpp` (live): `FM3_MIRROR_SKIP=<return addr|sub>,...`,
    `FM3_MIRROR_TRACE=1`, `FM3_MIRROR_CAR_LOD=N`.
- **Learned / ruled out:** the mirror on a race grid is ~1,300 of ~2,750 draws, mostly the **cars** (+40% fps if
  skipped); FM3's mirror draws every car at full detail, but a lower car detail level saves only ~50 draws, no fps
  (draws are per material). Shadow blobs off: no gain. No shadow pass of its own. Two-colour mirror: would keep every
  draw. Remaining mirror options are visible: no car glass (+10%) or fewer cars in the mirror; the user chose "back
  to the list". L37 rect clears: 2.4% of GPU, parked. L39: 0.2% of t7, not worth it. L14: 4 cores fine (-0.3/-2%),
  2c/4t -20% CPU-bound. Cockpit uses the same one-tile surface as chase (check done).
- **User decisions:** one guest tile stays **opt-in**; half-rate mirror stays opt-in.
- **The user's settings.ini:** 2560x1440, msaa 4, max_fps 360, perf_log 1, `one_tile=1`, `mirror_rate=2` (restored
  after every test today).
- **Next (the user asked at the end; offered, not started): shader and pipeline discovery.**
  1. Static scan of Disc 2 STFS packages (`<user>/dlc`; `src/static_shaders.cpp` skips them, line 16) and scan
     threads at below-normal priority (as FM2 `static_shaders.cpp:252`).
  2. An automated tour (`FM3_TOUR`, `src/kernel/imports_xam.cpp`) over every track incl. Disc 2 (Free Play quick
     race; the `quick_race` route needs `,WAIT:6,A,WAIT:2,A` appended to reach the countdown), showroom, photo mode,
     livery editor, replays; merge each run's `pipelines.manifest` with `tools/pipeline_seed.py` (FM2 seeds from 212
     manifests, we from 2).
  3. A coverage line per boot: pipelines compiled mid-game that the seed lacked.
  Then from the audit (below): the 22 unbuilt leads (most for weaker PCs or stutter), L27 rebase on upstream ReGPU
  (15 commits ahead; brings L7/L17), I6 `REGPU_OPTIMIZE_LEVEL`, FM2 branches `fm4-picks`/`proton-d3dcompiler`
  unexamined; draw-thread cost per draw (the limit at 1440p 4x: ~2 us a draw).
- **Testing notes from today:** the frozen test grid puts the player 8th (empty mirror); for mirror/pack tests the
  user drives to a Free Play quick race (Catalunya) and pauses, then `env FM3_FREEZE_TIME 1` + `press B 150` unpauses
  with the world frozen, and `press START 150` + `env FM3_FREEZE_TIME 0` restores. The freeze does not fully hold
  after GO (clock moves ~1 s per capture). Camera cycle by `press RB 500`: check each step with a shot (the order
  depends on the start view). Per-thread CPU: `tools/perf/threads.ps1` (don't assign its output to `$T` in the
  calling script). Resolution in settings.ini is `resolution=WxH`.

### Session 27 (2026-11-11): what the mirror draws (Free Play quick race, Catalunya, 4th of 8, cockpit)
- The user drove to the grid; capture (mirror every frame, `FM3_MIRROR_RATE=1` live, world frozen): **964 of 3,164
  draws (30%) are the mirror pass** (surface `06010190`, scissor 384x128, one unbroken run #427-1390, one resolve),
  vs ~150 on the empty test-grid mirror. By group (tinted live, `out/perf/s27mirror/montage.png`): **cars 369**
  (blend 07060706 shaders; 4 cars behind), **other world 273** (track, grass, barriers, buildings), **pit wall +
  fence 161** (ps `c90e1b9eab1212f3`, main view only 23), **car shadow blobs 128** (ps `1e5b88c3dde5cc88`, multiply
  blend, 64x64 texture, mirror-only variant: only a faint dark patch under the car behind), trees 34.
- Leads (cheaper than a two-colour mirror, which would keep every draw): skip the shadow blobs in the mirror only
  (~13% of the mirror, near-invisible); a lower car LOD in the mirror (cars are ~38%: check whether FM3 picks the
  mirror's car LOD by size); the pit wall's 161 small pieces are visible, leave them.
- **Shadow blobs skipped in the mirror (live `REGPU_SKIP_SHADERS=1e5b88c3dde5cc88`): not worth building.** -125
  draws (2,837 -> 2,712), picture 684 px different (0.019%, a faint shadow under the car behind), but fps +0.5%
  median / +1.1% mean over 10 alternating pairs (mirror every frame), inside this session's noise (181-211 fps in one
  state). A guest-side skip would save the game's side too, but these draws are cheap. Next mirror lead: car LOD.
- **Mirror LOD / shadows (user request), first look:** the mirror render `sub_827FDB88` only swaps two shadow-cascade
  fields of the shadow settings object (`*0x82C40EEC` +292 = 0, +532 = 50; restored after): the mirror reuses the main
  view's shadow maps (no shadow pass of its own); shadow receiving is pixel work (free, L43). No LOD override there.
  New live experiment switch in `src/hooks/mirror.cpp`: `FM3_MIRROR_SKIP=<return address or sub>,...` skips steps of
  the scene renderer `sub_8283A220` while the mirror renders; `FM3_MIRROR_TRACE=1` logs the mirror's 17 call sites.
  Montserrat quick race grid (8th, no cars behind, cockpit, ~150-draw mirror): only the first draw-list call
  (`sub_828342D8` from `8283A9BC`) draws anything (-136 draws); the other 16 draw nothing; fps unchanged (229 vs 228,
  base swinging 207-249). Cars, track and blobs share one draw list; telling them apart (and the LOD choice) is in the
  1,200-instruction draw scheduler `sub_82AF4AA8` (per-view visibility bits in 200-byte items): hours of work.
- **Catalunya grid with cars behind (user set up, 2nd/5th, cockpit, 1440p 4x), `FM3_MIRROR_SKIP` per step:** mirror
  ~1,316 of 2,753 draws. Skip all mirror cars (`sub_828329C8` from `828380BC`): -668 draws, **309 vs ~220 fps
  (+40%)**; car bodies only (1st `sub_82837530`, `8283A8D0`): -465, 270; car glass/transparent parts (2nd call,
  `8283AB40`): -211, 244 (+10%; windows look see-through at mirror size); track scenery list (`8283A9BC`): -457, 239
  (+8%); `sub_82838518` (`8283AAF4`): -184, no change. Half rate: 273. Montage `out/perf/s27mirror/cat_montage.png`.
- **Car detail level:** the main view ranks cars and sets `+596` of the object passed in r10 to `sub_828329C8` (0 for
  the nearest 1-2, 1 to rank 3-4, 2 beyond; 82837E50..82837F08); the mirror's car loop (82838044..828380BC) never
  sets it, so mirror cars are all level 0. `FM3_MIRROR_CAR_LOD=N` (live, `mirror.cpp`) sets it while the mirror draws
  them: level 2 = -50 draws, picture 0.09% different, **fps unchanged** (227.1 / 228.0 / 225.7 at 0/1/2; half rate
  258.8 vs 257.9). Car draws are per material, not per detail level (as `fastcarrender` showed in session 19): a
  lower car LOD is no lever. What would help is fewer car draws in the mirror: glass off (+10%, visible) or fewer cars.
- **Cockpit tiling check (done):** captures on the user's Catalunya race (one tile on), cockpit vs chase: both main passes
  on surface `14000500` (1,579 / 1,548 draws); the race set up scenario 1 as one tile (`one tile: scenario 1 (2 tiles)
  drawn as one 1280x720 tile`). Cockpit view uses no other scenario. The hood view shows a virtual rear-view mirror
  too (the mirror cost applies there as well). Careful: `FM3_FREEZE_TIME 1` did not fully hold after GO (race clock
  moved ~2 s over two captures and camera presses).
- **One guest tile stays opt-in (user's decision, session 27).**
- **L37 measured, not worth building now:** rect-list "clears" (pixel shader `8be0d4632b7d3122`, full scissor) are ~14
  draws a frame in a race capture (3 on the main surface `14000500`, the rest on smaller targets). Frozen test grid,
  1440p 4x, `REGPU_GPU_PASSES=1 REGPU_GPU_SHADERS=1`: the clear pair `vs ...B368B2CC / ps ...2B7D3122` = 9 draws,
  **0.097 ms of 3.97 ms GPU (2.4%)**, three 2-s blocks identical. A real clear saves at most that, and only when the
  GPU limits; with one guest tile there is no L10 tile merge left for it to unblock. (Same ps, other vs `236B889F`:
  143 small 16x16 point/quads, 0.080 ms: not clears.)
- **L39 (guest atomics without the interrupt lock): not worth it.** `PPCMsrChanged` = 0.2% of t7 in the session 20
  profile, absent from session 26's cockpit/chase profiles.
- **L40 built, default under a frame cap:** ReGPU patch 31 `regpu-cp-idle-sleep` (`REGPU_CP_IDLE_SLEEP_US=<n>`, live:
  after n us of empty ring the CP waits ~100 us steps on a high-resolution timer instead of `Sleep(0)`), plus the
  existing `REGPU_CP_SPIN_MS=1` (WAIT_REG_MEM yields 1 ms, not 100 ms, before sleeping; start time). Frozen grid, 60
  cap, 1440p 4x (`tools/perf/threads.ps1`): CP 98% of a core -> 50% (idle sleep 500 us) -> ~42-46% (20-200 us) ->
  ~18-22% (+ spin 1 ms); process 2.1 -> **1.02 cores**, 60 fps held. Uncapped: 1440p 4x 264.1 vs 263.7 (2 boots
  each), 720p 1x 328/333 old vs 324/330 new with dips (295, 275) and a live idle-sleep A/B 323.5 vs 319.7 (-1.2%):
  so `src/gpu_host.cpp` sets both only at `max_fps` <= 144 or vsync (log `gpu: command processor sleeps while idle
  (frame cap)`); `FM3_CP_SPIN=1` off. The user's 360 cap is unchanged.
- **L14 (4-core test, affinity live on the frozen grid):** 1440p 4x: all 262.7, 4 cores (0x55) 261.8 (-0.3%),
  2 cores + SMT (0xF) 257.1 (-2%). 720p 1x: 331.4 / 324.3 (-2.1%) / 264.7 (-20%); four threads busy (replay 98%,
  draw thread 92%, CP 72%, t7 52%; 3.9 cores). No thread gating needed for 4 cores; 2c/4t CPUs lose ~20% when
  CPU-bound.
- **Audit (user request, end of session 27):** PERF-PLAN's 60 ids: 14 built+default, 7 opt-in, 15 tested and
  rejected/parked, 2 measurement-only, **22 not built** (L7, L17-L23, L25, L27, L29, L31-L34, I6, I7, V2-V5, V8; the
  status column is stale for sessions 23-24, the result blocks above it are current). FH1R v0.3.2: its CP split, reg
  skip, swap without replay wait and GPU profile are covered (draw thread, L28, L12, L26); coherency waits from the
  shadow, dynamic-offset constants (L8 rest), NVIDIA-only transfers and PGO (L23) researched only. ReGPU: upstream
  `origin/main` is 15 commits past our pin (L27 rebase not done; L7/L17 from it not built); `engine-optimisation`'s
  `REGPU_OPTIMIZE_LEVEL` (I6) untested; FM2 branches `fm4-picks`/`proton-d3dcompiler` never examined. **Boot shader
  compile vs FM2:** FM2 also scans DLC/title update (we skip Disc 2 STFS), runs scan threads below normal priority
  (ours don't), seeds from 212 manifests incl. players' uploads (ours 2), has its own boot screen. Statically we find
  784 shaders; seed 2,100 pipelines + 86 vertex variants.
- Note: `quick_race=1`'s pad route stops one A short of Start Race (pre-race menu), then the track fly-over runs.

### ▶ Start here (session 27): state at the end of session 26 (2026-11-11)
- **Built in session 26 (all pushed; details in the section below):**
  - **L36 one guest tile**, opt-in (launcher Settings > Testing "One guest tile" = settings.ini `one_tile=1`,
    `FM3_ONE_TILE=1`; logged `gpu: one guest tile on|off`): `src/hooks/one_tile.cpp` + ReGPU patch 30
    `regpu-window-fixup-union`. CPU-bound grid **+9.1%**, pictures within noise. **The user drove it twice (on): "seems
    fine", no errors seen.** Open decision: make it the default (asked at the end of session 26, not answered yet).
  - **Half-rate rear-view mirror**, opt-in by the user's decision (launcher Testing "Half-rate mirror" = settings.ini
    `mirror_rate=2`, live `FM3_MIRROR_RATE=N`): `src/hooks/mirror.cpp` (mirror scene render `sub_827FDB88` every Nth
    frame; the mirror keeps its last picture). Cockpit was ~12% slower than chase while driving (the mirror's ~300
    extra draws; draw thread limits); with it the user's Catalunya cockpit run went 214 -> ~240 fps (traffic differed;
    grid A/B +2-5%), "looked good". Stays off by default: at ~60 fps a 30 Hz mirror may look choppy.
  - L38 (timer while hidden): checked, our minimized tests were never throttled; safeguard kept (`src/main.cpp`,
    `FM3_NO_TIMER_FIX=1` off).
- **Ruled out:** a stronger mirror `VisibilityThreshold` (track mod; +3.5% at 2000, but the user finds the mirror's
  draw distance short already; mod `out/user/mods/_mirror2000` left disabled); a lower-resolution mirror (0 change in
  fps/GPU at 1/2 and 1/4 scale: the draws cost, not the pixels).
- **The user's settings.ini now:** 2560x1440, msaa 4, max_fps 360, perf_log 1, `one_tile=1`, `mirror_rate=2`. Back it
  up before CPU-bound runs and restore it after, as always.
- **Parked:** a one-frame cyan bush (session 26 drive 1, not reproducible; NOTES below has the A/B plan if it shows
  again); the yellow Celica's rims flicker a little (not examined).
- **Next, in the suggested order (ask the user):**
  1. ~~Decide one guest tile as default~~ **session 27: the user keeps it opt-in.** (Was: if yes, default on in `one_tile.cpp`/`gpu_host.cpp`
     (`one_tile=0` off), launcher toggle default on, tooltip.
  2. **Cockpit tiling check** (small): the cockpit capture's main pass ran on `14000500` (1x, depth 2D0) even with one
     tile on; find out whether cockpit view uses another scenario (log `one tile: scenario ...` lines when switching
     views) and whether one tile does anything there.
  3. **L37 rect clears as real clears** (count full-surface plain-rect draws per frame first), then L39 lock-free
     guest atomics, L40 sleeps under a frame cap, L14 4-core test (masks 0x55, 0xF).
  4. Draw-thread cost per draw (it limits at 1440p 4x too: 3.4 of 3.8 ms busy on the grid): every cut there helps
     cockpit and the mirror as well.
  5. Older leads: GPU side (shader `510c814e1a4ef99e` without an L30 variant, literal sampler index, V8), stutter (L32
     with a 4 ms/frame budget, L33, L34).
- **How we test (unchanged, plus session 26):** `tools/perf_suite.ps1` (`Start-Grid` minimized, `Test-Live`,
  `Get-Fps`, `Save-Shot`). **Camera on the frozen grid: `Fm3 "press RB 500"`** cycles views (150 ms is too short);
  order chase -> hood -> bumper -> chase -> far chase -> cockpit, so check each step by a screenshot (cockpit = dark
  lower-left corner; see the `Dark` helper in the session's scripts). The perf log (`REGPU_PERF`) is only on when the
  game starts from the launcher (or `Start-Grid @{REGPU_PERF='1'}`); without it, the log's `renderer: frame N` lines
  (every 600 frames) still give fps and draws per ~2.5 s. **Driving tests:** the user records with Radeon ReLive;
  line the video up with the log (contact sheet `ffmpeg -vf "fps=0.5,scale=256:-1,tile=6x8"`; the race start/pause
  give the offset) and compare aligned stretches of the same straight (Catalunya's opening straight works).
  Per-pass GPU time: `REGPU_GPU_PASSES=1` (start time) + `tools/analysis/gpudiff.py`. Render-thread profile with
  stacks: `FM3_SAMPLE_THREAD=7`, `FM3_SAMPLE_CALLERS=6` (that is how the mirror render was found).

### Session 26 (2026-11-11): L38 checked, L36 one guest tile, cockpit mirror (details)
- **L38 (timer while hidden): not an issue for us.** Minimized test boots measure Sleep(1) = ~1.3 ms with and without
  the fix (`FM3_TRACE_SLEEP=1` logs it 3x at 20 s intervals); our limiter, vblank and ring waits use high-resolution
  waitable timers anyway. Kept as a cheap safeguard in `src/main.cpp`: `SetProcessInformation(ProcessPowerThrottling,
  IGNORE_TIMER_RESOLUTION)` + `timeBeginPeriod(1)` at start (`FM3_NO_TIMER_FIX=1` off). Earlier minimized results stand.
- **L36 one guest tile: built, opt-in (`one_tile=1` in settings.ini = launcher Settings > Testing "One guest tile",
  or `FM3_ONE_TILE=1`; logged `gpu: one guest tile on|off`).** `src/hooks/one_tile.cpp` + ReGPU patch 30
  `regpu-window-fixup-union`. Frozen grid 720p 1x **290.0 -> 316.4 fps (+9.1%, 3 boots each, all pairs +8.6-9.3%)**,
  draws 3,288 -> 2,647; 1440p 4x 250 -> 254 (one pair, GPU-bound); pictures within cross-boot noise, seam rows too.
  **Next: the user drives with it on** (races, menus = the 4x scenario is flattened too, showroom, photo mode,
  livery editor, replays); if clean, make it the default. How FM3 tiles (for later work):
  - Three static scenarios at `0x82BF60D0` (1232 bytes each): 4x MSAA 3 tiles (rows 256/512), 2x 2 tiles (512),
    1x 1 tile. +4 w, +8 h, +12 MSAA type, +16 tile count, +32 rects. Front end runs scenario 0, races scenario 1.
    Set-up `sub_82798890` (r7 = index; count 1 -> non-tiled path, MSAA into the present parameters; >1 -> tiled
    surfaces, byte +90); BeginTiling call `sub_82797C58` -> `sub_824BBA08` (byte +91 = game's own "one tile this
    frame"); per-tile resolves `sub_82797ED0`; per-draw tile masks `sub_8271CA40`; predication masks `sub_82850280`;
    tiled command buffers `sub_82855870`. `SetTilingScenario` = `sub_822A7758` (stores +2836, virtual).
  - Dead ends: count 1 before set-up (non-tiled path: world missing, part of the frame on the 4x layout); byte +91
    only (resolves still per tile: rows 512-719 black). The fix: set-up with the game's counts, then count 1 + tile 0
    over 720 rows. Prerecorded command buffers (dozens in the heap) carry **predicated per-tile window fixups**
    (`C0032D01 00040080 <offset> <TL> <BR>`); outside a tiling pass (bin select all ones) all run and tile 1's wins
    (track drawn in rows 0-207 only): ReGPU now merges them (offset 0, union of the scissors).
- **User drive 1 (20:34, video `unknown_2026.11.11-20.36.mp4`) ran with one tile OFF** (log `gpu: one guest tile off`,
  settings.ini without `one_tile`), so it says nothing about L36. Seen there (existing issues, not L36): the yellow
  Celica's wheel rims flicker a little (from ~1:17); a small bush behind the fence, left-above the car ahead, turns
  bright cyan for about one frame (video 1:22.2, timer 222.8 s, race lap 1 ~0:13.9; ~1040-1085 x 590-620 px at
  1440p). Nothing in the log then. First suspects for the bush: alpha-test-free variants (`FM3_ALPHA_KILL_ALWAYS=1`),
  plain-fetch variants, a foliage LOD switch; needs a capture at that spot. **Not reproducible** on a Restart of the
  same race (user, session 26); the vertex arena was near full then (505 of 512 MB, started over only at 274 s), no log
  event at 222.8 s. Parked: ask the user for the timer value if it shows again, then A/B `REGPU_SPLIT_ALPHA_TEST 0`,
  `REGPU_NO_VERTEX_MEMO 1`, `REGPU_NO_VERTEX_ARENA 1` (all live).
  Log stats around it: pipelines flat at 2,238 (no pipeline compiled), shaders 1,048 -> 1,049, textures rising
  (2,317 -> 2,333: streaming as the car moves), 2 skipped draws in 222-224 s. Best guess: a texture or mip arriving a
  frame late (LOD change of the bush), not a shader compile.
- **User drive 2 (one tile ON): "seems fine".** Cockpit view still costs more. Measured (drive log
  `out/logs/session26-drive-cockpit.log`, video 22.03, Catalunya opening straight, 1440p 4x, one tile on): chase
  ~243 fps / ~2,630 draws (log 905-930 s) vs cockpit ~215 fps / ~2,950 draws (950-978 s): **-12% fps with +12% draws**
  = the rear-view mirror's second scene (draw thread limits at this setting). On the frozen grid cockpit costs the same
  as chase (you start last: the mirror sees empty track; mirror pass `colour 190000 depth 050` = 148 draws, 0.18 ms GPU).
  The replay thread's "98% CPU" is AMD's driver sleeping in Present (frames queued), not work.
  Lever found: per-track `<Mirror VisibilityThreshold>` in TrackSettings.xml (32 on most tracks, CrownJewel 0; the
  cubemap's is 2,000-50,000). Test mod `out/user/mods/_mirror2000` (CrownJewel + Catalunya at 2000, disabled): grid
  mirror pass 148 -> 129 draws, mirror image 0.2% of pixels different. **User drive 3 with it on (video 22.15,
  log `out/logs/session26-drive-mirror2000.log`, same cockpit straight):** 8 aligned 2.6 s windows: draws 2,943 ->
  2,864 (-2.7%), fps 214 -> 221 (+3.5%), higher in every window (lines not identical). Mod left enabled
  (`out/user/mods/mirror2000`, CrownJewel + Catalunya). User: the mirror's draw distance is already short, so no
  stronger threshold; mod switched off again (`_mirror2000`).
- **Half-rate mirror built (opt-in): `src/hooks/mirror.cpp`, settings.ini `mirror_rate=N` (launcher Testing
  "Half-rate mirror" = 2), live `FM3_MIRROR_RATE`.** The mirror's scene render is `sub_827FDB88` (found by a t7
  profile with call stacks, chase vs cockpit: `sub_821FC878 -> sub_82839A78 -> sub_827FDB88 -> sub_8283A220`; once a
  frame, cockpit only; skipping it removes the mirror pass). The mirror keeps its last picture when skipped (0 pixels
  on the frozen grid), so every Nth frame shows the previous picture in between. Grid cockpit, live: rate 2 +1.9%
  (5/5) and +5.1% (3/4, noisier run); never +4.7%. The mirror pass: 384x128 target (2x MSAA in the game), ~147 opaque
  world draws with the main view's shaders: nothing cheap to drop by kind. Dead end: `sub_8270C3B0` ("RenderMirror"
  effect method) runs in every view, 0 pixels when skipped. Set `mirror_rate=2` for the user's next drive.
  **User drive 4 (half rate on, video 22.40, log `out/logs/session26-drive-halfmirror.log`): "looked good".** Cockpit
  ~240 fps (233-251) vs 214 before (+12%), ~chase level; traffic differed (the user reached 1st: fewer cars in view,
  draws 1,700-2,500), so the mirror's own share is between the grid's +2-5% and that. **User decision: stays an
  opt-in launcher toggle, off by default** (at ~60 fps a 30 Hz mirror may look choppy). The user's own settings.ini
  has `mirror_rate=2`.
  **Lower-resolution mirror (user request): no gain.** Tested with `REGPU_HELPER_SCALE` limited to the mirror's
  400-pitch surface (temporary filter, not kept): grid cockpit, perf log, mirror at 1 / 0.5 / 0.25 / 1 of the
  output's scale: 258.0 / 256.8 / 258.0 / 259.8 fps, GPU 3.72 / 3.74 / 3.72 / 3.70 ms; the mirror visibly blurrier
  at 0.25 (34% of its pixels). Its pass is ~0.2 ms of GPU and cockpit is draw-thread-limited: the draws, not the
  pixels, cost. (`REGPU_HELPER_SCALE` alone still scales all small passes, for weak GPUs.)
  Side note: the cockpit capture's main pass ran on `14000500` (1x, depth 2D0) with one tile on: check whether the
  cockpit view uses another tiling scenario.
- Then from PERF-PLAN "Next up": L37 rect clears as real clears, L39/L40/L14, the rest.

### ▶ Start here (session 26): state at the end of session 25 (2026-11-11)
- **Session 25 built (all default on, pushed, the user drove both):** L12 asynchronous frame end (patch
  `regpu-async-present`; launcher Settings > Testing "Asynchronous present" = settings.ini `async_present`, logged
  `gpu: asynchronous present on|off`; `FM3_SYNC_PRESENT=1` off) and the draw-thread memos (patch
  `regpu-draw-thread-memos`: vertex-cache memo, pass-scale snapshot, timer/overlay uploads without the replay wait).
  29 ReGPU patches. **CPU-bound grid (720p 1x) 216.5 -> ~288 fps**; 1440p 4x ~236-246 (GPU-bound; it drifts ~2% as
  the GPU warms). User: "a straight improvement", cockpit view "noticeably" better, "a good deal" higher fps floor.
  Draw-thread sampler takes whole call stacks now (`FM3_SAMPLE_CALLERS=N`, live; details in part 1 below).
- **Measured, not built:** upload hashing (6 MB/frame; `REGPU_UPLOAD_STABLE_REUSE` +1.2%, off for its stale-frame
  risk: the user's call); L10 tile merge scoped (**18% of draws are second-tile copies**; blocked by FM3's rectangle
  tile clears and the draw thread).
- **New leads from mcla-recomp** (RESEARCH.md "Midnight Club LA recomp", PERF-PLAN L36-L40). **Suggested order for
  session 26 (ask the user):**
  1. **L38 first (small, affects our test method):** Windows 11 drops `timeBeginPeriod(1)` for a process with no
     visible window, and every automated boot runs minimized. Check whether minimized runs sleep 15.6 ms (ring wait
     mode 2, `TimeStampThread`); fix with `SetProcessInformation(ProcessPowerThrottling,
     PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION)` + `timeBeginPeriod(1)` at start; re-check the grid baseline.
  2. **L36 one guest tile** (instead of L10's merge): first test = the game's own MSAA off (does FM3 then draw one
     tile? ReGPU's host MSAA is set apart); then find FM3's tiling setup (`xref.py`, the predicated-tile IB replays
     are visible with `REGPU_TRACE_IB`) and the EDRAM size check. Watch the tile seam (row 512) and the S16 4x-view rules.
  3. **L37 rect clears as real clears** (count full-surface plain-rect draws first), then L10 only if L36 fails.
  4. **L39 lock-free guest atomics** (profile `PPCMsrChanged` on t7/t3 first), **L40 sleeps under a frame cap**,
     L14 4-core test (affinity masks 0x55 and 0xF).
  5. Still open from before: cockpit cam (better since L12: re-check only if the user still finds it low), GPU-side
     leads (shader `510c814e1a4ef99e` without an L30 variant, literal sampler index, plain multiply per shader, V8),
     stutter (L32 with mcla's 4 ms/frame budget, L33, L34).
- How we test: unchanged (`tools/perf_suite.ps1`: `Start-Grid` minimized, `Test-Live`; CPU-bound = settings.ini
  1280x720 msaa 1, backed up and restored). A switch whose "off" side is not the old code (e.g.
  `REGPU_NO_PASS_SNAPSHOT`) needs old/new exe boots instead: build the old variant to a copy, alternate boots.
  Close the game before linking (a running fm3.exe fails the link silently in a script).

### ▶ Session 25, part 2 (2026-11-11): draw-thread memos; upload hashing and tile merge scoped
- User: "go ahead with the four candidates" (vertex cache lock, upload hashing, pass-scale helpers, L10 tile merge).
  Re-profile after L12 (CPU-bound grid, 253.8 fps): a `Real()` wait at the swap was left (2.7%): the timer/shader
  overlays recorded their uploads on the real list. Patch **`regpu-draw-thread-memos`** (29 ReGPU patches):
  1. **Timer/overlay recorded** through the recorded list (no replay wait): boots 253.8 -> ~270 fps (with 2).
  2. **Vertex cache memo**: each thread remembers this frame's arena answers, so ~2,600 hits a frame skip the texture
     mutex; `REGPU_NO_VERTEX_MEMO=1` off (live): **+3.6%** (5/5, 0 pixels); 0% at 1440p 4x (GPU-bound).
  3. **Pass-scale snapshot**: `BigPassSize`/`PassWidth`/`PassHeight`/`PassScale`/`ScaleX/Y/Wide` worked out once per
     input change: **+2.2%** over alternating boots of old/new exes (279.3, 280.8 vs 288.3, 284.0), byte-identical.
  **CPU-bound grid now ~288 fps** (session start 216.5 with sync present; 255 after L12). 1440p 4x: 235-240 (GPU
  drifts ~2% over minutes as it warms; timer on/off 0).
- **Upload hashing, not changed:** 6.0 MB hashed a frame for 1,446 small-buffer uses (<16 KB: 369 copies, 1,077 reuses
  hashed only to prove nothing changed). Write-watching them would fault every frame (they change every frame).
  `REGPU_UPLOAD_STABLE_REUSE=1` (live) hashes 2.5 MB instead: **+1.2%** (4/4, 0 pixels), with the known risk (a range
  unchanged for 600 frames that changes mid-frame is drawn stale for the rest of that frame: FH1's skid marks). Off;
  the user's call.
- **L10 tile merge, scoped only:** **18% of draws (~600 a frame) are second-tile copies** (temporary counter: draws in
  indirect buffers run again with another window offset). Still refused on the race scene (`14010500`: its second
  tile starts with a rectangle clear; ReGPU keeps such surfaces unmerged) and off with the draw thread. Building it =
  a way to keep the second tile's rows cleared without its rectangle clear + carrying the widen decision through the
  draw thread; risk at the tile seam (row 512). Next session's candidate.
- **User drove part 2** (1440p 4x, one race, ~64 s, log `out/logs/session25-drive-memos.log`, graph
  `out/perf/s25/drive-memos.html`): "a good deal to raise the FPS floor". No errors, 0 compiles. 251.1 fps average,
  1% low 200.5, lowest stretch 200.3, nothing under 180 (L12 drive: lowest 197-208; short drive, views differ, so the
  log alone doesn't separate the two).

### ▶ Session 25 (2026-11-11): draw thread, L12 asynchronous frame end
- User's pick: the draw thread. Profiled ReGPU's draw thread ("gpu draw worker") on the CPU-bound grid (settings.ini
  1280x720 msaa 1, backed up and restored) with whole call stacks: **~10% of its time was waiting at the frame end**:
  ~7% asleep inside AMD's driver (`amdxc64.dll` -> `SleepEx`) in `Present` (frame-queue throttling), ~2.5% waiting for
  the command-list replay thread at the swap (`RecordedList::Real`). The rest: ~11% waiting for the command processor
  (normal), `DrawImpl` body ~20%, hashing ~6%, pass-scale helpers ~5%, VertexCache lock release 2.7%.
- **Built: L12 (patch `regpu-async-present`, 28 ReGPU patches)**: `REGPU_ASYNC_PRESENT=1` (live) queues the frame end
  (back-buffer copy, close, submit, Present, frame fence) on the replay thread in stream order (new recorded-list ops
  `Call`/`Switch`); the next frame records into a pool list of its own slot without waiting. Every other fence signal
  and Present in ReGPU drains the replay first (`Real`), so fence values stay in order; a resize drains too. **FM3
  default** (`src/gpu_host.cpp`; `FM3_SYNC_PRESENT=1` off). Frozen grid, live, 5/5 rounds, byte-identical: **720p 1x
  216.5 -> 255.2 fps (+17.9%), 1440p 4x 232.4 -> 242.3 (+4.3%)**. Boot, shots and a full `capture` work with it on.
  Possible side effect: one more frame may be queued (input latency +~4 ms at 240 fps).
- **User drove L12** (1440p 4x, two career races, log `out/logs/session25-drive-l12.log`, graph
  `out/perf/s25/drive-l12.html`): "a straight improvement overall", and **cockpit view noticeably better** (the open
  cockpit-cam note from session 24 is helped; re-check it later if it still feels low). No errors, 0 shader compiles.
  Race 1: 263.8 fps average, 1% low 210.7, lowest stretch 208; race 2: 311.1 average, 1% low 204.8, lowest 197; no
  stretch under 180 in either (session 24's CPU drive: 235.6 average, 1% low 183, lowest 175; camera views differ).
- Explained: the `~PsAlias` "16-byte store" (7%) is the draw's common exit: the first store after the draw's burst of
  writes waits for the store buffer to drain; copying every constant (3x the bytes, `REGPU_ALL_CONSTANTS`) costs only
  -6.2%, so constant writes are a small share. GPU time at 720p 1x is ~3.1 ms of a ~4.3 ms frame.
- Sampler (`src/kernel/thread.cpp`): `FM3_SAMPLE_CALLERS=N` (2-16, live); the stack is copied while the thread is
  stopped and unwound after it resumes, and nothing that takes a lock (getenv, allocation, LiveSwitch construction)
  runs while it is stopped: deep unwinds and a getenv there froze the game twice (fps 0). Frames without symbols show
  as `module+offset`; " ~ name" = the nearest fm3.exe return addresses found on the stack (heuristic).
- Next: PERF-PLAN "Next up" (re-profile the draw thread after L12; cockpit cam if it still feels low); ask the user.

### ▶ Start here (session 25): state at the end of session 24 (2026-11-11)
- Session 24 built (all default on, all pushed): first-boot shader compile (static scan + pipeline seed with vertex
  variants), L30 plain-fetch shader variants, the CPU cuts (lock/getenv cuts, shader-load repeat skip, L28 register
  skip). 27 ReGPU patches. Frozen grid 1440p 4x ~225 fps (GPU and CPU limits now close); CPU-bound grid (720p 1x)
  214.5 -> 237.6 fps. The user drove the L30 and the CPU builds: both looked normal. Details: parts 1-3 below.
- **Next: the remaining performance leads, PERF-PLAN.md "Next up (from session 25)"**: (1) cockpit cam performance
  (user's open note), (2) the draw thread (still the CPU limit), (3) GPU-side leads, (4) other machines, (5) stutter.
  Ask the user which.
- How we test now: `tools/perf_suite.ps1` (`Start-Grid` boots minimized, `Test-Live` interleaved A/B with a pixel
  diff); CPU-bound runs: back up `out/user/settings.ini`, set 1280x720 msaa 1, restore after; per-thread CPU
  `tools/perf/threads.ps1`; draw-thread profile `FM3_SAMPLE_THREAD=name:gpu draw worker` (+ live `FM3_SAMPLE_CALLERS`,
  `FM3_SAMPLE_LINES`, `FM3_SAMPLE_RVA`); GPU time `REGPU_GPU_PASSES=1`; shader statistics `tools/rga_shaders.ps1`.
  Close the game before relinking (a running fm3.exe makes the link fail and the next boots test the old exe).

### Session 24, part 3 (2026-11-11): CPU side
- CPU-bound grid (settings.ini 1280x720 msaa 1, backed up and restored): ~214 fps minimized. `tools/perf/threads.ps1`
  (CPU per OS thread, named, no cost): **ReGPU's draw thread ("gpu draw worker") 95% of a core = the limit**; CP (t9)
  57%, game t3 35%, t7 31%, command list replay 10%, xma decoder 8%; ~2.8 cores in all.
- Draw-thread profile (`FM3_SAMPLE_THREAD=name:gpu draw worker`, new; `FM3_SAMPLE_CALLERS`/`FM3_SAMPLE_LINES` live):
  ~11% waiting; `DrawImpl` body 21%; pass-scale helpers ~10% (BigPassSize, PassScale, ScaleY, TallView, ScaleWide,
  WindowScissor); content hashing ~6%; locks ~5.5% (mostly `IsPsEditAlias`); `getenv` 1.2% from **our**
  `ExperimentWriteback` (FM3_WRITEBACK read at every resolve).
- Cut (patch `regpu-draw-thread-cuts` + `src/gpu_host.cpp`): no mutex in `IsPsEditAlias` without live edits,
  `FetchShader` memo per thread, `FM3_WRITEBACK` read 4x a second. 211.7/217.3 -> 220.7/220.2 fps (two boots each,
  ~+2.8%, noise +-1.5%). Remaining profile is flat: DrawImpl body 21%, waits ~10%, VertexCache mutex release 2.7%,
  upload hashing 2.5%, shader-load memcmp (`FrameOnShaderLoad`) 3.4%, pass-scale helpers ~4%. Next: ask the user
  (structural: L8 per-draw state reuse, L28 handoff; or the small ones above).
- **Small cuts, round 2 (user's pick):** shader-load repeat skip (patch `regpu-shader-repeat-skip`, live
  `REGPU_NO_SHADER_REPEAT_SKIP`): 55% of ~2,200 shader loads a frame are exact repeats; the CP drops them. Live A/B
  222.9 -> 228.3 fps (+2.4%, 5/5), 0 pixels. **Session total on the CPU-bound grid: 214.5 -> 229.4 fps (+7%)**, two
  boots each; the draw thread is still the limit (94%). Not done, on purpose: upload hashing (the safety net for vertex
  data rewritten mid-frame: I5's risk), the VertexCache lock (contended: needs a redesign), per-draw caching of pass
  scale (surface state can change inside a draw). Next CPU step: the structural L8/L28 (ask the user).
- **L28 built** (user's go; patch `regpu-reg-skip`, live `REGPU_NO_REG_SKIP`): the CP sends only register writes that
  change the draw thread's file (shadow of what was sent; runs cut to changed words). ~660,000 register words a frame,
  ~29% change (FH1R: ~30%). Live A/B +8.1% (5/5, 0 pixels); boots 235.1/238.1/239.5 = 237.6 fps.
  **CPU session total: 214.5 -> 237.6 fps (+10.8%)** on the CPU-bound grid; the draw thread is still the limit (~93%).
- **L8:** ReGPU already skips unchanged D3D state per draw (viewport, scissor, stencil ref, blend factor, tables,
  topology, index buffer, PSO). Tried reusing the previous draw's pixel-constant block when identical (50% of draws
  have the same PS constants, 31% VS): **-2.4% (0/5 rounds), reverted**; the compare/bookkeeping cost more than the
  upload writes. The 6-8% "closing brace of DrawImpl" sample is `~PsAlias` restoring the context's shader hashes (a
  16-byte store that waits behind earlier stores); not explained further. Sampler: `FM3_SAMPLE_RVA=1` (live) logs
  addresses; disassemble with `llvm-objdump --start-address` / `llvm-symbolizer --inlines --obj=build/rel/fm3.exe`.
- **User drove the CPU changes** (career race, log 25.7-176 s): "seems good", no visual issue reported; 0 compiles, 0
  failures. 235.6 fps average, 1% low 183, lowest stretch 175 (graph `out/perf/s24/drive-cpu.html`); not comparable with
  the L30 drive (300.6): camera views differed (cockpit used, see below).
- **Open, user's note (later): cockpit cam performance still disappointing.** S23 grid numbers: cockpit 221.8 vs chase
  223.1 (same on the frozen grid), so the cost shows while driving (more of the car interior, mirrors?, other cars
  closer). First step: a drive with the camera switched at known times (or the frozen grid + live camera cycles while
  the race runs), `REGPU_GPU_PASSES=1` per view, and `tools/perf/threads.ps1` to see whether it is GPU or CPU.

### Session 24, part 2 (2026-11-11): L6 shader profile + L30 fetch variants
- **L6** (RGA 2.15 downloaded with the user's OK to `G:/claude code projects/tools/rga-2.15`, outside the repo):
  `tools/rga_shaders.ps1 -RootSig <file> -Hlsl <files>` compiles translated pixel shaders for RDNA3 through the driver
  (root signature from `REGPU_DUMP_ROOTSIG=<file>`, HLSL from `Fm3 "hlsl ..."`). The five heaviest grid shaders: car
  paint `e41e183a423e6f1b` 80 VGPRs (occupancy fine, no spills) but **117 KB of code** (21,886 instructions, 236
  sample instructions for 17 fetches, 1,064 branches; RDNA3's instruction cache is 32 KB); `510c814e1a4ef99e` 97 KB,
  `b117fbafcc1fa980`/`f7841c0a8245eabf` 33 KB. Cause: every lean `TFetch2D` carries all the fetch-constant special
  cases (mirror, border, axis clamp, sub-image, 16-bit fixed, gamma, exponent, swizzle).
- **L30 built** (patch `regpu-fetch-variants`, `REGPU_FETCH_VARIANTS`, live; **on by default** in `src/gpu_host.cpp`
  with lean fetch, `FM3_NO_SHADER_SHORTCUTS=1` or `Fm3 "switch REGPU_FETCH_VARIANTS 0"` off): per draw, the slots whose
  fetch constant is plain get a variant calling `TFetchPlain` (one sample). Car paint 117 -> 20 KB (72 VGPRs),
  b117 33 -> 14 KB, f784 33 -> 13 KB. Frozen grid: **GPU frame 4.42 -> 4.31 ms (-2.5%, 4/4 rounds)**, pictures
  **byte-identical** (0 pixels, live A/B, crowd included); fps -0.3% (5 rounds) and +1.2% (3 rounds) in two sessions:
  this PC sits on its ~231 fps CPU ceiling, so the GPU saving shows on GPU-bound machines. First boot with it compiles
  ~57-100 variants once (base shaders used meanwhile, no hitch); the manifest records them (9th field `fetchMask`) and
  the seed now has 2,100 pipelines (317 fetch variants) + 86 vertex variants. `510c814e1a4ef99e` got no variant
  (some texture not plain): next lead there; also the remaining car paint cost is math/sampling, not code size.
- **User drove it** (career race, ~3 laps): 300.6 fps average, lowest stretch 182 (S23 drive 1: 258, lowest 91), one
  background compile, no errors (PERF-RESULTS session 24). **The user confirmed it looked normal**: the fetch variants stay the default.

### Session 24 (2026-11-11): shaders compiled at first boot, start minimized for tests
- **Static shader scan** (`src/static_shaders.cpp`, ported from the FM4 port's, MIT): `Host::staticShaders` lists every
  shader in the game files (10 loose `.fxobj` in `Media/Shaders`, 752 inside the track `bin.zip`s, 256 in the
  executable: 784 in all, 549 pixel), saved as `<user>/static_shaders.bin` with a stamp of the files searched (a
  changed game folder searches again; ~0.1 s). ReGPU compiles the uncached ones behind its progress overlay before the
  game goes on. `FM3_NO_STATIC_SHADERS=1` / `REGPU_NO_STATIC_SHADERS=1` off. Disc 2 packages are not searched.
- **Pipeline seed** (`src/pipeline_seed.h`, made by `tools/pipeline_seed.py <manifests>`): 1,783 pipelines + 86
  vertex-shader variants (`V` lines) from our manifests (hashes and render state only). `REGPU_NO_SEED_PIPELINES=1` off.
  `V` lines only appear in a manifest once the static shaders are known (ReGPU matches a run-time vertex shader to its
  shipped one), so the user's old manifest had none; regenerate the seed from manifests of runs with this build.
- **Measured** (frozen-grid route, fresh user folder): cold boot 1 (seed without variants) waited 53 s for 739 shaders,
  then 110 compiles on the way to the race (58 vs, 52 ps) and a 353 ms hitch in the race load; cold boot 2 (with the
  86 variants) waited 54.6 s for 826, then 60 (5 vs, 55 ps, all during loading screens) and no frame over 250 ms after
  the race start. Second boot: list loaded, 1 to compile, grid at 24 s as usual. On the user's warm cache the first boot
  of this build compiled 415 new ones in 42.7 s. Grid fps unchanged (225.8).
- **Leftovers fixed (same session, patch `regpu-seed-variants`):** (1) ReGPU's seed builder built the 265 flag-0x400
  seed pipelines with the base pixel shader, not the alpha-test-free variant (same picture, slower; the warm-up and
  draws did it right): the variants are now queued together and used; (2) the static wait stops once <=3 shaders are
  left for 5 s (vs `5bff4886221c3c2d`, never drawn, 37-44 s alone), they finish in the background
  (`REGPU_STATIC_WAIT_ALL=1` waits); (3) the scan find that never compiles (vs `8f3951a3cb05a9ea`, FXC X3504) is left
  out (`kSkip` in `src/static_shaders.cpp`, saved list version 2). **Cold boot 3:** wait 55 -> 35 s (30.4 s shaders +
  3.6 s for 1,284 seed pipelines and 237 variants), grid at 58 s instead of 79, compiles after the boot 60 -> 15 (4 vs,
  11 ps), no frame over 250 ms after the race start, 224.6 fps minimized, grandstand checked by eye (flags in colour).
  Its second boot: 0 compiles, grid at 22.7 s. Still open: the first-boot overlay not seen by eye (the debug server's
  shots are the game's frame; the user sees it after clearing `out/user/shadercache`).
- **`FM3_MINIMIZED=1`** (`src/gpu_host.cpp`): minimizes the game window once ReGPU opens it, without taking focus.
  `tools/perf_suite.ps1` `Start-Grid` sets it by default (pass `@{FM3_MINIMIZED='0'}` to see the window); set it for
  any hands-off boot. Debug-server screenshots still work; grid fps ~1.3% lower minimized (214/221 vs 218/224 shown).

### Session 23, part 3 (2026-11-11): drive tests, new defaults
- User drove the CrownJewel career race + a time trial with lean fetch 1 + plain multiply + 10-bit copies: first-lap
  stutters were one-time builds of the new shader variants (now listed in the pipeline manifest and built at boot);
  **plain multiply turns the crowd's waving flags black** (found by the user; my cross-boot diffs masked the crowd):
  removed from the test set and from the Turbo preset.
- **New defaults: `REGPU_LEAN_FETCH=1` + `REGPU_COPIES_1010102=1`** (`src/gpu_host.cpp`, `FM3_NO_SHADER_SHORTCUTS=1` =
  off), after a second recorded race with nothing wrong. Frozen grid 225.6 fps (was ~221).
- Camera views on the frozen grid (RB cycles 5): cockpit 221.8 = chase 223.1, hood/bumper ~232; a one-time cost on
  first entering cockpit. Race dips follow traffic (AI cars close).
- Engine audio G: no silences in the recording; the log's XMA stream restarts (hundreds of codec reopens per 5 s)
  stay the lead (port FM2's trim handling).
- Tools: `tools/perf/drive_graph.py` (fps graph of a drive from the log's swap stamps), `crops.ps1`, `sheet.ps1`,
  `report_s23.py` + `make_pairs.ps1` (the picture report `out/perf/s23/report.html`).
- `play.cmd` is back to normal (no temporary lines).

### ▶ Session 23, part 2 (2026-11-11, user AFK): every testable performance lead, hands-off (start here)
Full results, pictures and how to read them: **[PERF-RESULTS.md](PERF-RESULTS.md)** (summary at its top); raw numbers,
journal and every screenshot in `out/perf/s23/` (local). Harness: `tools/perf_suite.ps1` (boot to the frozen grid
with start-time env, fps samples, shots, interleaved live A/B, CSV), phase scripts `tools/perf/session23/s23_*.ps1` (copies of `out/perf/s23_*.ps1`).
- **Grid sameness (user's question)**: the same 7 AI drivers in the same order and spots on every boot (3 boots,
  `FM3_AI_POOL_LOG=1` rosters). Only the crowd/pit crew/flags animate differently, plus ~50-70 single pixels on AI car
  edges; `FM3_FIXED_DT=1` does not remove those. Within one session frozen shots are byte-identical.
- **Made default: `FM3_RING_WAIT=2`** (`src/hooks/ring_wait.cpp`, L11): D3D's ring/fence wait sleeps ~100 us instead
  of spinning: same fps at 1440p 4x and 720p 1x, same frame times, identical pictures, game CPU -17-19%.
  `FM3_RING_WAIT=0` (live) = old spin.
- **Fixed: Mild/Potato/Low/Extreme presets broke the race picture** (white rectangles). Cause: `REGPU_HELPER_SCALE`
  (an exposure/bloom-type pass counted as a helper) and `REGPU_FREEZE_HELPERS` (a per-tile pass frozen for one tile).
  Both are out of the presets (`src/gpu_host.cpp` kPresets, launcher texts); all six presets checked in a race.
- New: L15 driver exports (`src/main.cpp`), ReGPU patch `regpu-gpu-stats` (`REGPU_GPU_PASSES=2`: pipeline statistics
  per draw: overdraw by pass and pixel shader; 22 ReGPU patches now), `tools/analysis/pngdiff_mask.py` (diff with the
  crowd masked), `pngdiffimg.py` (diff picture), launcher build with the fixed presets.
- Key numbers (1440p 4x, frozen grid, ~220 fps): vertex arena +10.5%, alpha-test-free +4.1%, draw thread +17.5%
  (all already default); **lean fetch 1 + plain multiply + 10-bit copies +5.1%** (not default: needs a drive);
  Turbo +3.5%; L3 optimised shaders -6%, L9 submit-512 -5.7% (but +5.3% when CPU-bound), draw cull -1.3%.
  GPU frame 4.39 ms, 68% the main scene pass; car paint `e41e183a423e6f1b` 0.5 ms (17 fetches, not overdraw).
  **CPU-side ceiling ~231 fps** (720p 1x): GPU-only savings stop at about +5% on this PC.
- `perf_log` was off for the runs; the user's settings.ini is restored (backup `settings.ini.s23-backup`). The save
  changed from ~40 boots (career not advanced: still the CrownJewel grid); backup `out/profile-backups/s23-grid-route-start`.
- **Next (ask the user):** decide the +5.1% set (a drive: sun/glare/paint/liveries), I8 anisotropy (visible),
  L30 fetch variants for the car shaders, CPU-side cuts (L8/L28) to lift the ~231 ceiling, fixing the helper passes
  for FM3 (then presets could gain again), L6 RGP profile of the paint shader.

### ▶ Session 23 (2026-11-11): test grid by itself + frozen world (start here)
- **Merged** the cloud research branches (sessions 21, 21b, 22) into `main`: PERF-PLAN.md, RESEARCH.md studies, NOTES.
- **`FM3_FREEZE_TIME`** (LiveSwitch, `src/hooks/frame_time.cpp`): main-loop dt 0, the world stops (cars, cameras,
  countdown, race clock), every frame is still drawn (~190 fps on the grid, 1440p 4x). Two screenshots of the frozen
  grid 2-3 s apart: **0 pixels differ**. `Fm3 "env FM3_FREEZE_TIME 1"` / `0` anywhere, live. `FM3_FIXED_DT=1`
  (dt = 1/target fps) is in but not tried.
- **Test grid route** (`grid_test=1` in settings.ini, launcher "Start on the test grid", or `$env:FM3_GRID_TEST='1'`
  before starting `play.cmd`; `src/hooks/grid_test.cpp`): presses A ~3x a second from boot (title > Forza Motorsport 3 >
  Go Race > Season Play > calendar > event Start Race > track load > Pre-Race Start Race), stops when the race-start
  sequence runs, and **freezes at 9.0 s of its countdown clock** (mid-"1", chase camera, AI cars on their grid spots).
  Two runs: frozen 23.1 / 22.8 s after launch, no input from anyone. `Fm3 "env FM3_FREEZE_TIME 0"` lets the race go
  on. Expects the Baseline profile with CrownJewel (Ladera Test Track, Short Circuit Reverse, 8th, Scion xD) as the
  next Season Play event. `FM3_GRID_FREEZE_AT=<s>` moves the freeze point.
- **Across two boots** the frozen grid differs in 0.65% of pixels, all in the animated grandstand crowd (left) and
  the spectators (right): cars, track, sky and HUD are identical. Within one game session it is byte-identical. For
  start-time A/Bs (restart between sides) compare with the crowd boxes left out, or crop to cars/track.
- How the race start was found: the "321Tuning" settings (`sub_822E77D0` loads HumanRevScript/AIRevScript/
  PreRaceCamsFadeOut into `0x82C37520..70`); their reader `sub_822E6AD0(this, dt)` is the race-start sequence's
  update. Fields: phase byte +4 (3 in a career race), flags +24/+25/+26, a double clock at +16. Flags 1/1/0 = Start
  Race taken (short load, ~1.3 s), 0/1/0 = car intro + countdown running (clock from 0), 0/1/1 at GO (clock stops at
  9.5). Clock vs the user's video: "3" ~6.7 s, "2" ~7.7, "1" with the chase camera settled ~8.6, GO 9.5. The pre-race
  predicate `sub_823D5600` (ShowPreRaceAnd321InCmdlineLaunch) is not called during the intro/countdown: a dead end.
- Launcher fix: `skip_intro` and `quick_race` were read from settings.ini but never written back (the toggles did
  nothing); now saved, with `grid_test`.
- Virtual pad lessons: `Fm3 "hold A 400"` **latches** A until `release` (the number is ignored; use `press A 150`);
  menus hide after ~5 s without input and the first press only wakes them; after a game restart dot-source
  `fm3ctl.ps1` again (the old connection is dead).
- Save backup before this session's career-race runs: `out/profile-backups/s23-grid-route-start`.
- **Next:** use the frozen grid for the perf/picture A/Bs (PERF-PLAN step 1: live switches on the frozen grid, now
  without the pause menu over the picture); try `FM3_FIXED_DT=1` with the route to see whether the crowd differences
  across boots go away; maybe a "regrid" (pause > Restart > Start Race > freeze) without rebooting.


### ▶ Session 22 (2026-11-10, cloud, research only; branch `ccr-acb8fc05-rpzqri`)
No code changed; the game state is as at the end of session 20/21. All findings are in RESEARCH.md (index rows marked
session 22 at the top) and PERF-PLAN.md (new L24–L35, I8, V8; L25 rewritten). Studies: FH1R's latest perf work and its
ReGPU plugin, upstream ReGPU past our pin, Sonic Unleashed Recompiled, Pinyon Shift, ITC-Recomp (its interpolation:
no perf merit for FM3), upscalers/frame generation (FSR 4 INT8 for the user's RX 7000 via FSR SDK 2.4), an FPS
counter + graph, the legal/ESRB screens, triple screen (three instances over a virtual LAN; one instance), engine
sound (we apply no XMA trims: port FM2's trim handling first), VR after FH1R. Session log entry "session 22".
**Next (ask the user):** on the user's PC, best first steps: L26 GPU time by kind on the race grid, I8
`REGPU_ANISO_FROM_FETCH` A/B, the XMA trim port for the engine sweep, the legal/ESRB entry dump, the CrownJewel
test-grid route (plan in the session 22 log; needs the "countdown started" signal), the FPS overlay patch.

### Session 21b (2026-11-11, cloud, research only, no code; branch `ccr-2e46a7d8-wv8coh`)
The user asked about **AMD Anti-Lag 2 and NVIDIA Reflex**: both feasible (MIT SDKs, a few hundred lines), write-up
and plan in RESEARCH.md "Input latency". Order proposed: latency probe -> our own frame-queue limit (any GPU) -> Anti-Lag 2
(testable on the user's 7900 XTX) -> Reflex (needs an NVIDIA tester). Waiting for the user's go.
Also looked at FH1R's "Use FIFO latest ready" (a Vulkan present mode; our D3D12 equivalent = flip `Present(0, 0)`):
RESEARCH.md, same section.

### ▶ Start here next session (state at the end of session 20, 2026-11-10)
All work is committed and pushed (`main`, last commit "Session 20 wrap-up"). `play.cmd` is back to normal (no temporary
lines). **21 ReGPU patches** (new: `regpu-split-alpha-test`). Session 19's open list (attract video, vertex arena A/B,
presets in a race, grass, older items) is unchanged: it is in the session 19 block below.
**What session 20 found and changed (session log "session 20"; numbers in OPEN-ISSUES L):**
1. **The game's render thread t7 is not the frame's limit.** Its 96-98% "CPU" is mostly spinning: profiled on the paused
   CrownJewel race grid, ~65% in D3D's ring-space wait (`sub_824B4F78` -> `sub_824AAA68`, waiting for ReGPU to consume
   the ring), ~2% asleep, ~33% game work. GetThreadTimes counts spinning as busy: never read a thread's % as work
   without a profile (`FM3_SAMPLE_THREAD=<id>` at launch, callers live with `Fm3 "env FM3_SAMPLE_CALLERS 1"`).
2. **The ~100 fps gap between a race and a time trial** (198 vs 311 fps, 1440p 4x, CrownJewel, same view from the 8th
   grid spot) is GPU time: 4.80 vs 3.33 ms per frame, ~1.3 ms of the 1.47 being the seven AI cars' material shaders in
   the main scene (car paint `a102319a5aef757b/e41e183a423e6f1b` 63 vs 8 draws +0.32 ms, then a few at 0.1-0.23 ms):
   ~0.19 ms per car, pixel shading cost, not vertices (so lower car LODs don't help; `fastcarrender` was +1.3%).
3. **No duplicate or wasted draws** (`tools/analysis/dupdraws.py` on a capture with `REGPU_TRACE_VC`): 2 exact duplicates
   (tiny quads), 4 two-sided transparent car parts drawn back then front (intended).
4. **New default: alpha-test-free pixel shaders** (patch `regpu-split-alpha-test`, `REGPU_SPLIT_ALPHA_TEST`, on in
   `src/gpu_host.cpp`; `FM3_ALPHA_KILL_ALWAYS=1` off). Every translated pixel shader carried the run-time alpha test's
   `discard`, which can cost early depth rejection; draws with the alpha test and alpha-to-mask off now use a variant
   without it. Race grid, paused, 4 interleaved rounds: **203.0 -> 213.6 fps (+5.2%)**, screenshots byte-identical. User
   drove ~3 laps: nothing wrong. Variants are cached (`out/user/shadercache`) and their pipelines are in the pipeline
   manifest (prebuilt at boot; race ones get recorded on the next race).
5. Tools: `tools/fm3_ab.ps1` (interleaved live A/B of a switch), `tools/analysis/gpudiff.py` (two `REGPU_GPU_SHADERS`
   logs compared), `dupdraws.py`, `pngdiff.py` (pixel diff of two shots). `REGPU_GPU_PASSES`/`REGPU_GPU_SHADERS` are
   read at start only (temporary `play.cmd` line).
**Session 21 next (pick with the user):** the time-freeze switch (`FM3_FREEZE_TIME`, small); the car-physics RE in a race (float scan + watchpoints), which unlocks SimHub telemetry, AI throttle/brake/clutch control and the AI-driven benchmark; VRS measurement; installer decisions (ship exe or build locally, signing).
**Session 22 (cloud):** FH1R + Sonic Unleashed perf survey (RESEARCH.md, PERF-PLAN L24–L29, I8); best next GPU-side steps: L26 GPU time by kind, I8 anisotropy from the fetch.
**Session 21 (cloud):** [PERF-PLAN.md](PERF-PLAN.md) collects every performance item not yet built or measured (from this file, RESEARCH.md, OPEN-ISSUES.md), grouped by picture effect, target machine and live/start-time A/B, with a suggested order. Pick the next perf test from there.
**Session 21 (cloud, research only):** RESEARCH.md "Installer for a public release" (FM2/FM4 Inno Setup + bundled Python + GitHub patch-zip updater; FH1R builds the game on the player's PC, SignPath signing; FM3 recommendation: Inno per user, the launcher's C# XISO/STFS instead of Python, self-contained launcher; decide: ship the exe or build locally) and "Benchmark tools" (FH1R hands the player car to the native race AI via the opponent factory + releasing the human override; FM3 options; **cheapest win: a live `FM3_FREEZE_TIME` in `frame_time.cpp` (dt 0) freezes the world while rendering continues**; then a dev-option search with `xref.py options`).
**Next (the user asked how to make AI cars cheaper without visible change; pick with the user):**
- (1) **Re-measure the AI-car cost** with the new default (race grid vs time trial from the 8th spot, `REGPU_GPU_SHADERS=1`
  at launch, `gpudiff.py`): how much of the 1.3 ms is left.
- (2) **Hidden-part cull on the race grid** (live): `Fm3 "env REGPU_DRAW_CULL 1"` (occlusion-predicated draws; 1 px, the
  64 px threshold flickered in session 19), `tools/fm3_ab.ps1`-style A/B plus `pngdiff.py` and a shot burst for flicker.
- (3) **Speed-preset shader shortcuts in a race** (`REGPU_LEAN_FETCH=2`, `REGPU_PLAIN_MUL_PS`, start-time): cheaper car
  shaders; compare pictures numerically.
- (4) **Overdraw count**: D3D12 pipeline statistics (PS invocations) per draw or pass: if car pixels are shaded several
  times, a depth pre-pass or ordering for car draws could pay.
- (5) **Variable-rate shading for distant cars** (RX 7900 XTX: tier 2): biggest possible saving, most work (no VRS in
  ReGPU yet; choosing "far" draws needs care).
- Also: check the alpha-test-free shaders on other tracks, menus, photo mode, showroom; the next race records its
  variant pipelines (one-time builds), the one after should have none.
**How the session-20 tests ran:** user drove to the CrownJewel race (8th, Scion xD) or a time trial and paused; Claude
restarted the game itself for start-time switches (`Get-Process fm3 | % CloseMainWindow`, then `play.cmd`, env via a
temporary `play.cmd` line) and kept the same game session between the two sides of a comparison.

### Session 19 start-here (kept for reference; its open list is still open)
All work is committed and pushed (`main`, last commit "Session 19 wrap-up"). `play.cmd` is back to its normal state (no
temporary lines); test mods in `out/user/mods` are all switched off (`_lodx2`, `_crowdhalf`). 20 ReGPU patches.
**What session 19 added (details in the block below and the session log "session 19" parts 1-4):**
1. **Speed presets** (launcher Graphics > Performance > "Speed preset" slider; settings.ini `perf_preset=off|turbo|mild|
   potato|low|extreme`; `FM3_PERF_PRESET`): our own versions of a ReGPU "Turbo/Hyper" study of FM2 (patch
   `regpu-turbo-hyper`, table in `src/gpu_host.cpp` `ApplyPerfPreset`). Default **off**. Checked only in menus at
   Extreme (two cull faults fixed there). **Untested in races** (fps and picture per preset).
2. **Vertex arena** (from FM2, **on by default**; patch `regpu-vertex-arena`, write watch in `src/memory.cpp`, the GPU heap
   over its own view of guest memory). Boots, zero-copy kept, verify clean on the title screen; every race today ran with
   it with no visible fault. **Its fps A/B in a race was never done** (`Fm3 "switch REGPU_NO_VERTEX_ARENA 1"`, live from
   the next frame). Off: `vertex_arena=0` / `FM3_NO_VERTEX_ARENA=1`.
3. **ReGPU branch survey** (RESEARCH.md "ReGPU speed work in other projects"): next candidates PGR4's batched occlusion
   resolve, FM2's 4-entry sampler table, texture eviction budget, SMAA, laptop GPU pick, ring-wait sleep.
4. **LOD research toolkit + results** (OPEN-ISSUES **L** has everything): loose-file mods `out/user/mods/<mod>/game/<path>`,
   `tools/analysis/lodsettings.py` (list/mod TrackSettings LOD fields), `tools/analysis/xref.py` (string users, Turn 10's
   270 dev options with offsets in the options object `0x82C21C60`, their readers), `tools/analysis/passes.py` (captures
   compared pass by pass), `Fm3 "peek/poke"` for option bytes, `FM3_WATCHW`/`FM3_WATCHRW` watchpoints.
   Results on the CrownJewel grid (8 cars, 1440p 4x MSAA): `keepLOD0Car` works (full model for the near AI car, +0.6%
   draws); crowd `lod0-3` scale the 3D spectators (x2: +7% draws, ~-10% fps; x0.5: halved, -3% draws, fps same);
   `fastcrowdrender` removes 3D spectators (-7% draws, fps same, flatter crowd); `fastcarrender` lowest models for every
   car (+1.3% fps, ugly); `perfmode` = all of it + fallback UI font without `.'-ë` + broken menu car: no use;
   `trackfardistance`, the dev overlays (`showframerate`, metrics, PVS) and `noattractvideo` are cut from retail.
   **Key finding (corrected in session 20):** the grid frame (~4.9 ms) is **not** bound by the game's render thread t7.
   Its 98% "CPU" is mostly a spin: the session-20 profile (`out/logs/s20_t7_profile.log`, stock, paused CrownJewel grid,
   1440p 4x, 203 fps) has t7 ~65% in D3D's ring-space spin (`sub_824B5048` -> `sub_824B4F78` -> `sub_824AAA68`, waiting
   for ReGPU to consume the ring), ~2% asleep, only ~33% game work. The limit is downstream: ReGPU's draw thread
   (94-98% busy) and/or the GPU (4.72 of 4.90 ms); -3%/-7% draws (crowd cuts) moving nothing points at the GPU. Stock
   itself spread 198.8-206.0 fps over runs (use interleaved rounds for small differences).
   **Race vs time trial (session 20):** the ~100 fps gap (198 vs 311) is GPU time, ~1.3 of 1.47 ms = the seven AI cars'
   material shaders in the main scene (~0.19 ms per car, pixel cost at 1440p 4x, not vertices): OPEN-ISSUES L.
   **New default (session 20): alpha-test-free pixel shaders** (patch `regpu-split-alpha-test`, 21 ReGPU patches):
   race grid 203 -> 213.6 fps (+5%), picture byte-identical. Off: `FM3_ALPHA_KILL_ALWAYS=1`; live A/B
   `Fm3 "switch REGPU_SPLIT_ALPHA_TEST 0|1"`. User drove ~3 laps of CrownJewel with it: nothing wrong seen (first run
   compiles the variants once, then they load from `out/user/shadercache`; since a follow-up their pipelines are in the pipeline manifest too and
   are prebuilt at boot: title/menu check 1517 -> 1555 stored pipelines, all started at boot).
**Open, for the user to pick (ask first):**
- (a) **Stop the attract video** on "Press A to start" (user request; leads in the note below: `ForzaUI::CAttract`,
  `Forza_Intro.bik`, UI flow idle timer).
- (b) **Vertex arena race A/B** (fps, and stale geometry with `REGPU_VERTEX_VERIFY`).
- (c) **Speed presets in a race** (each preset vs Off; reflections, liveries, car pictures, sun flare; FSR 1 with 4x).
- (d) LOD: grass on a track that has grass at the grid (user: not CrownJewel); a launcher option for `keepLOD0Car`
  (full-detail AI cars) and maybe crowd density (data mod); single `fast*` flags other than crowd/car if wanted.
- (e) The older list: C1 watch (DLC patches), band D, alt-tab hang, early-fence write-back bug upstream, wheel test,
  engine sound G (pitch check or hl360 compare), Zonda R decals, vinyl leftovers.
**How the LOD tests ran (reuse):** same race each time (CrownJewel, player 8th in a Scion xD, pause menu open); `Fm3 "status"`
7 samples (perf log off) + `Save-Fm3Shot` + `Fm3 "capture"` saved to `out/logs/s19_capture_<run>.log`, then
`passes.py stock.log run.log`. Data mods need only a race restart; command-line options (`FM3_CMDLINE` as a temporary
`play.cmd` line) need a game restart. Claude restarted the game itself between runs (CloseMainWindow, then `play.cmd`);
the user drove to the grid.

### Session 19 details (kept for reference)
**Session 19: speed presets (launcher slider, 6 stops), modelled on a ReGPU study of FM2's "Turbo/Hyper" modes** (the user
shared its 4-page report; its code is not public, so every piece here is our own implementation from the descriptions).
- ReGPU patch `regpu-turbo-hyper` (switches, all off by default): `REGPU_LEAN_FETCH=2` (a plain 2D texture, flagged on the
  CPU as tinfo.x bit 7, is one sample; the lean path now honours the instruction's filter), `REGPU_LEAN_VFETCH`,
  `REGPU_INDEX_HASH_ONCE` (live), `REGPU_DRAW_CULL=<px>` (live via `Fm3 "env REGPU_DRAW_CULL n"`; per-draw occlusion
  queries + GPU predication, retested every other frame; only depth-writing, depth-tested, non-stencil draws;
  `REGPU_NO_DRAW_CULL` live off), `REGPU_COPIES_1010102` (live), `REGPU_A2M_AS_TEST` (live, 1x MSAA only; =2 always),
  `REGPU_PLAIN_MUL_PS` (pixel shaders only), `REGPU_HELPER_SCALE=<f>` (passes narrower than 1280, not big/tall),
  `REGPU_FREEZE_HELPERS=<n>` (steady narrow 8888/2_10_10_10 colour passes with depth-tested draws; live off
  `REGPU_NO_FREEZE_HELPERS`). The base shader preamble is unchanged: preset Off keeps its shader cache; each preset's
  shaders compile once (~40 s at first boot).
- FM3 side: `settings.ini perf_preset=off|turbo|mild|potato|low|extreme` (`FM3_PERF_PRESET` overrides; log line
  `gpu: performance preset X`), table in `src/gpu_host.cpp` `ApplyPerfPreset`; an env var set by hand wins over the preset.
  Mild/Low/Extreme lower the scene with FSR 1 (quality 1/2/3) when no upscaler is chosen. Launcher: Graphics >
  Performance > "Speed preset" slider.
- **Tested so far (by me, menus only, Extreme at 1440p 4x):** boots, no shader compile errors, FSR 1 at 50%, filtering 0,
  draw cull on. **Found and fixed:** (1) the 64-px cull made FM3's menu letters (I, L, T, Y, whole grey rows) vanish every
  other frame: a draw is retested on alternate frames, so small things flicker; presets use the 0-px cull only (bursts of 6
  shots: text steady at 0 px, lost on 1 in 4 at 64). (2) First cull build keyed menu glyphs (shared buffers) and lost letters
  too: cull now only for depth-writing draws.
- **Not tested yet (needs the user in a race):** fps per preset (paused start grid, `Fm3 "status"` averages; compare with
  Off), the picture per preset (reflections at 25% scale and frozen 2 of 3 frames; liveries; car pictures; the sun flare
  with the cull: draws inside the game's own queries are never culled), FSR 1 + 4x MSAA together, lean fetch v2 / plain
  multiply on track shaders. If a race shows a fault, A/B live: `REGPU_NO_DRAW_CULL`, `REGPU_COPIES_1010102`,
  `REGPU_INDEX_HASH_ONCE`, `REGPU_NO_FREEZE_HELPERS`; the start-time ones (lean fetch v2, lean vfetch, plain multiply,
  helper scale) need a restart with the env var set to 0 (`set REGPU_LEAN_FETCH=0` etc. before `play.cmd`).
- Not done: mesh decimation (the study's own result was inside noise and it bends geometry).
- **Vertex arena (later in session 19, from the FM2 port; on by default):** large vertex buffers live in a 512 MB VRAM
  arena (ReGPU patch `regpu-vertex-arena`), copied again only when the game writes their pages: `src/memory.cpp` write
  watch (pages read-only in the three physical views; the first write faults and frees its 64 KB block; NtReadFile
  releases its buffer first). Over the guest's 0xA0000000 view the GPU heap made VirtualProtect fail (error 87, as on
  FM2's PCs) and the arena switched off: ReGPU now opens the heap over a view of its own (`HeapViewAddress`), so zero-copy
  stays and the guest views can be watched. Boot test: watch arms, `REGPU_VERTEX_VERIFY=1` found no missed write on the
  title screen. **Not yet measured in a race:** live A/B `Fm3 "switch REGPU_NO_VERTEX_ARENA 1"` (from the next frame)
  on a paused grid; watch for stale geometry (tyre marks, damage) with `Fm3 "switch REGPU_VERTEX_VERIFY 1"` (logs
  `changed with no write seen`). Off: settings.ini `vertex_arena=0` or `FM3_NO_VERTEX_ARENA=1`.
- **LOD research toolkit (end of session 19, OPEN-ISSUES L "Research toolkit"):** loose-file mods
  (`out/user/mods/<mod>/game/<path>`), `tools/analysis/lodsettings.py` (per-track LOD fields; makes scaled TrackSettings
  mods), `tools/analysis/xref.py` (string/address users, the 270 developer options with their offsets, their readers),
  the existing `FM3_WATCHW`/`FM3_WATCHRW` watchpoint (validated on `launch`). Found: dev options `keepLOD0Car`,
  `trackfardistance`, `perfmode`/`fast*render`, `nogpuvisibility`, `nopvs`, overlays.
  **Tested in a race:** `keepLOD0Car` works (the adjacent AI car gets its full model; +0.6% draws, -1.3% fps
  on the paused grid): a candidate launcher option. `trackfardistance` is never read (dead). A TrackSettings
  x2 mod costs +7% draws / -10% fps on the grid, all of it crowd (3D spectators doubled; `tools/analysis/passes.py`
  compares captures pass by pass); grass/cubemap/shadow distance: no change from the grid. Next: those while
  driving (OPEN-ISSUES L). Developer overlays: cut from retail (never read). `perfmode`: fallback UI font and
  car bodies missing in the menu: unusable as a whole. Single flags: `fastcrowdrender` -7% draws, 0 fps, flatter crowd;
  `fastcarrender` lowest models for every car, +1.3% fps: no. Crowd x0.5 data mod: 3D spectators halved, -3% draws,
  fps unchanged (GPU-bound): the clean lever for a draw-bound low-end preset. Grass: test on another track.
  `perfmode` in a race: -12% draws, 196 fps, low-LOD cars, no 3D crowd, font without `.'-ë`: no use. Its "slower" is
  mostly run-to-run spread (stock 198.8-206.0 today); the GPU (4.7-4.8 ms of 4.9) and ReGPU's draw thread are
  both near full, so fewer cheap draws save nothing (why no LOD change moved fps here). t7 is mostly spin-waiting on
  ReGPU (session 20 profile), not the limit.
- **To do (user, session 19): no attract video on the title screen.** Left on "Press A to start" for a while, the game
  plays its attract video, which gets in the way of tests (and of the virtual pad). Leads: Turn 10's `noattractvideo`
  option (+2403) is only ever written, never read (cut, like the overlays); the UI class `ForzaUI::CAttract` (RTTI
  string `82BA0551`) runs it; the video is probably `Media/UI/Videos/Splash_Intros/Forza_Intro.bik` again (the only
  intro-style video on the disc). Options: hook CAttract's start (find it from the RTTI/vtable), or a `boot_options.cpp`
  switch that refuses that .bik after boot (check the game then skips cleanly), or the title screen's idle timer in
  the UI flow XML (ui.zip, zip-overlay mod). FM2's port has nothing for it.
- **Other ReGPU speed work found in other branches** (survey, RESEARCH.md "ReGPU speed work in other projects"): the
  biggest is FM2's vertex arena (large vertex buffers in VRAM, recopied on guest writes; FM3 reads them over PCIe).

### Session 18 start-here (kept for reference)
All work is committed and pushed (`main`). Details of everything below: session log "session 18" entries (three of them, 2026-11-09/10) and OPEN-ISSUES.
**Session 18 in short:**
1. **Music fixed (confirmed by the user, menus and races).** Two XMA decoder causes: the codec pool made the music streamer's start loop give up (a context stepped 8x while waiting now opens its codec in place), and the seek guard reset the music codec between its one-packet buffers (music now skips the guard). Switches: `FM3_XMA_POOL_WAIT_ALWAYS=1`, `FM3_XMA_GUARD_MUSIC=1` restore the old behaviour.
2. **DLC database patches now run** (`UnpackDlcPatches`, `imports_xam.cpp`): they never had (the game looks for them as loose files beside each DLC zip). 199 DLC cars get `IsInstalled=1`, car/track merges apply. `FM3_NO_DLC_PATCHES=1` removes them (C1 A/B). **C1 stayed quiet in one race since**: keep watching for `ai drivers:` lines.
3. **One-boot test** (`out/logs/s18-oneboot-prerace-capture.log`): ReGPU's **tile merge gives nothing on FM3** (the race scene surface is refused: "clears its tiles with rectangles"); `REGPU_UPLOAD_STABLE_REUSE` ~+2%; `REGPU_CONSTANTS_IN_RING` −21%. **Band D's edge is exactly row 256 = one 4x MSAA EDRAM tile** (same with merge on/off); capture of the pre-race menu in that log at 115.1 s, not analysed.
4. **Engine sound (G):** with the virtual pad holding clutch + 45% throttle (~5,000 RPM, RSX-S) the user hears a slow tone sweep, **"AAAAEEEEOOOO"**, not dropouts. The seek guard on/off: no audible difference. Trace (`FM3_TRACE_XMA_RESTARTS=1`, live): FM3 feeds most engine streams one packet per input buffer, and 36 of 41 "restarts" came after a single packet (none on a stream's first packets: not FM2-style replayed loops). An experimental **crossfade at every restart** (launcher Settings > Testing > "Engine sound crossfade", `engine_crossfade=1`, live `FM3_XMA_LOOP_CROSSFADE`) engages (~43 per 5 s) but **sounds worse** ("AAAEAEAEEEE...": old and new pieces alternate). It stays off by default; don't spend more on it as is.
5. Also: `FM3_*` LiveSwitches flip live via `Fm3 "env FM3_NAME 1|0"`; GPU adapter + memory log every 30 s; `upscaler=fsr4` runs as FSR 3; `FM3_TRACE_READS=<text>` logs host call stacks; `play.cmd` skips the intro movie.

**First thing next session:** ask the user which topic. Where the engine-sound work stands (the user's last choice was between these two):
- **(G-a) Compare with hl360:** same car (Acura RSX-S), same revs. `boot hl360.cmd`; hl360 has no virtual pad, so the user holds the revs (their controller's trigger is noisy: a keyboard key for throttle might hold steadier). If hl360 sweeps too, it's the game or common to all recomps.
- **(G-b) Check our pitch:** the sweep sounds like two near-identical layers beating. Check how our audio mixer applies each voice's frequency ratio (`src/audio/audio.cpp`, XAudio `SetFrequencyRatio` / the title's pitch writes) and whether two engine contexts (e.g. `A1000500` and `A1000540`, each fed one packet at a time) play at slightly different rates. `FM3_TRACE_AUDIO` (live) and `FM3_DUMP_AUDIO` exist.
- **Virtual pad recipe for steady revs:** `Fm3 "hold LB"` (clutch), `Fm3 "trigger R 0.45"` (~5,000 RPM on the RSX-S; 0.25 idles, 0.35 ~3,000, 0.55 limiter), `Fm3 "release all"` afterwards. The user may ask for this; telemetry page (d-pad) shows RPM.

Then the ranked list: (a) C1 (watch with the DLC patches in; A/B `FM3_NO_DLC_PATCHES=1` if it returns or never does), (b) band D from the saved capture, (c) alt-tab hang, (d) early-fence write-back bug upstream, (e) wheel test, (f) LOD distances (L); RESEARCH.md items (GPU choice in the launcher, iGPU defaults, shader precompilation, ultrawide); the Zonda R decal lead; vinyl leftovers.

### Session 17 start-here (kept for reference)
**Session 17 added: intro skip, "start in a quick race", Disc 2 install, FM3's developer command line.** Tested by me with the virtual pad (you allowed me to restart the game for these tests); **not yet played by you**.
1. **Intro skip** (`src/hooks/boot_options.cpp`; settings.ini `skip_intro=1`, launcher Play tab "Skip intro", `FM3_SKIP_INTRO=1`). Title screen ~8 s after start instead of ~100 s. `sub_8225A870` builds the boot sequence (8 entries × 240 bytes at r1+80, +160 = duration double; the +172 file field is not plain text): disclaimer 6 s, ESRB 4 s, Turn 10 10 s, MSGS 10 s, Dolby 5 s, Forza intro 70 s, handed to the sequencer's setter `sub_822BD7B8` by a virtual call (`(*(0x82C2111C)+472)->vtable[88]`). The sequencer exists only once the sequence is being built, so the hook on `sub_822232C8` (string copy) wraps the setter's indirect-table entry when the Forza intro's path (`0x82007BF8`) is copied; the wrapper recognises the boot sequence by its timings (10, 10, 5, 70 s) and sets 0.05 s per entry (FM2's method). Log: `intro: sequencer setter 822BD7B8 wrapped`, `intro: skipped (legal text, logos, intro video): 6 s 4 s 10 s 10 s 5 s 70 s`. (`FM3_INTRO_LOG` was a temporary debug switch, removed.) The game's own `disablesplashintro`/`noattractvideo` options have no reader in retail.
2. **Start in a quick race** (settings.ini `quick_race=1`, launcher "Start in a quick race", `FM3_QUICK_RACE=1`; implies the intro skip). Feeds the existing pad-script tour (`FM3_TOUR` in `imports_xam.cpp`, FM2's approach) the route title > START > FREE PLAY > QUICK RACE > manufacturer > car > track > assists & difficulty > race (timed presses with generous gaps). Twice tested: in the race (Catalunya pre-race camera) ~55 s after launch. The car is the first one (Acura), the track the game's pick. `FM3_TOUR` set explicitly overrides the route. Risks: a slow PC or a different profile state (e.g. a first-time profile with extra prompts) can put presses out of step; pressing buttons during the route too.
3. **Developer command line** (`FM3_CMDLINE` or settings.ini `cmdline=`). Retail FM3 still parses Turn 10's dev command line (≈180 options, strings at `82008400`–`82009400`; handler `sub_8225EF78`, options object `0x82C21C60`), but `sub_826F5D50` empties the text; the hook writes ours (log `cmdline: "…"`). Race options: `launch` (+1082), `track=<Tracks.id>` (0 = Laguna Seca; or `trackname=<MediaName> ribbon=<RibbonIndex>`), `car=<Data_Car.Id>`, `numaicars`, `aicar<N>`, `numlaps`, `startpos`, `aiskill`, `exhibition`, `freerun`, `shortrace`, `ShowPreRaceAnd321InCmdlineLaunch`. **`launch` does NOT work in retail:** it skips the front end and shows the right track's loading screen (track=0 → "Mazda Raceway Laguna Seca, Full Circuit"), then waits forever. Not a deadlock (two thread dumps: main loop `sub_821F5DE8` runs, workers idle), no file reads after the loading animation, same with no car, `exhibition`, `freerun`, START/A pressed. Something only a dev build provided is missing. Readers of `launch` (via options getter `sub_82210D10`): `sub_8231AA50`, `sub_8231B910`, `sub_8231C010`, `sub_823B29D0`, `sub_823D5600`, `sub_82238620`, `sub_8225AB70`, `sub_8225CDE0` (boot). Left as is (quick_race covers the need); a lead if someone wants a true direct launch.
4. **Disc 2** (subagent research, then built): launcher `Stfs.cs`/`XIso.cs` (from the FM4 fork, reading packages in place inside the ISO) + `Disc2.cs` (Play tab tile "Disc 2 content": install from .iso / folder; CLI `FM3Launcher --install-disc2 <src>`; `install-disc2.cmd` picks the Disc 2 ISO in the project root). Installed to `out\user\dlc`: all 8 packages (Asian, European, American, Ultimate 1–3 car packs, Ultimate Track Pack, Road Racing Track Pack), **byte-identical to hl360's install**, licence masks 0. Runtime (`imports_xam.cpp`): the type-2 enumerator lists only packages with `.license` (written last = finished install, Q13), names cut at the first unprintable byte (Q14, `ContentFileName`), `FM3_DLC_LICENSE_MASK=<hex>` overrides the masks (Q12; hl360 grants FFFFFFFF). **In game:** after START at the title, `content: enumerating 8 packages of type 00000002` and all 8 `DLCRootN:` mounts, then the profile; no errors; menus and Quick Race fine. **User test (end of session 17): Fujimi Kaido (Disc 2, pack 0060) loaded and raced fine, more events showed in career mode;** log clean. **Disc 2 car: 2010 Pagani Zonda R (pack 0035, model only on Disc 2) drives fine (user); its decals looked maybe lower resolution than expected: unconfirmed, no reference yet. Next: same car and view in hl360 (`boot hl360.cmd`, same 8 packages) vs ours; if hl360 is as soft it is the content.** Not yet checked: the Nordschleife. Disc 1 already has Amalfi (`Tracks\AmalfiGP`); Disc 2 adds `Tracks\Amalfi` (other layouts?), Fujimi Kaido, Nürburgring Nordschleife (pack 0060) and Nürburgring GP, Test Track (0045). Test those first. Save backup before the first Disc 2 boot: `out/profile-backups/session17-pre-disc2`. Open: `XamSwapDisc` still a stub (called once at 342 s in a session-16 run, lr `8258C794`).
   - **C1 note (subagent):** without Disc 2 no DB patch file ran on our build, so the patch system can't have caused C1 so far; Disc 2's `0045_pri_65.zip` has `4069_merge.slt` (may touch AIPlayers): compare C1 with and without Disc 2 from now on.
5. **Tools:** `Fm3 "env FM3_DUMP_THREADS <n>"` logs every thread's host stack (any new value = one dump; `LogAllHostThreadStacks`). `FM3_TRACE_FILES=1` + `FM3_TRACE_XAM=1` show file opens / XAM calls (the XAM trace hits its 300-line limit within ~2 s on the per-frame polls).
6. **Seen again, not new:** OPEN-ISSUES D (lighter band over the top of the 3D view) on the pre-race camera at Amalfi and Catalunya.

**First thing next session:** ask the user which topic. Open from session 17: the Zonda R decal-resolution lead (same car/view in hl360 vs ours), quick_race on the user's normal settings, the Nordschleife. Then the ranked list: (a) C1 (now with and without Disc 2: pack 0045's `4069_merge.slt`), (b) alt-tab hang, (c) early-fence write-back bug upstream, (d) wheel test, (e) LOD distances (L); launcher game import/photos, vinyl leftovers, OPEN-ISSUES D/F/G. README.md was updated for session 17 (Disc 2, skip intro, quick race).

### Session 16 start-here (kept for reference)
All work is committed and pushed (`main`). **Session 16 fixed three bugs, all confirmed in your play test ("no issues so far")**, with 3 new ReGPU patches (18 now):
1. **Livery broken on some race loads** (everything but one section, or just the front bumper; fine on the next load). Cause: each race load builds a car's livery in a **new image a few KB above the previous load's**; ReGPU's enclosing-image search took the matching image at the **lowest** base, i.e. the old one, so the new livery's pieces went there and our write-back safety check (rightly) dropped them. Fix (patch `regpu-livery-enclosing-recent`): images not used in the last 120 frames are left out (`REGPU_ENCLOSING_LOWEST=1` = old). The same patch makes the safety check compare texel by texel inside the write's own rectangle and log where a change was (`REGPU_WRITEBACK_REUSE_SPAN=1` = old). Evidence: `out/logs/session16-run1.log` (skips at 121.6 / 365.9 / 368.8 / 754.8 / 1095.6 s all into images that were liveries of earlier loads).
2. **OPEN-ISSUES E, other cars' shade flickering on your car** (1 frame in 5 at 360 fps, measurable while paused). Cause: every frame FM3 draws the track's baked shade around your car top-down into a 256x256 image (`159D0000` that run) that the paint (`ps e41e183a423e6f1b`, slot 10) samples, then one AI car's 64x64 map (a 5-slot ring) in the same EDRAM. It clears the 256x256 area with a 16-pixel 1x strip plus a **120x128 rectangle through a 4x MSAA view** (pitch 160): ReGPU scaled such clears only for depth-only clears, so 3/4 of the area kept the previous AI car's shade. Fix (patch `regpu-msaa-color-clear-scale`, `REGPU_NO_MSAA_COLOR_SCALE=1` = off): colour rectangles through an MSAA view of a small 1x surface are scaled too, only when pitch x samples across < 1000 (my first version also caught the 1280-wide scene's clears through a 4x view and whitened a quarter of the screen: you saw it, fixed). Measured: 20 consecutive paused frames identical with the fix, flicker back with it off.
3. **Debug tooling:** after any capture, every debug-server `shot` also dumped all ~3,500 resolved images (~30 s stall each). Patch `regpu-debug-shot-dump`. Use `Fm3 "shots N dir raw"` (640x360 consecutive frames) for flicker work: `tools`-style scripts in this session's scratchpad read them (box luma per frame, changed-cell map); worth adding to `tools/analysis/` if used again.

**If you see something like these again** (a livery part missing after a reload, shade or shadow blocks flickering on a car, white blocks after an MSAA-related change): the switches above A/B each fix live; grep the log for `memory changed since the resolve` (now with `changed texels inside/outside`) and `fits N images` (`left out as old`).

**C1 (driver 125) progress:** the split diagnostic worked (354.6 s, run 4): in the in-memory database row 125 has **`Name` NULL** (typeof null, `quote` NULL), `LastName` empty, `isUsed` 0, rowid 125, while the disc has `_&2149404878`. So something writes NULLs into that row during play (the workaround still gives the car a generic driver; no crash). No `UPDATE AIPlayers SET Name` exists in the xex; **lead:** the game's DB patch system (strings near `82007F00`: `ALTER TABLE AIPlayers ADD COLUMN isUsed ...`, `%s:\Media\LicenseMasks\%08x.sql`, `ERROR: DB Patch: Duplicate priority`, modes `overwrite` / `merge` / `exec`): a DLC / licence-mask SQL patch may overwrite AIPlayers rows. In one earlier load the standings showed C. Reed normally, so it depends on load/state. **Next:** find who calls the DB patch code (string xrefs), log each patch file applied and its SQL, and check `Media\LicenseMasks` / DLC `.sql` files for AIPlayers 125.

**First thing next session:** ask the user. Ranked: (a) C1 via the DB patch lead; (b) alt-tab hang (`out/logs/session15-c1-run3.log` thread dump); (c) report the early-fence write-back bug upstream; (d) wheel test; (e) LOD distances (L); launcher features, vinyl leftovers, OPEN-ISSUES D/F/G.

### Session 15 start-here (kept for reference; (a) livery solved in session 16)
All work is committed and pushed (`main`). **Session 15 in short:** (1) the sun flare now works with the draw workers on the draw thread (one-line ReGPU fix in patch 15), but that mode is slower than the draw thread alone (720p 1x: 195 vs 216 fps), so it stays opt-in: launcher **Settings → Testing → "Draw workers"** (`draw_workers=1` → `FM3_DRAW_THREAD_PIECES=1`); your racing A/B at 1440p 4x MSAA showed little difference (GPU-bound). (2) **Crash C1 workaround** (`src/hooks/ai_drivers.cpp`): driver 125 (C. Reed) sometimes reads back an empty Name; such a car now gets a generic driver. Cause open. (3) Two new leads from your play test: **livery breaking on some loads** and an **alt-tab hang**.

**First thing next session:**
1. **Ask the user which topic.** Ranked: (a) **livery breaking on some loads**; (b) C1's cause; (c) the alt-tab hang; then upstream bug reports, wheel test, LOD distances (L), launcher features, vinyl leftovers, OPEN-ISSUES D/E/F/G.
2. **(a) Livery breaking, how to start:** you saw the vinyls broken everywhere but the rear bumper (Montserrat, Road America, Tsukuba) or only the front bumper (Maple Valley), fixed again on the next load. The log `out/logs/session15-c1-run3.log` has `writeback: 06A55000 2048x2048 rows 288-552: the memory changed since the resolve (1 frames ago, ...): not written` (1110.8 s) and four lines for `07B96000 1024x1024` (1122.2 s): our write-back safety check (ReGPU patch `regpu-writeback-safety`, VINYLISSUES) dropped livery pieces because guest memory changed between the resolve and the write-back. Steps: `grep -c "memory changed since the resolve"` per race load in a fresh run; compare the count with `FM3_NO_DRAW_THREAD=1` (temporary `play.cmd` line) vs the default, since the draw thread (session 14) changes when resolves run relative to the CPU; read the safety check's rule in the patch and VINYLISSUES (why it skips, what "changed" means: the game's own CPU writes, or our previous write-back?). Ask the user to report the timer value of a broken livery.
3. **(b) C1:** play normally; when the log shows `ai drivers: car's driver id N has an empty Name`, the next line `ai drivers: driver N row: ...` says what SQLite holds (typeof/length/hex of Name, LastName, isUsed). Then `Fm3 "find _&2149404878"` (125's Name key) + `peek` that page and decode it with `tools/analysis/sqlpage.py` (page 17 = file offset `0x4400`). Also confirm in play that the final workaround shows no duplicate or odd driver names.
4. **(c) Alt-tab hang:** the run ended in a thread dump at 1225 s (`out/logs/session15-c1-run3.log`; Windows screenshot tools had stopped working before). Read which threads wait on what; reproduce by alt-tabbing in a race.

### Session 15 details: sun flare with the workers on the draw thread fixed; the serial draw thread stays the default (faster)
**Cause:** in ReGPU's ZPD packet handler the command processor ran `if (end) seg::g_query = false;` also in draw-thread mode. `seg::g_query` (the counting query's draws go straight onto the draw thread's list) belongs to the draw thread, and the command processor runs ahead of it, so the flag was often cleared before the draw thread reached the sun's draws: they went to worker lists the D3D query does not count → 0 samples → no flare. The serial draw thread never reads the flag, so it worked. **Fix:** cleared there only without the draw thread (its `kQuery` end record clears it on the draw thread), folded into patch 15 `regpu-draw-thread-pieces`. **Test** (you, Montserrat sun spot, paused, 1440p 4x MSAA, pieces on): query answers zpass 3214 / total 3214 every frame, ~257 fps (6 samples), **flare looks right** (you). **A/B (same cars grid, paused, 1280x720 MSAA 1x, perf log off, cap 360, 7 samples each, screenshots identical):** draw thread + workers **~195 fps** (187–200), serial draw thread **~216** (212–218). Session 14's "+15% with pieces" was against the older workers-on-CP mode (175); the serial draw thread was never measured at 720p. So **the default stays the serial draw thread** (`src/gpu_host.cpp`); `FM3_DRAW_THREAD_PIECES=1` = with workers (flare now correct), `FM3_NO_DRAW_THREAD=1` = workers on the command processor. Why the workers lose on the draw thread is not known (a piece waits for the replay, `SplitMainList`; the draw thread alone may already keep up with the CP). **Your settings.ini had `perf_log=1`** (costs ~14% fps); restored as it was. **Testing note:** no race start grid shows the sun flare; flare tests need a sun-facing spot reached by driving (the Montserrat spot). Grids are fine for fps A/B.

**Crash C1 (session 15): workaround in, cause open.** Always driver 125 (C. Reed): its row exists, but its Name read back empty on some loads (on disc `_&2149404878`); the in-memory database was intact at the menu and between loads. New `src/hooks/ai_drivers.cpp`: such a car gets a generic driver, an empty name that still gets through gets a stand-in, an empty generic pool is reset; the next occurrence logs what SQLite holds (OPEN-ISSUES C1). In your long test run the stand-in caught driver 125's empty name on 9 loads with no crash (the run ended in the alt-tab hang below); the final version (generic driver instead) is built but not yet tested in play.

**New leads from your session-15 play (not worked on):** (1) **livery breaking on some loads** (everywhere but the rear bumper, or just the front bumper; fine again on the next track): the log has `writeback: 06A55000 2048x2048 rows 288-552: the memory changed since the resolve (1 frames ago): not written` (1110.8 s) and four such lines for `07B96000 1024x1024` (1122.2 s): our write-back safety check skipped livery pieces. Check whether this is new with the draw thread (session 14) by counting these lines per load with `FM3_NO_DRAW_THREAD=1` vs default (VINYLISSUES). (2) **Alt-tab hang**: the game hung (thread dump at 1225 s in `out/logs/session15-c1-run3.log`) when you alt-tabbed out, and Windows screenshot tools stopped working before that.

**Tools added:** `tools/analysis/sqlpage.py` (decode a gamedb.slt page from the file and from a guest-memory hex dump; page snapshots of this session in `out/logs/session15-dbpages/`), the debug server's `Fm3 "find TEXT"` (guest addresses of a string) was the way to find the in-memory database.

### Session 14 state (kept for reference; its flare-with-pieces lead is solved in session 15)
All work is committed and pushed (`main`). **Session 14 in short:** (1) the sun flare works with ReGPU's draw workers (patch 13 `regpu-worker-queries`; launcher "Accurate sun flare" option removed); (2) the sun glare was far too big: FM3 sizes it from the raw sample count, now scaled by `REGPU_ZPD_SCALE=0.6` (patch 14, matched to console footage by eye); (3) the command processor was the speed limit: **ReGPU's draw thread is the default now** (patch 15 `regpu-draw-thread-pieces`; 1440p 4x MSAA cars grid 175 → 181–184, Montserrat 225 → 240, now GPU-bound), with the workers on the draw thread opt-in only (`FM3_DRAW_THREAD_PIECES=1`, +15% at 720p but **no sun flare**: the open lead); (4) crash **C1** (OPEN-ISSUES) on some race loads; (5) your cap is 360. Sections below in that order; the next-topic list is after the performance paragraph.

**How it works:** a D3D12 occlusion query only counts the draws of its own command list, and the workers record into their own lists, so with workers every query answered a fixed 1000 (no flare). Now a ZPD start flushes the open logged segment and, while the query **counts** (`RendererOcclusionCounting`, between a start and its tile-span pause or end from `regpu-zpd-tile-spans`), its draws go straight to the command processor's list (`seg::g_query` in `gpu.cpp`); between FM3's two tile spans and outside queries the workers go on. Worker draws never take a coverage draw of the command processor's query (`!C().worker`). PGR4's logged queries (3cbd0f4) were not ported: they record a query into a worker's piece, and FM3's query spans both tiles (pieces would split it, or the frame would stay one piece). Undo live: `Fm3 "switch REGPU_NO_WORKER_QUERIES 1"` (fixed answer).

**Measured** (paused on a race start grid, 2560×1440 4x MSAA, `max_fps` 240, cars already gone): real queries + workers **225 fps**, workers off (old "accurate sun") 219, fixed answer + workers 232: the queries cost ~3%. You confirmed the flare is back and looks the same with workers on or off. The sun query answers zpass 5,924 / total 5,924 (fully visible, like session 8b's 5,780). Log: `out/logs/session14-worker-queries.log`.

**Sun glare size (fixed by eye, session 14):** at 1x the glare covered half the screen at the start grid, far bigger than console footage (YouTube `Fx4sKnongdM` 1:16–1:21, Camino Viejo de Montserrat). Ruled out: a double count (capture, `out/logs/session14-sun-capture.log`: the sun quad is drawn once per EDRAM tile, each clipped to its tile; 22,729 host = ~2,840 guest samples there), resolution scaling (you: same at 720p 1x and 1440p 4x), and hl360 as a reference (no flare at all). **Found:** FM3 sizes the glare from the query's raw sample count, not the visible share (our answer is always zpass = total, yet the glare follows the count): with the new live multiplier `REGPU_ZPD_SCALE` (ReGPU patch 14 `regpu-zpd-scale`), frame brightness on the paused grid was 43–44 (no glare) at ≤0.5, 45 / 48 / 54 / 62 at 0.6–0.9, 72 at 1 (5,341 samples), 83 / 99 / 119 / 129 at 1.1 / 1.25 / 1.5 / 2. Half the count (pixels instead of 2x samples) would mean no glare at all, which the console footage contradicts. You compared live: 0.75 still too big, 0.65 slightly too big, **0.6 about right** → FM3 default `REGPU_ZPD_SCALE=0.6` in `src/gpu_host.cpp` (env overrides). Why the console counts fewer samples is unknown (Hi-Z? the sun quad's edges?); a later lead if other occlusion effects look off. Also seen in FH1R `docs/framerate.md`: its sun-glare fade (`sub_8257E5B8`) steps 1/30 per rendered frame, so at high fps the glare fades in/out faster (not checked for FM3).

**Crash once on loading a track** (superseded by OPEN-ISSUES C1, which has the stack; timer ~36, `fm3.log` of that run lost to the next launch): the guest threw `std::out_of_range` (ThrowInfo `8219A290`, thrower near `82A78BE8`, lr `82A78C58`); our runtime can't unwind guest C++ exceptions. Did not repeat on the next load. `src/kernel/imports_rtl.cpp` now logs the guest stack (`guest exception: stack ...`) on such a throw; the save before the retry is backed up in `out/profile-backups/session14-after-crash`.

**Performance: the draw thread is FM3's default now (session 14, "per-draw CP cost").** Profiling (paused on the cars grid, 3,560 draws; logs `out/logs/session14-*profile*.log`): the game's render thread spends ~74% waiting for ring space, so ReGPU's command processor was the bound; its time was spread thin (no hotspot); splitting segments into more pieces changed nothing; **`REGPU_PERF` itself costs ~14%** (151 vs 175 fps: keep the launcher's `perf_log` off except for measuring). ReGPU's draw thread (`REGPU_DRAW_THREAD`: the CP only parses and queues, one thread translates) beat the workers: cars grid 1440p 4x MSAA 175 → 181–184 fps, Montserrat spot 225 → 240 (the cap). New 15th ReGPU patch **`regpu-draw-thread-pieces`**: occlusion queries with the draw thread (answered on the CP from earlier frames, recorded on the draw thread in stream order with our tile spans: sun flare works, you confirmed) and the parallel segments on the draw thread (big segments to the workers while the CP parses: 720p 1x 175 → 202 fps). **But with the workers on the draw thread the sun flare was gone in your race (cause not found)**, so the default is the draw thread alone (`REGPU_DRAW_THREAD_SERIAL=1` set by `src/gpu_host.cpp`); `FM3_DRAW_THREAD_PIECES=1` = with workers (no flare), `FM3_NO_DRAW_THREAD=1` = workers on the CP as before. At 1440p 4x MSAA the GPU is now ~90%+ busy (your overlay), so the CPU side is no longer the limit there. Not yet measured: the serial draw thread at 720p. With the draw thread ReGPU's tile merge (dropping the second EDRAM tile's draws) is off; nothing looked wrong in your race.

**Your frame cap is now 360** (`max_fps=360` in `out/user/settings.ini`, your call: not faster than the game's internal tick rate; 360 is also the maximum `src/settings.cpp` and the launcher allow). Keep it there for tests.

**Crash C1 (OPEN-ISSUES):** some race loads (twice on Montserrat) throw `std::out_of_range` in the game's AI-driver setup (an `AIPlayers` Id string shorter than its 2-character prefix); not caused by today's changes; a retry loads. Guest stack logging added (`src/kernel/imports_rtl.cpp`).

**Pick the next topic with you:** the flare with workers on the draw thread (then make pieces the default: +15% where the CPU limits), crash C1 (log the short string, check `isUsed` running out), upstream bug report (early-fence write-backs), wheel test, LOD distances (L), launcher features, vinyl leftovers, OPEN-ISSUES D/E/F/G.

### Session 13 state (kept for reference; the flare trade-off below is solved in session 14)
All work is committed and pushed (`main`, last `4e63e41`). **Session 13** did three things: racing-wheel support, a frame-rate unlock, and a performance pass that took the game from ~70 to ~150 fps near other cars (1440p 4x MSAA, RX 7900 XTX). Details below and in **OPEN-ISSUES.md issue P**; the step-by-step record is in the session-13 log entry.

**1. Racing wheels (ported, not yet driven with a wheel).** From your FM4 fork's `wheel-support` branch (`88c12e9`): `src/input/wheel/*`, `src/kernel/imports_ff.cpp`, XAM input changes, `fm3.exe --wheel-probe`, launcher **Wheel stuff** tab, `tools/wheelprobe`, `docs/WHEELS.md`, `NOTICE-wheel.md` (FH1R BSD-3 parts). FM3 has FM4's XInputFF library: `sub_82AB6AE0` looks up xboxkrnl 0x282–0x289 and xam 0x1BA/0x208 at boot, and every lookup resolves (log `XexGetProcedureAddress ...`). **To do:** a test with a real wheel (bindings, force feedback; log `Wheel:` lines).

**2. Frame-rate unlock (working, you confirmed smooth).** `src/hooks/frame_time.cpp` + `src/gpu_host.cpp` (own code after FH1-ReImagined's `docs/framerate.md`; FH1R is GPL-3). FM3's main loop `sub_821F5DE8` runs once per guest vblank (callback `sub_822580C8`) and takes dt from the ms tick. At `max_fps` 60 nothing changes. At other caps: guest vblank = 2× cap and the vblank hook sets `mainthread30fps` (options `0x82C21C60`+1086 = `0x82C2209E`) so the main loop runs at the cap and the render thread draws every frame (divider 1 gave 120 simulated / 60 drawn); the main loop's call of `sub_821F6AF8` (lr `0x821F6004`) answers with dt from the vblank clock (120 fps: 8.332 ms, sd 0.002). Switches: `FM3_VBLANK_DIVIDER=1`, `FM3_VBLANK_HZ`, env (live via `Fm3 "env NAME 1"`) `FM3_GAME_FRAME_TIME`, `FM3_FRAMETIME_LOG`. **Your settings.ini: `max_fps=240`, vsync off**, 1440p, 4x MSAA. Not ported (no need seen): FH1's render sync, side-effect fixes, PGR4's menu hold delay / gear latch.

**3. Performance (new defaults in `src/gpu_host.cpp`, each with an undo switch).** The race was bound by turn-taking between the game's render thread, ReGPU's command processor and the host GPU, not by any saturated core:
| Default | Effect (measured) | Undo |
|---|---|---|
| `REGPU_NO_FENCE_ORDER` (set via `regpu::driver::SetSwitch`) | near cars 69–77 → 100–120 fps | `FM3_FENCE_ORDER=1` |
| `REGPU_TEXTURE_CHECK_STAGGER` + `REGPU_DIRECT_DYNAMIC_INDICES` | paused grid 108 → 120 | `FM3_NO_DRAW_SHORTCUTS=1` |
| `REGPU_EARLY_FENCES` + **our 12th ReGPU patch `regpu-early-fence-writebacks`** (ReGPU's `WriteBack` zeroed the fence that must wait for a deferred write-back: liveries came out noise/blank) | paused grid 117 → 147–153; liveries intact | `FM3_LATE_FENCES=1`; old ReGPU behaviour `REGPU_EARLY_FENCES_SKIP_WRITEBACKS=1` |
| `REGPU_DRAW_WORKERS=3` (parallel draw translation) | behind an AI car 142 → 155 | `accurate_sun=1` in settings.ini = launcher Graphics **"Accurate sun flare"** (tooltip: ~10% fps near cars) |
**Known trade-off:** with draw workers our ReGPU can't count occlusion queries (each answers 1000 → FM3 shows **no sun flare/glare**). You chose speed over the flare for now.
Tried and not used: `REGPU_DRAW_THREAD` (marginal), `REGPU_UPLOAD_SAMPLED`, `REGPU_DYNAMIC_EXPIRE` (no effect). 720p 1x ran as fast as 1440p 4x before early fences: not GPU-bound.
**Now:** the command processor thread is the bound (t9 ~99%); fps drops as an AI car comes close (its detailed model = more draws). ~200+ fps when no cars are in view.

**Pick the next topic with you** (options, roughly by value):
- **Sun flare + speed together:** port PGR4's logged occlusion queries (`reference/sibling-recomps/PGR4-ReGPU/thirdparty/ReGPU`, commit 3cbd0f4 in `zer0ducksgiven/PGR4-ReGPU`) so queries work with draw workers; it touches the same functions as our `regpu-zpd-tile-spans` patch (redo the tile-span pause/resume on per-thread query state). Then the "Accurate sun flare" option can go.
- **Per-draw cost on the command processor:** biggest buckets in the perf log are constants (~1.8 ms/frame) and submit (~1.4); ideas in OPEN-ISSUES P (FH1R's CP: reuse unchanged render-target setup, skip NOP padding, avoid per-draw locked atomics; PGO build of ReGPU +6%; `-march=x86-64-v3` for the recompiled code). `Config::lowPerformance` (constants straight into the ring) untested.
- **Report upstream:** the early-fence write-back bug (`gpu.cpp` `WriteBack`) is in upstream ReGPU, FH1's fork and PGR4 too.
- **Wheel test** with a real wheel.
- **LOD / render distances** (OPEN-ISSUES **L**: spectator LOD popping, FH1R's "World detail beyond the console").
- Earlier topics still open: launcher features (import, DLC, photos, icon), high-resolution parity, vinyl leftovers (VINYLISSUES.md), OPEN-ISSUES D/E/F/G.

**Testing aids learned in session 13:** pause on a race's **start grid** for stable A/B numbers (draws stay constant); start with `launcher.cmd` when you need the perf log (`perf_log=1` → `REGPU_PERF`), or add temporary `set` lines to `play.cmd` (remove after). The debug server's `switch` only accepts `REGPU_*`/`PGR4_*` names (our `FM3_*` LiveSwitches are a separate class it can't reach). `REGPU_EARLY_FENCES` is also read once at start in `gpu.cpp`, so its live switch alone is not a real A/B. Profile a guest thread with `FM3_SAMPLE_THREAD=<id>` + `FM3_SAMPLE_CALLERS=1` (at start); t7 = render thread (entry `822BDE68`), t9 = ReGPU command processor. D3D's block-on-fence: `sub_824B5048` → `sub_824B4F78` → spin `sub_824AAA68`; device `*(0x400005D0)`, +0x2A90 → GPU fence word, +0x2A9C issued fence number. Logs of each test: `out/logs/session13-*.log`.

### Session 12 state (kept for reference)
All work is committed and pushed (`main`). **Session 12** added the launcher (`launcher/FM3Launcher`, C# .NET 9 / Photino, ported from your FM4 fork; you tested it: works) and, from testing at 2560×1440, fixed the My Cars thumbnail ghost (11th ReGPU patch `regpu-guest-lod-short-mips`, you confirmed). Higher resolutions and 4x MSAA now run; a one-off race dip with 1440p + 4x MSAA did not reproduce. **How you test now:** `play.cmd` (unchanged) or `launcher.cmd` (same folders; profile from the Profiles tab; graphics settings in `out\user\settings.ini` apply to both). The launcher keeps the previous `fm3.log` as `fm3.prev.log`.

**Pick the next topic with you** (no default chosen at the end of session 12):
- **Launcher, next features** (Phase 6 in the work plan): game import (disc .iso / drop, the fork's `Import.cs`/`XIso.cs`/`DropTarget.cs`), DLC packs (`Stfs.cs`, the runtime already reads `user\dlc`), photos (check FM3's photo-mode save format first), an icon of our own (FM4's is game art), maybe the updater/reports later (need GitHub releases / a server). FSR 4, SMAA and the shader optimiser need a ReGPU update first (our pin lacks them; 11 patches to carry over).
- **High-resolution parity:** play more screens at 1440p and 4x MSAA (the thumbnail bug only showed above 1×; similar "the game relies on its 720p size" cases may exist). If the race dip returns, note the timer value and keep the log.
- **Vinyl leftovers** (below) and the **other open issues** (OPEN-ISSUES.md).

Session 11 fixed the vinyl issues **B** (cut along a straight line) and **B2** (race/photo-mode livery noise), my own **poster bug** (a bumper vinyl in a career poster), and **H** (a car vanishing from My Cars after painting another; the damaged save then crashed on buying a car). **Everything vinyl/livery-related, with how FM3 builds a livery, the fixes, switches, saves, test tools and the remaining leads, is in [VINYLISSUES.md](VINYLISSUES.md).**

**Next (vinyls, when you pick it up):** (1) confirm the final build in normal play: paint several cars, buy one, check My Cars keeps every car (the last part of the H fix wasn't played yet); (2) small uncovered spots at the edges of big vinyl regions (Scion right side by the rear wheel arch and rear door window; Fiat similar + a chunk of the rear wing; Supra rear bumper both sides); (3) Honda CRX vinyls invisible (seen once with the poster bug; re-check). Steps and leads in VINYLISSUES.md "Where to continue".

**Profile:** `play.cmd` now starts profile **`Baseline`** (a copy of hl360's Baseline save, XUID `E0000BA5E11E0001`); the old `Player` save is damaged (buying crashes, in hl360 too). Backups of every test state are in `out/profile-backups/` (`rewardcar` = clean restart point). Only swap profiles with the game closed: it saves on exit.

**Other open items:** see OPEN-ISSUES.md (D photo-mode/pre-race band, E shadow flicker, F foliage dither, G engine audio cutouts with a strong lead, Phase 4/5 items).

**Session 9 summary:** issues A (matte paint/black chrome, k_2_10_10_10 decode) and C (My Cars pictures) **fixed**, B (vinyls) partly fixed, a race-load crash fixed, new B2 (race livery noise). An on-screen timer (top-left, the log clock to 0.1 s; ReGPU patch `regpu-timer-overlay`, `REGPU_TIMER`, FM3 default on, `FM3_NO_TIMER=1` hides it) lets your screenshots point into the log. You then explored beyond the intro race (career menus, upgrade shop, paint/vinyl editors, a solo race, photo mode). Log of that run kept as `out/logs/session9-explore.log` (times below are its `[time]`/the timer). **New issue list (not fixed yet; FM2/FM4 comparison reported to you, waiting for your go):**

| # | Issue (timer) | Our log / first lead | FM2 / FM4 port |
|---|---|---|---|
| A ✅ | Out-of-race 3D (showroom 66.6, career-menu car 576.1, paint booth 1382.1): paint dull/matte, chrome (badges, chrome rims 655.4) **matte black** | **Fixed (session 9, you confirmed):** the showroom reflection cube (`19781000`, 256×256, mipped, read from memory) is k_2_10_10_10, which ReGPU couldn't decode → black cube. ReGPU patch `regpu-cube-2101010` | FM2: same open bug ("paint turns matte, loses reflections" in garage); null cube SRV reads black. FM4 ReGPU: cube keep/stale/mips fixes; upstream has `REGPU_NO_CUBE_KEEP`, `REGPU_NO_LIVE_CUBE`, `REGPU_DUMP_CUBES` |
| B ✅ | Vinyls/decals invisible when applying (1509.2) and in the vinyl editor (1567.6), sometimes partly visible | **Fixed (sessions 9–11, you confirmed):** regions drawn into the car texture (patch `regpu-resolve-into-enclosing`, S9); the straight-line cut (S11): the big vinyl regions' side views are 4×MSAA passes resolved in 224-row tiles, and only the first tile reached its image (big-pass image width ≠ copy pitch; last tile 8-row-rounded past the image): patch `regpu-enclosing-big-pass`. Two small gaps left on the right side ("Start here") | FM2 fixed its vinyls in its own renderer (tall/big passes, foreign-MSAA stencil clears); not the cause here |
| (B, session-start notes) | `resolve … (352x131) stale` at 1483; editor uses big EDRAM surfaces (1368: bases up to B40000, 1539: 1400000) | **FM2 fixed it** in its own renderer: tall 1024-row / 2048 "big pass" livery targets, stencil clears through foreign-MSAA views rescaled, tile shift for foreign views (`FM2_NO_TALL_PASS`, `_BIG_PASS`, `_FOREIGN_MSAA`, `_MSAA_SCALE`, `_FOREIGN_TILE`). Upstream ReGPU has `REGPU_NO_FOREIGN_TILE`; we force `REGPU_NO_FOREIGN_MSAA=1` (bar fix) — may matter here |
| C ✅ | My Cars: car picture = noise blocks, centre thumbnail missing (1782.5) | **Fixed (session 9, you confirmed):** on a car change (paint saved) the game resolves a 512×256 picture and a 256×128 thumbnail, then copies them on the CPU into the car's saved images. Write-back rule in `src/gpu_host.cpp` (`FM3_NO_PICTURE_WRITEBACK`). Old saved pictures stay broken until the car changes | **FM2 fixed it**: thumbnails are rendered, resolved, then compressed/saved by the CPU → resolve write-back of those sizes, with R/B swap handled. Old saved thumbnails stay broken |
| D | Pre-race menu (119.3) and photo mode (1219.9): upper third slightly whitened (edge ≈ y 410 of 1125 in the shot ≈ row 262 of 720) | — | FM4: photo mode's 64-bit accumulation shares an EDRAM base with the 32-bit scene; fixed with vendored-only `REGPU_COLOR_SURFACE_PER_BPP` (not in upstream). FM2: photo-mode tile fixes |
| E ✅ (S16) | Flickering shadow on the player car's right rear from other cars (266.2, intro race) | Fixed: colour clear through a 4x MSAA view scaled (`regpu-msaa-color-clear-scale`) | **FM4 fixed a match**: soft car-shadow cascades; other cars' silhouettes come from a depth copy read as 8888 bytes → vendored-only `REGPU_DEPTH_AS_8888` + `REGPU_CASCADE_*` (FM4 `gpu_host.cpp`) |
| B2 ✅ | **Race: a car with vinyls is covered in noise** (session 9); photo mode/countdown: other cars' decals and noise | **Fixed (session 11, you confirmed):** the game builds each car's livery at load (boot/garage) as an 8888 2048×2048 texture (close-ups) and a 1024×1024 one (racing): first strip/base resolve = whole image, other strips and vinyl regions resolved into it; its CPU then compresses them to DXT from memory. Rule in `src/gpu_host.cpp` writes both sizes back Deferred (`FM3_NO_LIVERY_WRITEBACK`, `FM3_LIVERY_WRITEBACK_NOW`); ReGPU patch `regpu-enclosing-writeback` writes each strip/region drawn into a written-back image too (`REGPU_NO_ENCLOSING_WRITEBACK`). Cars now show the same cut as the editor (B) | FM2's port wrote its race livery back (1024x1024) |
| F | Dither/stipple on foliage and fences (365.8) | We render at host MSAA 1× (log: `anti-aliasing 1x`) | Upstream ReGPU dithers alpha-to-coverage 2×2 at 1× (Xenia-style); FM4 uses plain D3D coverage (hard cut). A/B: `REGPU_NO_ALPHA_DITHER=1` (live) |
| G | Engine audio cutouts holding RPM (1027.1) | `xma: 5 s` stats: **0 discontinuity flushes in menus, 110–200 per 5 s in a solo race, 500–730 in the 6-car intro race**, each with codec reopens and 4–11k decoded samples dropped at "stream restarts". Our seek guard (`FM3_NO_XMA_SEEK_GUARD`) + drop-ahead (`FM3_NO_XMA_DROP_AHEAD`) fire ~30×/s while driving | Both ports have the queue depth/top-up/boost/consumed-means-delivered fixes we already carry. FM2: engine loops get a fresh codec per restart plus a seamless-loop table and crossfades (`seamless_loops.h`) |

Also logged (not reported as visible bugs): `unimplemented import XamParseGamerTileKey/XamReadTileToTexture` (1598.9, gamer picture), `XamSwapDisc/XamSwapCancel` (2070), `XamShowMarketplaceUI` (2077); `vfs: no mapping for "Update:\media\cars\driver\*.gr2"` at each race load (title-update files we don't mount).

**Session 8 status:** FM3 is playable on our build. Boots, menus, races to the finish; controller, keyboard and audio work. All visual bugs you reported in session 8 are **fixed and confirmed by you**:

| Bug | Cause | Fix (where) | A/B switch |
|---|---|---|---|
| Screen-wide bar (rows ~360–511) | ReGPU "foreign MSAA" view path on FM3's two-tile 2×MSAA scene | `src/gpu_host.cpp` default `REGPU_NO_FOREIGN_MSAA=1` | `FM3_FOREIGN_MSAA=1` |
| Sun white-out + slowdown | Occlusion query counted from tile 1's start to tile 2's end (6.47M samples) | ReGPU patch `regpu-zpd-tile-spans` | `REGPU_NO_ZPD_SPANS=1` |
| Fine dashed road streaks | Road strips rendered at track load, read from memory, never written back | `src/gpu_host.cpp`: 32×512 resolves → `Writeback::Mode::Now` | `FM3_NO_STRIP_WRITEBACK=1` |
| Square car shadow (my regression) | Fixed-16 fetch conversion applied to same-format reads and the border | ReGPU patch `regpu-fixed16-fetch` (corrected) | `REGPU_NO_FIXED16_FETCH=1` |
| Smooth road bands (rotated chase cam) | Road image's lower-half rebuild filed 256 rows early as a "tile" | ReGPU patch `regpu-resolve-into-image` | `REGPU_NO_RESOLVE_INTO_IMAGE=1` |
| (S9) My Cars picture/thumbnail noise or missing | 512×256 + 256×128 car-picture resolves read by the CPU, kept on the GPU | `src/gpu_host.cpp`: those sizes (8888) → `Writeback::Mode::Now` | `FM3_NO_PICTURE_WRITEBACK=1` |
| (S9) Matte paint, black chrome out of races | Showroom reflection cube is k_2_10_10_10, unsupported in ReGPU → decoded black | ReGPU patch `regpu-cube-2101010` (+ cube face staging bug) | `REGPU_NO_2_10_10_10=1` |
| (S11) Car with vinyls: noise/other cars' decals in races, countdown, photo mode (B2) | Livery textures built on the GPU, compressed to DXT by the CPU from memory | `src/gpu_host.cpp` livery rule (2048², 1024², Deferred) + ReGPU patch `regpu-enclosing-writeback` | `FM3_NO_LIVERY_WRITEBACK=1`, `REGPU_NO_ENCLOSING_WRITEBACK=1` |
| (S11) Vinyls cut along a straight line on sides and hood (B) | Big side-view passes resolved in 4× tiles: tiles 2–3 never drawn into their image (width 1378 vs pitch 1408; last tile 88 rows vs 86) | ReGPU patch `regpu-enclosing-big-pass` | `REGPU_NO_ENCLOSING_PASS_WIDTH=1`, `REGPU_NO_ENCLOSING_ROW_CLIP=1` (at the next car load) |

**Open / next (pick from here):**
1. **Vinyl leftovers** (small edge gaps; confirm the H fix in play; CRX re-check): **[VINYLISSUES.md](VINYLISSUES.md)**.
2. **Everything else** (D photo-mode/pre-race band, E shadow flicker, F foliage dither, G engine audio cutouts, Phase 4/5 items, leftovers): **[OPEN-ISSUES.md](OPEN-ISSUES.md)**, with evidence, sibling-port findings and a first step for each.

**How we work (agreed with you):**
- **You start the game with `play.cmd`** (project root: debug server on, saves in `out/user`, log in `out/run/fm3.log`) and drive to where a test needs to be, then report. Don't repeat the launch steps unless `play.cmd` changes; say so when it does. Temporary test lines in it get removed afterwards.
- I attach with `tools/fm3ctl.ps1`: `Fm3 "status"`, `Fm3 "capture"`, `ps <hash> tint|<expr>`, `hlsl <hash> out/hlsl/x.hlsl` (no spaces in that path), `switch NAME 0|1`, `Save-Fm3Shot`. I pause the game myself if needed. Rebuilding needs the game closed: ask first, then link with `tools/vcenv.cmd ninja -C build/rel fm3`.
- Method that worked: pause → shots → live `switch` A/B → `capture` (draw list in the log, resolved images in `out/run/out/res`, textures in `out/run/out/capture/tex`) → tint candidates to find the draw → `hlsl` → show single terms with `ps` edits → **measure** (box/edge metrics) rather than judge by eye → compare with hl360 when unsure what is correct.

### Previous status (session 7)
**Phase 3 goal reached: `fm3.exe` boots and renders.** The runtime (ported from the FM4 port, see `NOTICE.md`) runs FM3's recompiled code on ReGPU. From a fresh user folder, with no input: ESRB screen at ~8 s (correct, see `out/shots/boot_08s.png`), logo fades, then the **R8 intro video plays** (40 s: the door close-up hl360's dump showed; 70 s: the Le Mans prototype shot). **59.6 fps average** (4,140 swaps in 69.5 s, vblank 60 Hz, frame cap 60), 54 draws/frame in the video like hl360. 15 guest threads, GPU interrupt and ring buffer OK, audio driver at 188 callbacks/s with 0 dropped frames. No unimplemented-import warnings so far; only `ExGetXConfigSetting 0003/0001` and kernel variable ordinals 0x156/0x1BF are unknown.

**How to run:**
```powershell
$env:FM3_USER_DIR = "G:\claude code projects\FM3-ReGPU recomp\out\user"   # keep test saves out of Documents
cd "G:\claude code projects\FM3-ReGPU recomp\out\run"                       # fm3.log is written here
& ..\..\build\rel\fm3.exe "G:\claude code projects\FM3-ReGPU recomp\reference\rexglue recomp reference\hl360-forza3\game\disc1"
```
Build: `tools\vcenv.cmd cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link`, then `tools\vcenv.cmd ninja -C build/rel fm3` (≈ 2 min incremental; the first build compiles `out/ppc`, several minutes). Screenshots: dot-source `tools/fm3_route.ps1`, set `$Fm3Run`, then `Connect-Fm3` / `Save-Shot` (works on our window too).

**Next steps (Phase 4):**
1. Input: check keyboard/XInput reach the game (Return to skip the video), then get past the controls/loading screen to the intro race, as hl360 does in ~2.5 min.
2. Profiles/DLC (Q11–Q16): the FM4 XAM code is untested on FM3; Disc 2 DLC lives in hl360's `user/`.
3. Compare frames against hl360 (`dumps/hl360/runA`) once the start grid is reachable; Phase 5's dump hook.
4. Leftovers from Phase 2 (not hit yet): `blrl` trap in `sub_82A918F0` (SEH funclet call), SEH policy.
5. Still your call: whether to delete the empty `githubfileignorepls`. (Licence decided 2026-11-08: MIT, `LICENSE`, copyright braaap-stututu; borrowed code keeps its notices in `NOTICE.md`.)

---

### Previous status (session 6)
**Phase 2 (first recompilation) is done.** `tools/recomp.ps1` turns `game/default.xex` into `out/ppc` (45,760 functions, 179 files, 164 MB) with **0 unresolved branches**, and **all of it compiles** with clang-cl 23 (`-DFM3_PPC_ONLY=ON` → `fm3_ppc.lib`, 118 MB, no errors). The pipeline is reproducible: re-running it with `-Prepare` gives the same config, the same switch tables and byte-identical output. The repo is on GitHub: **https://github.com/braaap-stututu/FM3-ReGPU** (`main`).

**How to recompile:**
```powershell
& ./tools/setup.ps1                 # once: submodules, XenonRecomp patches, build/tools
& ./tools/recomp.ps1 -Prepare       # xexdump, helper/EH scan, XenonAnalyse, XenonRecomp, checks
& ./tools/recomp.ps1                # recompile only (config unchanged)
```
Outputs: `out/ppc`, `out/recomp.log`, `out/check.txt` (errors / traps / run-offs / pointer-only targets). Disassemble: `build/tools/ppcdis.exe game/default.xex <hex addr> <count>`.

**Known leftovers for Phase 3** (none block a first boot):
- **1 trap:** `blrl` in `sub_82A918F0`, the SEH helper that calls a filter/handler funclet by address (`mtlr r3; blrl`, with r12 = frame). Give it a `PPC_FUNC` override that does the indirect call.
- **16 SEH `__except` resume targets** (from the scope tables at the end of `.rdata`, 82199B10–8219A150) are not function starts. hl360 lists 15 of them as functions, and the 16th (`0x82A8E440`) is the one it excluded as an "EH continuation". They are resume points inside functions, so they are not added. They matter only if the guest raises an exception: decide a policy then (log + fatal is the likely start).
- **23 functions run off their end.** All end in calls that don't return: CRT `exit`/`_exit` (`825366E0`/`825366F0`), `longjmp`, `ExTerminateThread`, and assert/error paths (`82528800`, `82538110`, `82258A08`, `82AB4F18`, `822991F8`, probably no-return; not verified one by one).
- 313 imports become weak stubs (`tools/genstubs.py`, generated into the build folder).

**Deps (session 6b; FFmpeg resolved in session 7):** **FidelityFX SDK 2.1.0 is wired in** (`thirdparty/fidelityfx` is a junction to `G:\claude code projects\tools\FidelityFX-SDK-2.1.0`; all 5 signedbin DLLs Authenticode-valid, AMD). The full game CMake configures, and **`regpu.lib` builds against our `out/ppc` with FSR3 on**; `regpu_linkcheck` links with no unresolved externals. **FFmpeg still missing:** the winget `Gyan.FFmpeg` 9.0.2 you installed is the static **GPL** full_build (only `ffmpeg/ffplay/ffprobe.exe`, no `include/`, `lib/` or DLLs), so it can't be linked. Needed: BtbN `ffmpeg-n8.x-latest-win64-lgpl-shared-8.x.zip` (what FM2 names) extracted so `thirdparty/ffmpeg/{include,lib,bin}` exist. Only audio (`src/audio`) needs it.

(Session 6 next steps, now done: FFmpeg LGPL shared build; Phase 3 runtime port.)

---

### Previous status (session 5)
Phase 1 skeleton pushed (`65f82d0`): pinned submodules, FM4's 5 XenonRecomp patches, `tools/setup.ps1`, host tools, game CMake, `src/gpu_host.cpp`.

### Previous status (session 4)
**The baseline comparison kit is complete.**
- The hl360 baseline builds, runs and races (60 fps in-race; the post-race finish menu is a locked 30 fps).
- It captures frames with our SDK patch `0007-frame-dump-hook`. RenderDoc 1.47 is installed.
- **`tools/fm3_route.ps1`** drives a fresh-profile boot to the intro race's start grid with no human input, pauses there (frozen scene), and captures screenshots + dumps.
- **`tools/dumpdecode.py`** lists, decodes (all formats seen in-race) and diffs dumps.
- Two route runs diffed: 425/471 textures identical, same 28-resolve pipeline.

Our own XenonRecomp + ReGPU project has **not** been started.

**How to run the baseline:** `reference\rexglue recomp reference\hl360-forza3\out\build\win-amd64-release\fm3.exe` (no args). It reads the hand-written `fm3.user.toml` next to it (see *Baseline config*). Logs go to `logs\fm3_NNN.log` there.

**How to capture (scripted):**
```powershell
. .\tools\fm3_route.ps1
Invoke-Fm3Route -RunLabel myrun      # ~2.5 min; -> dumps\hl360\myrun\ (r01_grid_paused, r02_race_t10 + PNG shots)
```
**Manual:** with the game running, create `dumps\hl360\TRIGGER`; the next guest frame lands in `dumps\hl360\frame_NNNN\`. For draw-level inspection, launch `fm3.exe` from RenderDoc (`qrenderdoc.exe`).

**How to inspect / compare:**
```bash
python -I tools/dumpdecode.py list   dumps/hl360/myrun/r01_grid_paused
python -I tools/dumpdecode.py decode dumps/hl360/myrun/r01_grid_paused        # -> png/ (fetch swizzle applied; --raw to skip)
python -I tools/dumpdecode.py diff   dumps/hl360/runA/r01_grid_paused dumps/hl360/runB/r01_grid_paused --out dumps/diff_x
```

**Next steps** (full phased plan under *Work plan*; we are at the start of Phase 1):
1. Start the XenonRecomp + ReGPU project skeleton (FM2 `src/` + ReGPU wired FM4-style), with git + `.gitattributes` (`*.patch -text`, ignore `dumps/`, `game/`, `*.iso`) from day one.
2. In that port, emit the **same `index.txt` + `tex_*.bin` dump format** from a ReGPU host hook. Then `dumpdecode.py diff` compares hl360 vs ReGPU directly, and `fm3_route.ps1` can drive both (it only needs the window and the TRIGGER folder).
3. Optional: `pip install numpy` would make decode/diff roughly 20–50× faster (currently ~47 s for a full race frame). Needs your OK.

---

## Target build
| Field | Value |
|---|---|
| Game | Forza Motorsport 3 Ultimate Collection (USA) |
| Title / Media ID | `4D53084D` / `6F74359D` |
| default.xex | v0.0.0.8, 13,250,560 B, SHA-256 `aa15372c13bc4c899b5ef90a1cfb5634d389b0a3620cc9715f6023d61a9b8742` |
| Image | base `0x82000000` size `0xD60000`; code `0x821E0000`–`0x82B7E128` |
| Discs | Disc 1 MD5 `A527154D55C7B39849EC68CD46F17281`, Disc 2 MD5 `E65FF1CDC189F3EB9B1D0F14F97BAEA4` |
| Target stack | XenonRecomp (hedge-dev) + ReGPU (zer0ducksgiven), D3D12, Windows x64 |
| Templates | FM2-ReGPU (`src/` layout, kernel, audio, hooks), FM4-ReGPU (ReGPU linkage, fibers, indirect table, XR patches) |
| Baseline | hl360-forza3 0.1.3 (ReXGlue v0.10.0+2), reaches races at 60 fps |

## Host machine
- Windows 11 Home 10.0.26200; GPU AMD Radeon RX 7900 XTX (driver 32.0.32015.7029) + iGPU; G: has ~205 GB free.
- **Test machines (session 29, user):** (1) the main PC above, dual boot with Linux; (2) Ryzen 5 5600 + RX 6650 XT
  (RDNA2, 8 GB, PCIe 4.0 x8), dual boot with Linux; (3) **Steam Deck OLED** (SteamOS, plus Windows 11 on an SD card).
  Each can run Windows and Linux/Proton on the same hardware: see RESEARCH.md "Linux, Proton and the Steam Deck".

## Toolchain audit (2026-11-06)
| Tool | Needed by | Status |
|---|---|---|
| Git for Windows | all | ✅ 2.56.0 (`sh` exists at `C:\Program Files\Git\usr\bin\sh.exe` but is **not on PATH**; it can be added per shell, no install needed) |
| VS 2022 Build Tools + MSVC | all | ✅ 17.14, MSVC 14.44.35207 |
| Windows SDK (incl. `fxc`, `d3dcompiler_47`) | all | ✅ 10.0.26100.0 |
| CMake ≥3.25 | all | ✅ 3.31.6 bundled in VS Build Tools (on PATH after `vcvars64`) |
| Ninja | all | ✅ 1.12.1 bundled in VS Build Tools |
| Python ≥3.11 | XR/FM2/FM4 tools, hl360 tools | ✅ 3.15.2 |
| **LLVM/Clang ≥20** (clang, clang++, clang-cl, lld-link) | hl360 (hard requirement, expects `C:\Program Files\LLVM\bin`), XenonRecomp, ReGPU | ✅ **23.1.5** installed 2026-11-06 at `C:\Program Files\LLVM` (see config decisions) |
| winget | — | not available; not needed |
| FFmpeg win64 LGPL shared | FM2/FM4 template (XMA) | ❌ missing (later phase) |
| AMD FidelityFX SDK v2.x | ReGPU (`REGPU_FFX`, FSR1) | ❌ missing (later phase) |
| SDL3 3.4.x | FM2 template | comes via submodule/thirdparty (later phase) |
| RenderDoc | frame-capture diffing; ReGPU auto-enables its hook if `C:\Program Files\RenderDoc\renderdoc_app.h` exists | ✅ **1.47** installed 2026-11-07 (`RenderDoc_1.47_64.msi` from renderdoc.org/stable, Authenticode valid: "Baldur Scott Karlsson", Sectigo CA; SHA-256 `240e1253…7725`) |
| Microsoft PIX | TS-style D3D12 capture (optional) | ❌ missing (optional) |
| Vulkan SDK / vcpkg | — | not needed |

## Config decisions
- **2026-11-06** Reference layout: `reference/rexglue recomp reference/{hl360-forza3,forza3-recomp}` (as requested), plus `reference/framework/{XenonRecomp,ReGPU}` and `reference/sibling-recomps/{Forza-Motorsport-2-ReGPU,Fuzzy-Rittersport-4-ReGPU}` for the target-stack mechanisms. All are shallow clones (depth 1).
- **2026-11-06** The hl360 clone has repo-local `core.longpaths=true` (also in its submodules): `rexglue-sdk/thirdparty/vulkan-loader` failed to check out because a test file name went over the 260-char limit. This is not a system-wide change.
- **2026-11-06** Agent-produced catalogues saved to `reference/catalogs/` as the verbatim source for FM3-QUIRKS.md (full address lists live there).
- **2026-11-06** Installed **LLVM 23.1.5** (latest release, `LLVM-23.1.5-win64.msi`, official llvm-project GitHub release) to the default `C:\Program Files\LLVM`, user-approved. Checks: SHA-256 `288ab326…7c12` matches GitHub's asset digest, and the GPG signature is good (key `FFB3 3689 80F3 E6BB 5737 145A 316C 56D0 64CA CBA5`, Douglas Yung, from releases.llvm.org/release-keys.asc; checked against a temporary keyring). The MSI has no Authenticode signature, which is normal for LLVM. It is *not* added to the system PATH; build scripts get it through `devenv.cmd`. hl360 asks for "20 or newer" and was likely developed on 20–22, so **if the SDK build fails on new clang-23 diagnostics, fall back to LLVM 21/22** before patching code.
- **2026-11-07** ISOs live in the project root: `FM3UC U Disc 1 (Play Disc).iso` (MD5 `A527154D…7281` ✅), `FM3UC U Disc 2 (Content Install Disc).iso` (MD5 `E65FF1CD…BAEA4` ✅). Both match hl360's reference dumps.
- **2026-11-07** **Line endings (CRLF) fix for hl360.** The global git `core.autocrlf=true` checked out hl360's `patches/sdk/*.patch` and the SDK sources as CRLF, so `git apply` failed on every patch. Fixes, all repo-local and none global: (1) `thirdparty/rexglue-sdk`: `core.autocrlf=false` + re-checkout (`git rm -r --cached . && git reset --hard`) so sources are LF; (2) hl360: `*.patch -text` in `.git/info/attributes`, patches re-checked-out as LF. hl360's `.cmd` scripts stay CRLF on purpose (cmd.exe prefers it). **Same trap applies to our own repo and to XenonRecomp/ReGPU patches later; use `.gitattributes` with `*.patch -text`.**
- **2026-11-07** hl360 SDK built with hl360's unmodified `build_sdk.cmd` on clang 23.1.5 with no source changes needed. The build registered `rexglue` in the CMake *user package registry* (`HKCU\Software\Kitware\CMake\Packages\rexglue`), a side effect of the SDK's install step.
- **2026-11-07** **Our repo:** `git init -b main` in the project root, repo-local `core.longpaths=true`. `.gitattributes`: `* text=auto eol=lf`, `*.patch/*.diff -text`, `*.cmd/*.bat/*.ps1 eol=crlf`, binaries. `.gitignore`: `*.iso`, `*.xex(p)`, `game/`, `dumps/`, `out/`, `build/`, `reference/`, `*.log`, and the locally supplied `thirdparty/{ffmpeg,fidelityfx,dlss,sdl3,directx-headers}`.
- **2026-11-07** **Submodules (pinned):** `thirdparty/XenonRecomp` = hedge-dev `ddd128bcca99fe8bfbb99bea583c972351fa6ace` (same commit FM4 pins; its 6 nested submodules recursive), `ignore = dirty` because our patches sit in its worktree. `thirdparty/ReGPU` = zer0ducksgiven `57143cb79e8dd4de7b776c70867313cd0b1862f2` (upstream HEAD, 2026-11-03). Both have repo-local `core.autocrlf=false` and were re-checked-out as LF (the first checkout was CRLF from the global setting).
- **2026-11-07** **ReGPU source: upstream submodule, not FM4's vendored copy.** FM4 keeps a *plain directory* `thirdparty/ReGPU` that differs from upstream in 46 files: it has `edram*.cpp/h`, `gamma_ramp.*`, `smaa_passes.hlsl` and test tools (`edramtest`, `resolvetest`, `shadertest`, `gammatest`) that upstream lacks, while upstream has `include/regpu/driver.h` (`REGPU_GUEST_ABI=rexglue`) that FM4's lacks. Same `Host`/`Config` API for everything our `gpu_host.cpp` uses. We start on upstream (cleaner, pinnable, has the rexglue A/B mode). **If Phase 3/5 rendering lacks EDRAM/gamma behaviour FM4 has, compare with FM4's copy first.**
- **2026-11-07** **Host tools are a separate CMake project** (`tools/hosttools` → `build/tools`), like FM4's `tools/xexdump`: XenonRecomp + XenonAnalyse + our `xexdump`. The game CMake (`CMakeLists.txt`) is FM2/FM4-style and FATALs without `out/ppc` or `thirdparty/fidelityfx`. Not yet carried over: FM2/FM4's configure-time `patch_mftb.py` (Phase 3, with the runtime's timebase), `genstubs.py` (Phase 2/3), the `.rc` icon.
- **2026-11-07** `tools/setup.ps1` checks the patch stack by building "pinned commit + all 5 patches" in a scratch index and comparing it with the worktree (`git write-tree`): clean → apply all; equal → skip; anything else → stop. Per-patch `apply --reverse --check` doesn't work because the patches overlap.
- **2026-11-06 (planned)** Baseline frame diffing: use hl360 for the ReXGlue side. A later option is ReGPU's `REGPU_GUEST_ABI=rexglue`, so ReGPU renders under a ReXGlue host and isolates CPU-side from GPU-side differences.

## Patches applied
- **Our project, XenonRecomp** (`ddd128b`), 2026-11-07, in this order, applied by `tools/setup.ps1`; details and provenance in `patches/README.md`:
  1. `xenonrecomp-fm2` — PPC divides never trap; MSR[EE] → `PPC_MSR_CHANGED` (interrupt lock).
  2. `xenonrecomp-modules` — per-image name prefix (unused: FM3 is one image); warns on unnamed import thunks.
  3. `xenonrecomp-fm4` — XenonAnalyse jump-table back-trace (Q6), fallthrough count, XEX image size from security info.
  4. `xenonrecomp-fm4-instructions` — VMX128 / CR-bit instruction fixes.
  5. `xenonrecomp-switch-u32` — switch on `u32` index; `vcmpbfp` implemented.
  **Why:** all generic recompiler fixes the FM4 port needed on the same engine family; none carries FM2/FM4 addresses. Taken unmodified from FM4 commit `69bdae4` git objects (LF). Combined: 8 files, +1241/−24.
- **Our project, ReGPU** (`57143cb`), session 8, in this order, applied and verified by `tools/setup.ps1`; details in `patches/README.md`. All game-neutral, each behind a `REGPU_*` switch:
  1. `regpu-fixed16-fetch`: resolved 16-bit fixed images read with the fetch's number format when it differs from the copy's (not the border colour).
  2. `regpu-zpd-tile-spans`: occlusion queries count only inside each tile's ZPD on/off pair (sun glare). Also: `REGPU_TRACE_ZPD` logs `RB_SAMPLE_COUNT_CTL`.
  3. `regpu-resolve-into-image`: a resolve with a row offset into an existing image of the same width is part of that image, not a tile (road bands).
- **ReGPU patch 15** (session 14) `regpu-draw-thread-pieces`: draw thread with occlusion queries and with the parallel segments; FM3 default = draw thread alone (`REGPU_DRAW_THREAD=1`, `REGPU_DRAW_THREAD_SERIAL=1`).
- **ReGPU patch 14** (session 14) `regpu-zpd-scale`: `REGPU_ZPD_SCALE=<f>` multiplies occlusion answers (live via env); FM3 default 0.6 (sun glare size matched to console footage by eye).
- **ReGPU patch 13** (session 14) `regpu-worker-queries`: occlusion queries with draw workers (a counting query's draws go to the command processor's list); `REGPU_NO_WORKER_QUERIES=1` = old fixed answer.
- **ReGPU patches 4–12** (sessions 9–13) are listed with reasons in `patches/README.md`; the 12th (session 13) is `regpu-early-fence-writebacks`: with early fences, a fence behind a deferred resolve write-back keeps the fence `RendererFenceSubmit` returns (ReGPU's `WriteBack` zeroed it), so CPU-read resolves (FM3 liveries) are written before the game sees the fence. `REGPU_EARLY_FENCES_SKIP_WRITEBACKS=1` = old.
- **Session 13 host defaults** (`src/gpu_host.cpp`): vblank = 2× frame cap off 60 + `mainthread30fps` (frame-rate unlock); `REGPU_NO_FENCE_ORDER`, `REGPU_TEXTURE_CHECK_STAGGER`, `REGPU_DIRECT_DYNAMIC_INDICES` (via `regpu::driver::SetSwitch`), `REGPU_EARLY_FENCES`, `REGPU_DRAW_WORKERS=3` unless `accurate_sun=1`. Table with numbers and undo switches at the top of this file.
- **Our project, host rules** (`src/gpu_host.cpp`, not patches): `REGPU_NO_FOREIGN_MSAA=1` default (bar); 32×512 colour resolves written back `Now` (road strips); vblank 60 Hz (Q24).
- **hl360 baseline:** its own `patches/sdk/0001–0006`, applied unmodified by `build_sdk.cmd` to ReXGlue SDK `c94f5eb` (v0.10.0+2). No changes to hl360 sources. The only changes to the checkout are repo-local git line-ending settings (see config decisions).
- **hl360 baseline, ours: `patches/sdk/0007-frame-dump-hook.patch`** (2026-11-07, about 215 lines, written from scratch; revised twice in session 4: per-frame hash cache, never reuse an existing `frame_NNNN`). It lives in hl360's `patches/sdk/` so `build_sdk.cmd` picks it up like the others. **Why:** hl360 has no frame dump, and `--texture_dump_dir` is *not* in stock ReXGlue (it was forza3-recomp's unpublished local hook). We need guest-side texture/resolve data from the baseline to diff against ReGPU later. **What:**
  - cvar `texture_dump_dir` (hot-reload, default empty = off).
  - `TextureCache::OnGuestSwap()` is called at the top of `D3D12CommandProcessor::IssueSwap`. It closes an open capture, then checks `<dir>/TRIGGER`; if present, it deletes it and opens `<dir>/frame_NNNN/index.txt`.
  - `TextureCache::DumpDrawTextures()` runs at the top of `RequestTextures` while a capture is open. It writes one line per fetch slot per draw, and the base level of each distinct texture once as raw guest memory: `tex_<base>_<w>x<h>_f<fmt>_<xxh3>.bin`.
  - The d3d12 command processor passes VS/PS ucode hashes via `SetDumpDrawShaders()`. `draw_util::GetResolveInfo` logs `# resolve …` lines (rect, src format → dest format, base, range, dest_info, msaa).
  - `SharedMemory::memory()` was made public.
  - Index token layout matches forza3-recomp's `texdecode.py` (w=7, h=8, pitch=10, tiled=11, fmt=13, endian=14, fetch=17–22, file=last), so their decoder logic is reusable.
  - **Limits:** D3D12 path only; base mip only; resolve-produced textures are dumped from guest memory, i.e. the `fast` readback (a frame late), not necessarily what the host GPU sampled.

## Baseline config (hl360) — `out/build/win-amd64-release/fm3.user.toml`
Hand-written with the same keys the launcher writes, so runs are reproducible without the launcher GUI:
- `game_data_root` = `<hl360>\game\disc1`, `user_data_root` = `<hl360>\user` (Disc 2 DLC installed there: 8 packages + `.header` licences under `0000000000000000\4D53084D\`)
- profile `Baseline`, XUID `E0000BA5E11E0001` (offline `0xE…`, bits 54–55 clear); language 1 / country 103 (English/US)
- windowed **1280×720**, `resolution_scale = 1`, `anisotropic_override = -1` (game default), `swap_post_effect = none`, `present_effect = bilinear`, letterbox on
- `gpu_backend = d3d12`, `vsync = true` (guest vblank), `readback_resolve = fast`, `async_shader_compilation = false`
- `mnk_mode = true` (keyboard works), `frame_stats = true` (5-second frame-interval stats in log)
- `texture_dump_dir = 'G:\claude code projects\FM3-ReGPU recomp\dumps\hl360'` (patch 0007; idle cost is one file-exists check per frame)
- Data paths: `game/disc1` 3,059 files / 6.8 GB, all **3,057 manifest files verified** (SHA-256 + size, 0 bad); `game/disc2` 3.4 GB; `user` 3.4 GB.

## Known-broken / observations on the baseline
- **Frame pacing in the intro (2026-11-07):** average holds at exactly 60.0 fps (avg 16.67 ms) but **~100–150 of every 300 frames exceed 17.5 ms**, with p99 ≈ 22 ms and almost none over 33 ms. That looks like alternating short/long intervals, not dropped frames. It could be the intro video's own cadence or present-vs-vblank beat; re-measure in a race before drawing conclusions. A useful number to compare our ReGPU build against.
- **First race (2026-11-07, user-driven intro race; log `fm3_001.log`, 13:12–13:29):**
  - Intro/attract (13:12–13:25:45): the alternating pattern above, about 145/300 frames late.
  - Race load (13:25:50–13:26:27): 14–42 fps, stalls up to 1.37 s (load + first-time shader compiles).
  - **In-race (13:26:27–13:29:18): about 60 fps, only ~35–75/300 frames over 17.5 ms, p99 ~19–25 ms**, with occasional 35–48 ms spikes (likely shader compiles). Pacing is *better* in-race than in the intro.
  - **After the race (13:29:23 → exit): locked at exactly 30.0 fps** (151/151 frames over 17.5 ms).
- **30 fps identified (2026-11-07, second run `fm3_002.log`, you noted the times):**
  - Race 13:44:39–13:48:09 at 59.4–60.0 fps; **pause menu (~13:46) stays at 60**.
  - **Post-race finish menu 13:48:14–13:49:00 is locked at 30.0 fps**, with even intervals (p99 36–40 ms).
  - Race load/intro 13:44:04–13:44:34 runs at 26–43 fps.
  - **Interpretation (unconfirmed):** the perfectly even 30.0 lock looks like the game choosing a 2-vblank present interval on that screen (a deliberate 30 fps UI), not an emulation slowdown. To confirm later: check the presentation interval the guest passes at `VdSwap` (hook point `sub_824AF280` → `VdSwap` at `0x824AF4F4`), or compare against a real console / ReGPU build. Our ReGPU build should reproduce exactly 30.0 there if it's game-native.
  - Also logged: `ResolvePath(Update:\media\cars\driver) failed`, the same expected title-update probe.
- Frame capture plan (decided 2026-11-07): use both tools for different jobs. RenderDoc answers "why" (draw-level inspection, manual); the dump hook answers "whether/where" (scriptable guest-side textures/resolves, comparable across renderers).
- Log error on boot: `ResolvePath(update:\media.zip) failed - device not found`. Expected: no title update installed, and the game probes for one.
- Build warnings in hl360's own `crash_handler.cpp` / `file_signatures.cpp` (macro redefined, deprecated CRT) are cosmetic. The codegen reported **0 unresolved** branches/functions.

## Known-broken / risk areas (from the reference projects; details in FM3-QUIRKS.md)
- **Vblank pacing (Q24):** FM3 paces on guest vblank, and ReGPU fires vblank at 2× the frame cap. Start with `REGPU_VBLANK_HZ=60`.
- **Car shadow block (Q19):** needs integer number-format handling in both resolve and fetch. Check what ReGPU supports.
- **Road flicker / thumbnails (Q18):** needs resolve writeback rules (FM4-style `resolveWriteback`).
- **Fibers (Q7):** need host-fiber overrides; SwitchToFiber body `0x82A82210` size `0x40C`.
- **Jump tables (Q6):** probably need FM4's XenonRecomp switch back-trace patch (inference).
- Career, multiplayer, replays and most tracks are untested in both reference ports; Xbox Live is unavailable.

## Work plan (phases)
Status legend: ✅ done · ▶ next · ☐ later. Each phase ends with a NOTES.md update.

**Phase 0 — Setup & baseline ✅** (sessions 1–4)
- ✅ Toolchain: LLVM 23.1.5, RenderDoc 1.47; VS BT 2022 bundles CMake/Ninja.
- ✅ References cloned, `FM3-QUIRKS.md` written (26 quirks + GPU table).
- ✅ hl360 baseline built and running (SDK patches 0001–0006 + our 0007 dump hook); races at 60 fps.
- ✅ Comparison kit: `tools/fm3_route.ps1` (scripted start-grid capture), `tools/dumpdecode.py` (list/decode/diff).

**Phase 1 — Project skeleton ✅ (session 5; licence: MIT, session 10)**
1. ✅ `git init`, `.gitattributes`, `.gitignore` (see config decisions). ☐ First commit: needs your identity/OK.
2. ✅ Layout: `config/`, `patches/`, `src/` (planned layout in `src/README.md`), `tools/`, `thirdparty/{XenonRecomp,ReGPU}` submodules, pinned.
3. ✅ ReGPU CMake block + `src/gpu_host.cpp` (FM4-style, FM3 title, vblank 60 Hz default; compiles from Phase 3 when the runtime exists).
4. ✅ All 5 XenonRecomp patches applied; patched XenonRecomp/XenonAnalyse build with clang-cl 23.1.5.
5. ☐ Deps to request: FidelityFX SDK v2.x (required by ReGPU's CMake), FFmpeg win64 LGPL shared. SDL3 only if we take FM2's wheel input. Optional: numpy.

**Phase 2 — First recompilation ✅ (session 6; details in the session log)**
1. Extract `default.xex` from Disc 1 into `game/` (or reuse `reference/.../hl360-forza3/game/disc1`). Verify SHA-256 `aa15372c…8742`.
2. XenonAnalyse → switch tables. Find helpers (`savegprlr_14` etc.) and EH `invalid_instructions` with the FM4 `fm4_config.py` approach.
3. `config/fm3.toml`:
   - setjmp `0x82534B20` / longjmp `0x825345E0` (Q1)
   - the 15–16 boundaries with sizes (Q2/Q3)
   - the fptr union (Q4, sizes required; check with `fm4_crosscheck.py` against hl360's `fptr_functions.toml`)
   - SwitchToFiber `{0x82A82210, 0x40C}` (Q7)
4. Run XenonRecomp; grep the log for `// ERROR` / out-of-function switch cases; iterate until clean. Generate `stubs.cpp`.

**Phase 3 — Boot to first frame ✅ (session 7)**
- ✅ Runtime ported from FM4 (`src/`, NOTICE.md); `indirect.cpp`, `PPCMsrChanged`, `PPCMmioStore32` come with it; `PPCLookupIndirect` is fatal on a miss.
- ✅ Fiber overrides with FM3 `hooks/addresses.h`; mftb patch (`tools/patch_mftb.py`, 25 reads in 11 files); vblank 60 Hz (Q24).
- ✅ Goal: ESRB/logos and the intro video render, ~60 fps.
- ☐ Carried to Phase 4/5: `sub_82A918F0` blrl override and SEH policy (not hit yet); `cache:` utility drive (Q8: FM4's VFS may already mount it; check at race start); game-speed check against hl360 (timing of the intro/video).

**Phase 4 — Menus & content ▶ (partly done)**
- ✅ Intro, controls/loading screen, intro race to the finish, pause menu, photo mode reachable (session 8, you).
- ◐ Disc 2 DLC (Q11–Q14): installer + runtime done in session 17, all 8 packages mount; Fujimi Kaido raced fine and career shows more events (user). Licence mask all bits (Q12: `FM3_DLC_LICENSE_MASK`), profile XUID/gamertag rules (Q15), language+country pairs (Q16); career and menus beyond the intro race.

**Phase 5 — Race & GPU parity ▶ (started, session 8)**
- ✅ Road: strip write-back at load (Q18 part) and road-image half placement (ReGPU patch). ✅ Car shadow correct (Q19: no square, unlike hl360). ✅ Sun glare (occlusion under tiling). ✅ Screen bar (foreign MSAA).
- ✅ (session 9) Showroom/garage reflections (k_2_10_10_10 decode), My Cars pictures (512×256/256×128 write-back), race-load thread crash.
- ✅ (session 11) Vinyls: B (straight-line cut) and B2 (race/photo livery noise) fixed; H (vanishing car / purchase crash on the damaged save) fixed, final build to confirm in play.
- ◐ Vinyl leftovers: small edge gaps on big regions (Scion, Fiat incl. rear wing, Supra rear bumper), CRX re-check: [VINYLISSUES.md](VINYLISSUES.md).
- ☐ Photo mode whitened top-third band (OPEN-ISSUES D).
- ✅ (session 12) Above 1×: My Cars thumbnail ghost (picture texture = 512×256 at mip 0 + 256×128 thumbnail at mip 1; guest mip choice for short chains, ReGPU patch 11). 2560×1440 and 4x MSAA run.
- ☐ More screens at 1440p / 4x MSAA (other "720p-sized" assumptions); the one-off race dip (1440p + 4x MSAA, not reproduced).
- ☐ Car thumbnails (Q18 rest), audio queue depth (Q26), XMA path check.
- ☐ ReGPU-side dump hook in the **same index.txt/tex_*.bin format**, then `dumpdecode.py diff` hl360-grid vs ReGPU-grid (would speed up finding the next parity bugs).
- ☐ Post-race finish menu at exactly 30.0 fps? Full-lap fps measurement.
- ✅ (session 13) **Performance pass** (OPEN-ISSUES P): near cars ~70 → ~150 fps at 1440p 4x MSAA (no fence order, early fences + ReGPU patch 12, two per-draw switches, draw workers).
- ✅ (session 14) Occlusion queries with draw workers (patch 13) and with the draw thread (patch 15): sun flare and speed together; sun glare sized to console footage (patch 14, `REGPU_ZPD_SCALE=0.6`).
- ✅ (session 14) Command-processor bound found and moved: **draw thread default** (cars grid 175 → 181–184, Montserrat 225 → 240 at 1440p 4x MSAA, now GPU-bound there).
- ✅ (session 15) Draw thread + workers: sun flare fixed with it (`seg::g_query` cleared by the CP ahead of the draw thread); but slower than the serial draw thread (720p 1x 195 vs 216), so opt-in.
- ◐ (session 15) Crash C1: workaround in (`src/hooks/ai_drivers.cpp`: a car whose driver reads back an empty Name gets a generic driver; held in play, no crash, no odd names). (session 16) Row 125's `Name` is NULL in the in-memory DB when it happens. ▶ **Next:** the game's DB patch system (LicenseMasks `.sql`, overwrite/merge/exec) as the likely writer. OPEN-ISSUES C1.
- ✅ (session 16) Livery broken on some race loads: new livery image a few KB above the previous load's, pieces went into the old one (ReGPU patch 16 `regpu-livery-enclosing-recent`; also a per-texel write-back safety check). Confirmed over several reloads.
- ✅ (session 16) OPEN-ISSUES E, other cars' shade flickering on the player's car: a colour clear through a 4x MSAA view (ReGPU patch 17 `regpu-msaa-color-clear-scale`). Confirmed paused and in races.
- ✅ (session 16) Debug server: shots after a capture no longer stall ~30 s (patch 18). `Fm3 "shots N dir raw"` bursts + `REGPU_FREEZE_RESOLVES` = the flicker method.
- ☐ Alt-tab hang (session 15; thread dump in `out/logs/session15-c1-run3.log`).
- ☐ Report the early-fence write-back bug upstream (ReGPU `gpu.cpp` `WriteBack`; also in FH1's fork and PGR4). Smaller CP ideas left: FH1R's CP tricks, PGO, `-march=x86-64-v3`.
- ◐ LOD / render distances (OPEN-ISSUES L): session 19 toolkit and results (LOD cuts don't move fps on this PC).
- ✅ (session 20) Frame limit understood: the render thread t7 mostly spin-waits on ReGPU; at 1440p 4x the GPU is the
  limit, and the race vs time-trial gap is the AI cars' material shaders (~0.19 ms per car).
- ✅ (session 20) Alpha-test-free pixel shaders (ReGPU patch 21 `regpu-split-alpha-test`, default on): race grid +5%,
  picture identical; their pipelines prebuilt at boot.
- ▶ AI-car cost without visual change: re-measure, hidden-part cull, preset shader shortcuts, overdraw count, VRS for
  distant cars (NOTES top, "Next").

**Phase 6 — Polish ▶ (launcher started, session 12)**
- ✅ Launcher `launcher/FM3Launcher` (from your FM4 fork): Play (game folder, `default.xex` SHA-256 check, language), Profiles + backups, Graphics (resolution, aspect, fullscreen, fps cap, VSync, MSAA, FSR 1/3, anisotropy, shader box), Settings (shader cache, folders, test timer, debug server, perf log). `launcher.cmd` for the dev checkout; `FM3Launcher.exe --play` starts the game without the window.
- ☐ Launcher: game import (ISO/folder drop; Disc 1 only is used), DLC packs, photos (FM3 format unknown), icon, then maybe updater/reports. FSR 4 / SMAA / shader optimiser after a ReGPU update.
- ☐ Unused setting: `enhance` is read by `src/settings.cpp` but FM3 doesn't apply it (gpu_host doesn't pass it). `skip_intro` works since session 17 (plus `quick_race`).
- ✅ (session 13) Frame-rate unlock: `max_fps` 30–360, game time follows the clock (FH1's scheme, `src/hooks/frame_time.cpp`); tested at 120 and 240, smooth. Your cap since session 14: **360** (not faster than the game's tick rate). ☐ Mods (Q9 re-signing, PF+3).
- ✅ (session 15) Launcher Settings → Testing → "Draw workers" toggle (A/B; off by default).
- ◐ (session 13) Racing wheels + force feedback ported from the FM4 fork (launcher "Wheel stuff" tab); ☐ test with a real wheel.

## Session log
### 2026-11-12 — session 29b (cloud: Project Forza Plus 3 research)
- User: "research how to add Project Forza Plus 3 as an option". No code; RESEARCH.md section + NOTES top. Found
  that loose-file mods skip `.zip` paths, mods don't reach DLC files, and Q9 re-signing is missing.
- Then four more studies on the user's questions (all RESEARCH.md, index rows at the top): **ray tracing** (screen-space
  AO/reflections first, RT shadows later; no path tracing), **vendor-specific speed-ups** (PERF-PLAN L45-L50: RGP capture
  for decompress barriers first; **ReBAR/GPU upload heaps need code in ReGPU**, lead = S18's constants-in-ring -21%),
  **Linux/Proton/Steam Deck** (PERF-PLAN L51-L53; the user's 3 dual-boot test machines in "Host machine"). The user wants
  these written up for the other 360 recomps once measured. Branch `ccr-d85d3ffd-fbzvco`, merged into `main` (user).
### 2026-11-11/12 — session 27 (mirror analysis, L37/L39/L40/L14, audit)
- User: two-colour mirror question -> mirror captures on the user's Catalunya quick-race grids: cars are most of the
  mirror's cost; shadow blobs, car detail level, separate shadows: no gain. Experiment switches in `mirror.cpp`.
  Cockpit tiling check done. One guest tile stays opt-in (user). L37 parked (2.4% GPU), L39 not worth it, **L40 built
  (patch 31, default under caps <= 144)**, L14 tested. Audit of open leads, FH1R v0.3.2, ReGPU branches, FM2 boot
  compile; next: shader/pipeline discovery. settings.ini restored; game closed.
### 2026-11-11 — session 26 (L38 timer, L36 one guest tile)
- User: "let's continue with the performance optimization leads". L38: minimized boots were never throttled (Sleep(1)
  ~1.3 ms either way); fix kept as a safeguard. L36: found FM3's tiling scenarios and set-up, built one guest tile
  (hook + ReGPU `regpu-window-fixup-union`, patch 30), opt-in with a launcher toggle: CPU-bound grid +9.1%, picture
  within noise. Waiting on the user's drive before it becomes a default. settings.ini restored (2560x1440, msaa 4).
- User drives (ReLive videos + logs): one tile on twice, fine (an unrelated one-frame cyan bush, parked). Cockpit
  ~12% slower than chase = the rear-view mirror's second scene; found its render (`sub_827FDB88`) with a t7 profile;
  built the half-rate mirror (opt-in, `mirror_rate`, launcher toggle; user: "looked good", stays off by default).
  Mirror threshold mod and lower-resolution mirror ruled out. One revert mistake (a `git checkout` of ReGPU's
  renderer.cpp dropped the patch stack's changes; restored from the stack, gotcha added to CLAUDE.md). User's
  settings.ini: one_tile=1, mirror_rate=2.
### 2026-11-11 — session 25 (draw thread: L12 asynchronous frame end)
- User: "let's tackle the draw thread next". Profiled ReGPU's draw thread with whole call stacks (sampler reworked:
  `FM3_SAMPLE_CALLERS=N`; two freezes on the way, from unwinding and getenv while the thread was stopped, fixed).
- Found ~10% of the draw thread waiting at the frame end (Present asleep in AMD's driver, the replay wait at the swap);
  built L12 (`regpu-async-present`, default): CPU-bound grid +17.9%, 1440p 4x +4.3%, byte-identical, boot/shot/capture
  fine. `~PsAlias` store explained (store buffer at the draw's exit). settings.ini restored (2560x1440, msaa 4).
- User drove L12: straight improvement, cockpit view noticeably better; 264/311 fps race averages, none under 180.
- Part 2: patch `regpu-draw-thread-memos` (vertex memo +3.6%, pass snapshot +2.2%, timer/overlay without the replay
  wait); CPU-bound grid ~288 fps. Upload hashing measured (stable reuse +1.2%, off: risk); L10 scoped (18% of draws).
  User drove part 2: FPS floor noticeably higher (user); lowest stretch 200. Open: L10 build (ask).
- Launcher option (user request): Settings > Testing **"Asynchronous present"** (L12, on by default) writes
  `async_present=0|1` to settings.ini; the game reads it (so `play.cmd` runs follow it too) and logs
  `gpu: asynchronous present on|off`. Checked both ways with grid boots (off 245.3, on 246.7 fps at 1440p 4x).
### 2026-11-11 — session 24 (shaders at first boot, minimized test boots)
- Checked which leads are left (PERF-PLAN) and FH1R v0.3.2 (its CP split ~ our draw thread; descriptor reuse L8,
  changed-register handoff L28 and present-without-replay-wait L12 not built). User picked FM2-style shader
  precompilation: FM3 already reloaded its cache at boot; added the static scan + seed (above). Two cold boots in a
  throwaway user folder (DLC by junction, removed afterwards), one warm. `FM3_MINIMIZED` on user request.
- Part 2: leftovers fixed (`regpu-seed-variants`), then L6 (RGA downloaded with the user's OK) and L30
  (`regpu-fetch-variants`, default on). All boots minimized; game closed at the end.

### 2026-11-11 — session 23 part 2 (performance leads, hands-off)
- Phases on the frozen grid: boot check (3 boots), 1 live switches, 3 GPU time/overdraw, L11, fixed-dt, 2 start-time
  switches, 4 CPU-bound (720p 1x), 5 stacked pieces (3 rotated rounds), 6 preset bug isolation, 7 presets after the
  fix, 8 L11 frame pacing. Details and tables: PERF-RESULTS.md. Harness bug found and fixed on the way (CSV rows with
  no shot were dropped; phase 3/4 numbers kept in the journal).
### 2026-11-11 — session 23 (research merge, frozen world, test-grid route)
- Merged cloud branches `ccr-acb8fc05-rpzqri` (sessions 21/22) and `ccr-2e46a7d8-wv8coh` (21b, Anti-Lag 2/Reflex);
  conflicts were additions on both sides (kept both; the Anti-Lag session renamed 21b).
- User put the splash-screen skip aside (screens are mostly gone already).
- Walked the route to CrownJewel with the virtual pad (screenshots per step), then the user recorded an A-spam boot;
  `FM3_FREEZE_TIME` + `FrameTimeFreeze`, the route in `grid_test.cpp`, settings/launcher option. Details above.
### 2026-11-10 — session 22 (cloud: research: FH1R, upstream ReGPU, Sonic Unleashed, Pinyon Shift, ITC, features, VR)
- Research only, no code (RESEARCH.md "FH1-ReImagined's latest performance work, and Sonic Unleashed Recompiled";
  new PERF-PLAN items L24–L29, I8; L10/L23 notes).
- FH1R since 11-05: command processor on two threads by default with ≥6 cores (+13%; −11% on 4 cores), descriptor
  reuse (−7% CP per draw), present without the replay wait now default, a GPU profile by kind of work that found
  render-target transfers at 30–37% of the GPU frame, a ReGPU D3D12 plugin (their fork `fh1`: `REGPU_ANISO_FROM_FETCH`,
  no tile merge (0 gain), PGO +6%).
- Upstream ReGPU is 15 commits past our pin (vertex reach + submit per view, one-copy queries, texture budget, live
  MSAA/fullscreen): rebase = L27.
- Sonic Unleashed Recompiled: hardware MSAA resolve, pipelines built at asset load behind the loading screen,
  all XenonRecomp register options on with mid-asm hooks (FM3 keeps `non_volatile_as_local` off).
- Suggested next on the user's PC: L26 (GPU time by kind on the race grid), then I8 (`REGPU_ANISO_FROM_FETCH` A/B).
- Pinyon Shift (FH1, ShiftGlue; RESEARCH.md section): fetch-state shader variants (L30: our `TFetch` branches per fetch),
  per-hash 0×Inf rule (I3 note), depth-alias copies, reflection cube rate (V8), inline register save/restore + fewer
  MXCSR toggles (L25 rewritten: **`non_volatile_as_local` measured 20–27% slower there, so not that**), pacing (L31),
  pipelines waited at submit (L32), long-frame sampler + `SetPWLGamma`/SQLite hitches (L33).
- ITC-Recomp (Import Tuner Challenge): its frame interpolation has no performance merit for FM3 (FM3 already
  simulates every frame with a real dt; a blended frame costs the GPU the same; ITC needed it because its game has
  a fixed 30 Hz step). Leads kept: untile by runs + SIMD swaps (L34), texture change checks (L35), a per-frame-state
  finder as a correctness check of our frame unlock.
- Six feature studies (RESEARCH.md, session 22 rows in the index): upscalers/frame generation (FSR 4 INT8 on the
  user's RX 7000 via FSR SDK 2.4; DLSS/DLAA code exists; XeSS; frame generation large), FPS counter + graph (a ReGPU
  patch beside the timer), the two splash screens (**need the user's screenshots**: the boot sequence is already
  cut), triple screen over a virtual LAN (FH1R's design; networking is stubbed today), single-instance triple screen
  (flat 48:9 → homography warp → three projections), engine sound (**we apply no XMA trims: port FM2's trim
  handling first**, then hl360 comparison, then a loop table).
- User: the two screens left are the **legal text and ESRB**; our skip already shortens their entries, so they likely
  cover the startup load (RESEARCH VR section, addendum: log the entries + file reads to tell). VR study after FH1R
  (RESEARCH "VR"): virtual screen → head-tracked cockpit → true stereo in ReGPU; first step OpenXR on D3D12.
- **Queued (user request): start and pause on the CrownJewel career race grid by itself** (`grid_test=1` /
  `FM3_GRID_TEST=1`, launcher "Start on the test grid"). The user gets there by pressing A repeatedly from the
  title/main menu until the countdown. Plan (local, needs building and a game run; nothing written yet):
  1. New tour steps in `imports_xam.cpp` (the `quick_race` machinery): `A_UNTIL:<signal>` (A every ~1 s until the
     signal), `UNTIL:<signal>`, then `START` (pause). Timed presses alone can't do it: an A after the pause would
     pick the pause menu's first item and resume.
  2. Signals, from hooks we already have: **race load started** = the AI driver setup `sub_822A1150` called
     (`ai_drivers.cpp`); **load finished** = `frame_time.cpp` saw the main loop's >4 s load frame and then ~2 s of
     normal frames. **Countdown started**: not known yet. Lead: the code that reads the developer option
     `ShowPreRaceAnd321InCmdlineLaunch` (+1083; `xref.py readers`) decides whether the pre-race show and the 3-2-1
     run, so it sits next to the countdown's start; alternatively a call trace while the user presses A into the
     countdown. Fallback: after "load finished", A every 1 s for N presses (measured once), then START.
  3. The save: CrownJewel stays the next career race only while the profile isn't advanced. Run on a copy of the
     Baseline save restored before each run (`out/profile-backups/`), or check the route still lands on CrownJewel.
  4. Log each step (`tour: ...`) with the time, so a missed press shows in the log.

### 2026-11-10 — session 21 (cloud: PERF-PLAN.md, installer study, AI-driver benchmark / frozen grid study)
- PERF-PLAN.md: every unbuilt/unmeasured performance item by picture effect, target machine, live/start-time A/B.
- RESEARCH.md: half-rate shading (D3D12 VRS) for weak GPUs/iGPUs: design, GPU coverage, FM3 exclusions; PERF-PLAN V2 updated.
- RESEARCH.md: telemetry out to SimHub (Forza Data Out, FM7 311-byte Dash over UDP; needs the car-physics RE shared with the AI-control study; FM3's telemetry-page getters are the shortcut).
- RESEARCH.md: two new studies (installer for a public release; benchmark tools: AI on the player car, frozen grid). No code change; nothing checked against FM3's code (no `out/ppc` in the cloud container): first local steps are listed in each study.

### 2026-11-11 — session 21b (cloud research: Anti-Lag 2 and Reflex)
- Read ReGPU's present path (flip swap chain, 3 buffers, no latency waitable, `kFrames = 3` GPU slots), the vblank/
  `VdSwap`/limiter code and our `XamInputGetState`; checked the SDKs (AntiLag2-SDK header, NVIDIA/nvapi `nvapi.h`, both
  MIT). Result and plan: RESEARCH.md "Input latency: AMD Anti-Lag 2 and NVIDIA Reflex". No code, nothing built.
- FH1R's "Use FIFO latest ready" (their SDK patch 0070, Vulkan `VK_PRESENT_MODE_FIFO_LATEST_READY_KHR`): read and
  mapped to DXGI in the same RESEARCH section.
### 2026-11-10 — session 20 (render thread profile, race vs time trial, alpha-test-free shaders)
- User asked what "the render thread runs flat out" meant. Rechecked: the 96-98% is GetThreadTimes CPU time, which counts
  spinning. Profiled t7 (`FM3_SAMPLE_THREAD=7`, temporary `play.cmd` line, removed; callers on live with
  `Fm3 "env FM3_SAMPLE_CALLERS 1"`) on the paused CrownJewel grid, stock, 203 fps: ~65% in D3D's ring-space spin
  (`sub_824B4F78` -> `sub_824AAA68` from `sub_824B5048`/`sub_82AF4AA8` and the present path `sub_824AF280`), ~2% asleep
  (condition variable), ~33% game work spread thin (no hotspot over 1.1%). So t7 waits on ReGPU; the limit is ReGPU's draw
  thread or the GPU (likely the GPU at 1440p 4x, since draw cuts moved nothing). NOTES top, OPEN-ISSUES L and CLAUDE.md
  corrected. Log `out/logs/s20_t7_profile.log`. Open: a 720p 1x run of the same grid would tell draw thread vs GPU.
- Race vs time trial (user asked, to find the ~100 fps difference): race grid 198 fps / 3,630 draws / GPU 4.80 ms vs time
  trial 311 / 1,635 / 3.1. Then with `REGPU_GPU_SHADERS=1` (temporary `play.cmd` line, removed) and the time-trial car
  moved back to the 8th grid spot (same view): GPU 4.80 vs 3.33 ms, ~1.3 ms of the 1.47 are the AI cars' own material
  shaders in the main scene (~0.19 ms per car). Details OPEN-ISSUES L. Next ideas: the speed presets in a race (cheaper
  car shaders), 720p/1x to confirm pixel cost.
- Duplicate-draw check (user asked): none worth fixing (`tools/analysis/dupdraws.py`, capture with `REGPU_TRACE_VC`).
  It showed every translated pixel shader carries the run-time alpha test's `discard`. New ReGPU patch
  `regpu-split-alpha-test`: `REGPU_SPLIT_ALPHA_TEST=1` uses a variant without it for draws with the alpha test and
  alpha-to-mask off (`NoKillShader` in `renderer.cpp`, like the motion variant; pipeline flag 0x400). Race grid, paused,
  4 interleaved rounds: off 199.4-205.1 (avg 203.0), on 211.4-216.0 (avg 213.6), +5.2%; on/off screenshots
  byte-identical (`tools/analysis/pngdiff.py`). On by default in `src/gpu_host.cpp` (`FM3_ALPHA_KILL_ALWAYS=1` off).
  User drove ~3 laps with it: no visual problems; the variants compiled on that first run (saved to the shader cache).
  Not yet seen on other tracks or in menus/photo mode.

### 2026-11-10 — session 19, part 4 (LOD tests in races, perfmode sanity check, wrap-up)
- With the user driving to the CrownJewel grid each time: keepLOD0Car (works), trackfardistance (dead), TrackSettings x2
  (all crowd), dev overlays (cut; no input combo: never read even in a race), perfmode (menu car body missing, fallback font;
  proven by restart, not the vertex arena: live arena off changed nothing), fastcrowdrender, fastcarrender, crowd x0.5,
  perfmode in a race, then a perf-log sanity check (run-to-run spread 198.8-206.0 fps; the "t7 bound" read was corrected in session 20). Results:
  OPEN-ISSUES L. User notes: stop the attract video (to do), test grass on another track later.

### 2026-11-10 — session 19, part 3 (LOD research toolkit)
- User asked for a way to research FM3's LOD levels first. Built and boot-tested: loose-file mods in `imports_io.cpp`
  (a no-op mod of all TrackSettings loaded for the menu's UIWhite/UITrack scenes, then removed), `lodsettings.py`
  (list/mod/fields), `xref.py` (strings/refs/func/floats/options/readers; `readers launch` reproduces session 17's 8
  readers), watchpoint check on `launch` (3 reads in `sub_8225CDE0` at boot) and `trackfardistance` (none at boot/menus).
- Research plan and every lead: OPEN-ISSUES L.

### 2026-11-10 — session 19, part 2 (vertex arena from FM2, on by default)
- Ported FM2's vertex arena (commits a6edeb7..f893212 read for the design): ReGPU patch `regpu-vertex-arena` + `Host::watchArm/
  watchClean/watchSeq`; FM3 `src/memory.cpp` write watch (FM2's, MIT), `GuestWillWrite` before NtReadFile's ReadFile.
- First boot: "guest memory can't be made read-only (error 87)": the heap opened over the 0xA0000000 view locks its protection.
  Fix: the heap over a separate MapViewOfFile view of the same section (ReGPU `HeapViewAddress`, `REGPU_GUEST_VIEW_HEAP=1` = old).
  Second boot: arena on, zero-copy kept, verify clean on the title screen. Race test pending (user).

### 2026-11-10 — session 19 (speed presets: Turbo/Hyper pieces in ReGPU, launcher slider; ReGPU branch survey)
- The user shared a 4-page ReGPU study of FM2 at 720p on a Ryzen 3 5300U (Turbo +21.8% with the same picture; Mild, Potato,
  Low, Extreme +44 to +71% trading picture). Code not public: implemented each piece ourselves (patch `regpu-turbo-hyper`,
  table in patches/README.md) and a 6-stop launcher slider (`perf_preset`). Details at the top.
- Our interpretations: "lean fetch v2, constants baked" = per-slot plain-texture flag computed on the CPU, one sample in the
  shader; "v3 plain multiply" = IEEE multiply in pixel shaders (the study's global version garbled a HUD); "10:10:10:2
  copies" = FM3's 7e3 scene/reflection copies (destination 2_10_10_10 with bias -2/-3, most of FM3's resolves) as
  R10G10B10A2 instead of RGBA16F; "helper surfaces" = passes narrower than the screen; "frozen helper passes" = those drawn
  every n-th frame; "A2M as test" = the 2x2 dither with a 0.5 threshold.
- Boot test (Extreme, menus): see the top. Two cull faults found and fixed with shot bursts measured by a small text-pixel
  counter (scratch script). Game closed with CloseMainWindow each time (saves on exit as normal).
- Survey (subagent, read-only git): none of the Turbo/Hyper code is in any public branch; our pin already has the
  rexglue-driver and most engine-optimisation work. Candidates: RESEARCH.md.

### 2026-11-09 — session 18 (cloud: GitHub issues + sibling-port check, no code change)
- GitHub: no issues filed on `braaap-stututu/FM3-ReGPU` (0 open, 0 closed). Checked the FM2 and FM4 ports' commits/notes against our open list; leads in OPEN-ISSUES.md "Sibling-port check". Best lead: **C1**: FM2 found the game reads DLC DB patches from a folder named after each DLC zip (`dlczips\<zip>\media\db\patch\`), which never existed, so no DLC patch ever ran (FM2 `eda8833`). Check with `FM3_TRACE_FILES=1` whether FM3 has the same gap with Disc 2. Also: D might be FM2's photo-mode composite clipped to one 4x MSAA tile (256 rows at 720p 4x); G: FM2 decodes engine streams per stream with no seek guard.
- New `RESEARCH.md`: 16-bit shaders for low-end GPUs (pixel shaders only, opt-in; vertex index and positions need 32-bit), music stopping for the session (hl360 fine; lead: no APC support in `NtReadFile`/`NtSetTimerEx`; check the log for `with an APC routine`), local multiplayer (untested; one signed-in user).
- RESEARCH.md: AI cars 200 vs time trial 300 fps. Lead: ReGPU's tile merge (drops the second EDRAM tile's copy of each scene draw; PGR4 +19%, FH1R's one-pass scene +15%) is off with the draw thread, FM3's default, so AI cars are drawn twice. First: `FM3_NO_DRAW_THREAD=1` + grep `tile merge: surface`. Plus iGPU/APU options (upscaler + render scale, UMA defaults, frame cap, thread gating, sleeping GPU waits) and a `fsr4` settings bug.
- RESEARCH.md, four more studies: (1) GPU choice in the launcher (ReGPU takes the default adapter; plan: `REGPU_ADAPTER` in high-performance order, per-adapter pipeline library, Optimus/PowerXpress exports, launcher list via DXGI P/Invoke); (2) RAM/VRAM/bandwidth (RGBA16F + D32S8 render targets at 2x the console's bytes, 4-pass texture uploads, 3x128 MB upload rings, no texture budget or memory-budget log); (3) ultrawide (ReGPU's Hor+ and HUD squeeze exist; missing: FM2-style camera-aspect hook for culling, found with `FM3_SCAN_FLOAT`); (4) shaders (FM3 uses neither `staticShaders` nor `pipelineSeed`; port FM4's `.fxobj` scan, seed from manifests; tool list incl. XenosRecomp, RGA, RenderDoc).
### 2026-11-10 — session 18, music fixed (user-confirmed, menus and races)
- **Two causes, both in our XMA decoder (`src/audio/xma.cpp`):**
  1. **Codec pool:** the music streamer (thread `sub_828E2E60` → `sub_827EAD40`: reads 4 x 128 KB, seeks to the last 128 KB boundary, reads it) starts a track by enabling/disabling its context `A1001900` in a tight loop, expecting the hardware to decode at once; it gives up after ~2.4 ms (386 tries). Each try found no codec in the pool (FM2's pool, refilled by the decoder thread every 2 ms), so it only waited: the earlier `386 pool waits, 0 packets decoded`. Fix: a context stepped 8 times in a row while waiting gets its codec opened in place (`CodecReady`); an engine start is stepped once or twice, so the race start keeps the pool. `FM3_XMA_POOL_WAIT_ALWAYS=1` = old behaviour.
  2. **Seek guard:** music arrives one packet per input buffer, alternating 0/1, in file order; the guard flushed the codec at most switches, the next packet then decoded to 0 frames, and the title gave up on the track (~40 s in). Fix: no guard for streams recognised as music (`d.music`); `FM3_XMA_GUARD_MUSIC=1` restores it. Check boot: 0 music flushes, 247 packets with frames, file reads continue (offset 524288+).
- **G (engine cut-outs) A/B:** guard on vs off at a steady ~5,000 RPM (virtual pad: clutch LB + right trigger 0.45; the game's pad throttle idles below ~0.3): ~59 vs 0 flushes per 5 s, but the user heard no difference and no cut-outs on this car (RSX-S). The guard is not G's cause; details in OPEN-ISSUES G.
- **Engine loop crossfade (experimental, off by default; launcher Testing > "Engine loop crossfade" = settings.ini `engine_crossfade=1`, read by play.cmd runs too; live `Fm3 "env FM3_XMA_LOOP_CROSSFADE 1"`):** our port of FM2's idea, fitted to our decoder. An engine sound loops by the title feeding the stream's first packet again; the seek guard then replaced the codec and dropped the audio FFmpeg still held back (about a packet) plus anything unplayed, so each pass started on a step. With the switch: a restart whose packet equals the stream's first packet (full 2 KB hash) drains the old codec, keeps its last 384 frames and blends them into the new pass's first frames (raised cosine, ~9 ms; `Decoder::LoopFade`/`BlendFade` in `xma.cpp`); other restarts are unchanged. Counted as `xma: 5 s: N engine loop restarts crossfaded`. Not FM2's offline seamless-loop table (FM2 data; FM3 would need its own from its `.fsb` banks). Not yet heard in a race.
  - **Result, same session:** the first-packet match never fired (trace `FM3_TRACE_XMA_RESTARTS=1`: FM3's engine streams are fed one packet per buffer and "restart" after 1 packet; restarts are never on one of the stream's first 8 packets). Changed to crossfade **every** guard restart (launcher label now "Engine sound crossfade"; log `stream restarts crossfaded`, ~43 per 5 s at held revs). The user heard it **worse**: without it the engine at steady revs goes "AAAAEEEEOOOO" (a slow tone sweep), with it "AAAEAEAEEEEEEOEOOEOOOOOAOAA" (old and new pieces alternating). Off by default; kept only as an A/B toggle.
- The virtual pad (`hold LB`, `trigger R <0-1>`, `release all`) can hold steady input for audio tests; the user's own stick/trigger noise made that hard.
- `FM3_TRACE_READS=<text>` now logs each matching read's and seek's host call stack (recompiled function chain) and event handle.
### 2026-11-10 — session 18, local part (DLC DB patches found never applied; one-boot test prep)
- Merged the cloud branch (RESEARCH.md etc.) into `main`.
- **C1 lead confirmed:** a menu boot with `FM3_TRACE_FILES=1` showed `attributes not found DLCRootN:\media\dlczips\<zip>\media\db\patch\<file>` for all 15 DLC patch files: they sit only inside each `<zip>.zip`. So no DLC DB patch ever ran on our build: the `*_execute.sql` scripts set `IsInstalled=1` for 199 DLC cars (the base `gamedb.slt` has them at 0) and Disc 2's tracks; the `*_merge.slt` files carry car data (Data_Car, engines, upgrades, decals) and Disc 2's track rows (SplitPoints, StartGridPositions, ...). Fix (`imports_xam.cpp` `UnpackDlcPatches`, as FM2 `eda8833`): unpack those members next to each zip once (stamp `.fm3-db-patches`); the next boot opened all 15 in order (merges, then executes) and reached the menus. `FM3_NO_DLC_PATCHES=1` removes them (C1 A/B). AIPlayerID is 0 for every car on disc and in the merges, so the link to driver 125 is not proven: a race test decides. Save backup before the first patched boot: `out/profile-backups/s18-before-dlc-patches`.
- FM3_* LiveSwitches now follow their env var (thread, 4x/s, only with `REGPU_DEBUG`): `Fm3 "env FM3_NO_XMA_SEEK_GUARD 1"` flips it live (tested with `FM3_NO_XMA_BOOST`).
- New log: DXGI adapters at start (this PC: 7900 XTX listed twice, the Ryzen iGPU `1002:13C0` 485 MB, WARP) and `gpu memory:` every 30 s (menu: 3.4 GB local, 1.0 GB shared, 7.0 GB private bytes). `upscaler=fsr4` now runs as FSR 3.
- Live vs start-time (for planning A/Bs): tile merge needs `FM3_NO_DRAW_THREAD=1` at launch, then `REGPU_NO_TILE_MERGE` is live; `REGPU_NO_ANISO` is not really live (static samplers built once); `REGPU_UPLOAD_STABLE_REUSE`, `REGPU_CONSTANTS_IN_RING` are live; `REGPU_LEAN_FETCH`, `REGPU_SUBMIT_DRAWS`, `REGPU_OPTIMIZE_SHADERS` start-time.
- No `with an APC routine` line in any kept log: RESEARCH's music lead 1 looks unlikely.
- **One-boot race test** (user drove; temporary `play.cmd` lines `FM3_NO_DRAW_THREAD=1`, `FM3_TRACE_READS=Music`, removed after; log + shots saved as `out/logs/s18-oneboot-prerace-capture.log`, `s18_prerace_merge_{on,off}.png`):
  - **Tile merge: no gain on FM3.** Paused career grid with AI cars, 1440p 4x, draw thread off, ~3,560 draws: merge on 169–170 fps, off 170. The race scene surface `14010500` is refused: `tile merge: surface 14010500 clears its tiles with rectangles, not merged` (only menu/other surfaces merge). RESEARCH's AI-car lead needs ReGPU to merge scenes that clear per tile with rectangles first.
  - Same grid, live: `REGPU_UPLOAD_STABLE_REUSE=1` 173.6 fps (~+2%, small; risk: a buffer one frame stale), `REGPU_CONSTANTS_IN_RING=1` 133.7 (−21%: keep off).
  - **Band D** in the pre-race menu: the top part is not darkened like the rest; its edge is at y 512 of 1440 = **row 256 of 720 = one 4x MSAA EDRAM tile**. Same with merge on and off. Capture taken at log 115.1 s (in the saved log); not analysed yet: find the full-screen darkening draw and its tile/scissor.
  - **C1:** one career race (quit before the end) with the DLC patches in: no `ai drivers:` line at all (the workaround logs only an empty/missing driver). Not conclusive (C1 hit only some loads); keep watching.
  - **Music: none from the start.** The game reads each track's first 4 x 128 KB, then one chunk at the file's last 128 KB boundary (`Qemists_OnTheRunVocal.xma` 0..393216 then 5767168 of 5881108; `UI_BaseTelemetry.xma` likewise to 7995392), then never reads that track again, not even after a race restart. Reads use the file position (no offset pointer), so the game seeks to the end itself. Turning off `FM3_NO_XMA_SEEK_GUARD` and `FM3_NO_XMA_DROP_AHEAD` live changed nothing (no new reads). So the streamer stops asking for data: next, compare with hl360 (does it read the same tail chunk, then continue at 524288?), and look at what the title waits for after the tail read (event/XMA context state, `XMAGetOutputBufferReadOffset`/input-buffer consumed flags of voice A1001900).
### 2026-11-09 — session 17 (intro skip, dev command line, Disc 2 install)
- FM2 sibling checked: intro skip shortens the timed splash sequence; "into a race" there is only a pad script (`FM2_TOUR`, ported earlier as `FM3_TOUR`). FM3: same sequencer design (hooked, `boot_options.cpp`), and Turn 10's developer command line is still parsed in retail (string table at `82008400`–`82009400`), only fed an empty string: hooked to take `FM3_CMDLINE`.
- Disc 2: subagent research (runtime mostly ready; installer missing), then launcher installer + runtime fixes; installed from the user's ISO, identical to hl360's copy; the game mounts all 8 packages.
- `launch` stalls on the loading screen (you saw it); thread dumps (new `FM3_DUMP_THREADS`) show no deadlock; variants (no car, exhibition, freerun) the same. You allowed me to restart the game for tests. Intro skip first failed (holder address mistyped as 0x82C3111C; sequencer created later; file field not text): fixed (setter wrapped from the string-copy hook, timings signature). New `quick_race` option: Quick Race through the menus by pad script, ~55 s after launch. Launcher: "Skip intro", "Start in a quick race" switches.

### 2026-11-09 — session 16 (livery on reload, shade flicker E, C1 diagnostic)
- Topic C1 chosen. Session-15 log: driver 125 read back empty on every load that picked it. Diagnostic split into one query per value (the combined one failed, -1 rows): Name is NULL in the in-memory DB. Lead: the game's DB patch system (LicenseMasks `.sql`, overwrite/merge/exec). Not solved; workaround holds.
- Livery broken on reloads: write-back skips all targeted images of earlier loads; the enclosing search took the lowest base. Fix: recent images only (patch 16 `regpu-livery-enclosing-recent`, also the per-texel write-back check). Several reloads clean.
- Shade flicker (E): 5-frame cycle measured with raw bursts; freezing resolves (`REGPU_FREEZE_RESOLVES`, live via `env`) found `159D0000`; capture showed the clear through a 4x view. Ruled out: depth packing, stale-resolve check, sun shadow maps, 64x64 ring, write-backs, occlusion answers, direct dynamic indices, texture-check stagger, the car-shadow composite (#392). Fix: patch 17 `regpu-msaa-color-clear-scale` (first version whitened the screen: limited to pitch x across < 1000).
- Tooling: patch 18 `regpu-debug-shot-dump` (shots after a capture stalled ~30 s). Logs `out/logs/session16-run1..3.log`.
### 2026-11-08/09 — session 15 (sun flare with the workers on the draw thread, crash C1 workaround)
- You picked the flare bug with pieces on the draw thread. Read patch 15: the only command-processor write to the draw thread's segment state was `seg::g_query = false` at a ZPD end (the CP runs ahead → the sun's draws went to worker lists). Fixed (`end && !work::Active()`), patch 15 regenerated (only that hunk changed), `setup.ps1 -NoBuild` OK.
- Test with a temporary `play.cmd` line (`FM3_DRAW_THREAD_PIECES=1`, removed): Montserrat sun spot, 1440p 4x MSAA: answers 3214/3214, ~257 fps, flare right (you). You noted no start grid shows the flare (memory saved).
- Made draw thread + workers the default, then A/B at 720p 1x on a paused cars grid (your settings backed up and restored, temporary `play.cmd` line removed): with workers ~195 fps, serial ~216 → serial back as the default, pieces opt-in (`FM3_DRAW_THREAD_PIECES=1`).
- Launcher: new Testing toggle **"Draw workers"** (`draw_workers=1` in settings.ini → `FM3_DRAW_THREAD_PIECES=1` at launch, off by default) for your 1440p 4x MSAA racing A/B with the GPU overlay: **not much difference on your hardware** (GPU-bound there); you keep the toggle in case it helps weaker CPUs.
- Crash C1: a hook for the generic pool (theory: pool empty) did not fire on the next crash; the id was the car's own driver (`Data_Car.AIPlayerID`). Driver 125 has its row but read back an empty Name; hooks now give such a car a generic driver and stand in for an empty name (a first stand-in also took a car-history key: your "lore drop" driver name, fixed to driver names only). Peeked the in-memory database (`find` + `peek`, decoded the SQLite page): intact at the menu and after two loads. Added row diagnostics for the next occurrence. Logs `out/logs/session15-c1-*.log`.
- Your play test also showed livery breakage on some loads (write-back skips in the log) and an alt-tab hang: both written down as leads.
- Hand-off: NOTES top rewritten ("Start here next session" with steps per topic), work plan updated, `tools/analysis/sqlpage.py` added, CLAUDE.md updated. GitHub had two README edits of yours; my commit was rebased on top of them.

### 2026-11-08 — session 14 (sun flare with draw workers, glare size, draw thread default)
- You picked "sun flare + speed". Compared PGR4's vendored ReGPU with ours: same `seg::` structure (gpu.cpp differs by ~110 lines), PGR4 adds `kQuery` log records, thread-local query state and a direct path (`seg::g_query`) for queries outside a logged segment. Its logged path does not fit our tile spans (FM3's sun query = on/off pair per EDRAM tile, the end only on the last), so I used only the direct idea: flush at a ZPD start, draws direct while counting, workers between spans. Coverage draws: workers excluded (`!C().worker`), since they read the command processor's global query state.
- Patch `regpu-worker-queries` (gpu.cpp, renderer.cpp/.h), stack verified with `setup.ps1 -NoBuild` (13 patches).
- Test (you, `launcher.cmd`, 1440p 4x MSAA): flare back (timer 250.8). Live A/B on the paused grid: 225 / 219 (workers off) / 232 (fixed answer) fps. You: flare strength the same with workers off; maybe stronger than console (open).
- Glare size: hl360 has no flare; capture ruled out a double count; with a live answer multiplier (patch `regpu-zpd-scale`) the glare turned out to follow the raw count (none ≤0.5x, max ~1.5–2x); you matched the YouTube console clip live: 0.6. A one-off `std::out_of_range` guest throw on a track load (did not repeat); guest stack logging added.
- Per-draw CP cost (you picked it, then "structural change"): profiled t7/t9 (temporary `play.cmd` lines, removed; the sampler now lists 60 lines), found the perf log's own cost, piece counts irrelevant; the draw thread beat the workers; wrote patch 15 (queries with the draw thread, then segments on the draw thread); A/B at 720p 1x no MSAA 400 cap (settings restored after): workers 175, draw thread + workers 202; the latter lost the flare in a race, so the default is the draw thread alone (flare OK, Montserrat spot 240 = cap). The AI-driver crash came back once with a stack (OPEN-ISSUES C1).
- Removed the launcher's "Accurate sun flare" toggle (Install.cs, app.js, index.html) and `accurate_sun` in `src/gpu_host.cpp`; game and launcher rebuilt.
### 2026-11-08 — session 13 (racing wheels; frame-rate unlock)
- Wheel support ported from the FM4 fork's `wheel-support` branch: new files applied as-is, the changed files (`imports_xam.cpp`, `settings.*`, `main.cpp`, `CMakeLists.txt`, launcher `Bridge.cs`/`Program.cs`/`app.js`/`index.html`) merged by hand (our versions differ slightly), fm4→fm3 names. Checked that FM3 looks up the same force-feedback ordinals (`sub_82AB6AE0` callers in `ppc_recomp.169.cpp`). Probe mode starts; launcher builds. Commit `6d217bb`.
- Frame-rate research (a subagent read FM2, PGR4, FH1R, the FM4 ports): details in "Session 13" at the top. Implemented FH1's frame-time scheme for FM3 (own code; FH1R is GPL-3) and the vblank-follows-cap rule. Untested in game.
- Later: tested the unlock (120, then 240), profiled the near-cars slowdown, read ReGPU/PGR4/FH1R for CPU/threading fixes (OPEN-ISSUES P), then A/B-tested and enabled no-fence-order, the two per-draw switches, early fences (after finding and patching the ReGPU write-back fence bug) and draw workers (with the "Accurate sun flare" option). Commits `6155b95`…`4e63e41`.
- **Record as written during the session** (in the order things happened; later bullets correct earlier ones, e.g. the early-fence "rejected" note was an invalid A/B):
- **Racing wheel support** ported from your FM4 fork's `wheel-support` branch (commit `88c12e9`): `src/input/wheel/*`, `src/kernel/imports_ff.cpp` (force feedback), the XAM input changes, `fm3.exe --wheel-probe`, the launcher's **Wheel stuff** tab, `tools/wheelprobe`, `docs/WHEELS.md`, `NOTICE-wheel.md` (FH1R's BSD-3 wheel code). FM3 has the same XInputFF library as FM4: `sub_82AB6AE0` looks up xboxkrnl ordinals 642–649 (`XInputdFF*`) and xam 442/520 at start, which the ported `HostProcedure` answers. **Test:** wheel plugged in → launcher Wheel stuff tab (devices, live view, bindings) → race; log `Wheel:` and `XexGetProcedureAddress(xboxkrnl.exe, 282..289)` lines.
- **Frame-rate unlock** (`src/hooks/frame_time.cpp`, `src/gpu_host.cpp`), after FH1-ReImagined's `docs/framerate.md` (FM3's main loop and vblank counter match FH1's, which is FM4's engine). FH1R is GPL-3, so this is our own code from its description, not a copy. FM3's main loop `sub_821F5DE8` runs once per guest vblank (vblank callback `sub_822580C8`; options `mainthread30fps`/`frontend30fps` off by default, object `0x82C21C60`) and takes dt from the ms tick (16/17 ms snapped to 1/59.94, >4000 ms → 16 ms). So: guest vblank = the frame cap (`max_fps`, lowered to the display refresh with vsync on; 60 by default → **nothing changes at 60**), and above/below 60 the main loop's `sub_821F6AF8` call (`lr 0x821F6004`) answers true with f31 = dt from the vblank clock (nominal vblank time if the frame started within 2 ms of it, else the start time). Live switches: `FM3_GAME_FRAME_TIME` (A/B: the game's ms dt), `FM3_FRAMETIME_LOG` (dt stats every 10 s). `FM3_VBLANK_HZ` still overrides. The old Q24 "2× vblank = 2× speed" rule was never measured; this assumes game time follows the clock (as FH1 measured).
  - **Tested at 120 (session 13, 1440p 4x MSAA, vsync off):** first build (vblank = cap, divider 1): main loop 120 but **60.0 fps drawn**: with `mainthread30fps` off the render thread draws every second main frame (poking `0x82C2209E`=1 live made the vblank counter `0x82C21C3C` release every 2nd vblank and every frame was drawn). Fixed with **divider 2** (vblank 2× cap, the vblank hook sets `mainthread30fps` before each check; `FM3_VBLANK_DIVIDER=1` for the old way). Then: main loop **120.0 fps, dt 8.332 ms sd 0.002, 0 late** (`FM3_FRAMETIME_LOG`; the game's ms dt would be 0.8 ms rms off); drawn fps follows draw count: ~3,200 draws 71–75 fps, ~2,500 85, ~1,900 105 (≈2.7 ms + 3.5 µs/draw); guest t7/t9 at ~100%, CP waits on guest memory most of the time. **You: smooth, but fps drops from 120 when other cars come near.** Next lead: per-draw cost (guest render thread vs ReGPU's per-draw work); A/B 720p no MSAA to rule out the host GPU. Render sync (FH1) not needed so far (no judder/HUD loss reported). Debug-server `switch` only takes `REGPU_*`/`PGR4_*` names, so the frame-time switches are env vars re-read every second (`Fm3 "env FM3_FRAMETIME_LOG 1"`).
  - **Why drawn fps drops near cars (profiled, session 13; logs `out/logs/session13-t7profile.log`, `session13-drawthread.log`):** the game's render thread (t7, entry `822BDE68`) spends ~86% of its samples in D3D's ring-buffer wait (`sub_824B4F78` -> spin step `sub_824AAA68`: wait until the GPU read pointer, device +10896, passes a position; called from `sub_82AF4AA8` and the present path `sub_824AF280`). ReGPU publishes the read pointer per packet, so t7 waits on ReGPU's command processor (t9). With the default single-thread CP (parse + translate), t9 ~95% CPU but ~50% of it spinning in WAIT_REG_MEM on mem `18C27014` (swap 2, fn 3 "equal"; plus `18C27000/04`): a value the game writes, so CPU and CP take turns. A/B `REGPU_DRAW_THREAD=1` (translation on a worker): ~70 fps at 3,200–3,600 draws vs 71–75 at 3,200 before (≈3.0 vs 3.5 µs/draw, marginal); worker only ~42% busy, CP 40% spin-waiting on `18C27014`, 32% in drains. **Nothing is saturated: it's a latency/turn-taking chain (render thread ↔ CP ↔ worker ↔ the game's fence write), growing with draws.** Next leads: who writes `18C27014` and when (guest thread, after which wait), per-draw translation cost in ReGPU, CP spin/drain latency. Left `REGPU_DRAW_THREAD` off (no clear gain).
  - **Draw workers on by default; no sun flare unless `accurate_sun=1`** (launcher Graphics "Accurate sun flare", ~10% fps near cars; your choice). Behind an AI car 142 → 155 fps. To fix both: port PGR4's logged occlusion queries onto `regpu-zpd-tile-spans` (OPEN-ISSUES P).
  - **Later in session 13: early fences ARE the default now**, with our 12th ReGPU patch `regpu-early-fence-writebacks` (liveries intact); plus `REGPU_TEXTURE_CHECK_STAGGER` + `REGPU_DIRECT_DYNAMIC_INDICES` defaults. Paused start grid (1440p 4x, 240 cap): 108 → 147–153 fps. The "rejected" note below was based on an invalid A/B. Details: OPEN-ISSUES P.
  - **`REGPU_EARLY_FENCES` rejected:** +5–8% at a 240 cap but breaks liveries into noise (fence before the livery write-back); left off (OPEN-ISSUES P). settings.ini is now `max_fps=240` (~135–170 fps near cars).
  - **`REGPU_NO_FENCE_ORDER` is now FM3's default** (`FM3_FENCE_ORDER=1` undoes): near cars 69–77 → 100–120 fps, no visual change (OPEN-ISSUES P).
  - **Performance research (read-only, session 13 end):** ReGPU branches/forks, PGR4/FM2/FM4 vendored copies and FH1R reviewed for CPU/threading fixes: **OPEN-ISSUES.md issue P** has the ranked tests. Top: live `REGPU_NO_FENCE_ORDER=1` (PGR4 86 → 147 fps), then `REGPU_EARLY_FENCES=1` (+ draw workers later); FM3 sets none of these switches today.
  - Your settings.ini is now `max_fps=120` (was 360, which used to be ignored and now really means 360).
  - **Test:** `max_fps` 120 (launcher Graphics), race; I attach and `switch FM3_FRAMETIME_LOG 1`; compare the race clock/countdown with the on-screen timer over a lap (does game time keep real time?), 0–100 feel, menus (auto-repeat speed: PGR4 needed a hold delay), HUD dropouts or judder (FH1's "render sync" if the render thread falls behind), crowd/garage camera speed (FH1 fixed both), gear taps. Then 240/360.
  - Not ported yet (do if the test shows the need): FH1's render sync (main loop waits for the render thread), divider-2 pacing, the side-effect fixes (fixed 1/30 s steps: search `.rdata` for `3C888889`/`3D088889`), PGR4's menu hold delay and gear latch. Research notes: FM2/PGR4 uncap with ReGPU's `max_fps` + 2× vblank alone (their games keep time by the clock); FH1R `forza-recomp/src/hooks/frame_time.cpp`, `docs/framerate.md`; PGR4 `src/hooks/gear_latch.cpp`, `src/kernel/imports_xam.cpp` (menu hold). Clones: `reference/sibling-recomps/{PGR4-ReGPU,ForzaHorizon1-ReImagined}`.

### 2026-11-08 — session 12 (launcher, from the FM4 fork)
- Ported the launcher from your FM4 fork (`braaap-stututu/Fuzzy-Rittersport-4-ReGPU-testing`, `launcher/FM4Launcher`, commit `69bdae44`; reference clone in `reference/sibling-recomps/Fuzzy-Rittersport-4-ReGPU-testing`) to `launcher/FM3Launcher`. Installed the **.NET 9 SDK 9.0.320** (winget `Microsoft.DotNet.SDK.9`). Builds with 0 warnings; page checked in the browser pane with a stand-in host (all tabs, no script errors); the real window opens. You then tested it (below): works.
- Why it ports cleanly: our `src/settings.cpp` is the same file as the fork's (same 14 `settings.ini` keys), and the profile layout (`profiles\<name>\content` + `xuid`, FNV-1a name fallback in `imports_xam.cpp`) is identical, so `Profiles.cs` is the fork's minus photos.
- Kept (your pick): Play (game folder pick + SHA-256 check of `default.xex` against `aa15372c…8742`, language from the Disc 1 string tables, Play), Graphics (only what our ReGPU has: resolution, aspect, fullscreen, frame cap, VSync, MSAA 1/2/4, FSR 1/3 + quality + sharpening, anisotropy, shader-box toggle), Settings (shader cache, folders), Profiles and backups. Added for our workflow: test timer (`FM3_NO_TIMER`), debug server (`REGPU_DEBUG`), performance log (`REGPU_PERF`) toggles.
- Left out for now: game import (ISO/drop), DLC, photos, Reports, Updater, Linux/Proton; FSR 4, SMAA, shader optimiser, debug views and fast mode (our ReGPU pin lacks them or they're untested on FM3: `REGPU_EARLY_FENCES`). Enhance and skip_intro are read by `settings.cpp` but not used by FM3 yet, so not offered. No icon yet (FM4's is game art from its xex).
- **Your launcher test (session 12):** no launcher issues; 2560×1440 + 4x MSAA run fine. Two new observations in that run (VSync off, 360 cap, perf log on): (1) **car thumbnails in My Cars looked wrong** (screenshot at 302.5 s: white smear right of the selected car's thumbnail); (2) **race frame dip**: 159–163 s at 30 fps (~3,600 draws/frame, renderer draw only ~5.5 ms, two host threads at 100%, "waiting on host GPU 0"), plus single 50–67 ms frames at 135/137 s; and in a menu at 47–58 s, 183 ms frames each with a 2048² livery write-back. **A/B at 1280×720, MSAA off, same route:** no race dip (no second under 59 fps between ~89 and 213 s, same ~3,600 draw peak), thumbnails fine, no 183 ms menu frames (only 8 livery write-backs that route). GPU use was 40–50% in both runs, so not GPU-bound. Not yet split between 1440p and 4x MSAA; one run each, so the race dip could also be chance. Note: `fm3.log` is overwritten each run; the 1440p log is gone (numbers above are from it).
- **Thumbnail ghost fixed (you confirmed):** at 1440p (MSAA on or off) each My Cars carousel thumbnail showed a faint second, larger car. Cause: the car's picture texture (8888 512×256, fetch dword 4 = `0x43` → mip levels 0–1, mip address = base + 0x80000) holds the **512×256 picture at level 0 and the 256×128 side-view thumbnail at level 1** (the two picture write-backs of issue C). The carousel draws it small enough that the console always picks level 1; at 2× the host's derivatives are half, so it picked between 0 and 1 and blended the big picture in. Found by: vs edit tinting by transform (ghost = same draw as the thumbnail), ps edit `float4(r2.xyz, 1)` (the ghost is in what the GPU samples; memory copy clean), fetch decode. Note `ps` edits name registers `r2`, not `r[2]` (compile error → draw silently skipped; check the log for `failed to compile`). Fix: **11th ReGPU patch `regpu-guest-lod-short-mips`**: textures whose mip chain stops early (smallest level ≥ 64 texels) get LOD bias + log2(draw scale) above 1×, i.e. the console's mip choice; full chains keep the sharper host pick. `REGPU_NO_GUEST_LOD_SHORT_MIPS=1` (live) off.
- **Race dip not reproduced:** 1440p MSAA off: no second under 59 fps in the race (peak ~3,500 draws), as at 720p. The dip (159–163 s, guest threads at 100%) was seen once, only in the 1440p + 4x MSAA run; you couldn't reproduce it. The 30 fps stretches in every run are the race intro/countdown and loading (game-native). Open, low priority: if it comes back, note the time and keep `fm3.log` (the launcher keeps the previous one as `fm3.prev.log`).
- How it starts the game: `build\rel\fm3.exe <game dir>` in `out\run` (so `fm3.log` and the debug server's files land where `play.cmd` puts them), `FM3_PROFILE` removed so the Profiles tab decides. `launcher.cmd` (dev) sets `FM3_GAME_DIR` (hl360 Disc 1) and `FM3_USER_DIR=out\user` like `play.cmd`, and seeds `out\user\settings.ini` with `profile=Baseline` if missing. Note: graphics settings saved by the launcher also apply to `play.cmd` runs (same `settings.ini`); `play.cmd`'s `FM3_PROFILE` still wins for the profile.

### 2026-11-08 — session 11 (B2, B and H fixed; VINYLISSUES.md)
- End of session: all vinyl/livery knowledge moved into **VINYLISSUES.md** (livery pipeline, fixes, switches, saves, tools, leads, lessons); NOTES.md's "Start here" points there. `play.cmd` defaults to the `Baseline` profile. Patches: 10 ReGPU (`regpu-enclosing-writeback`, `regpu-enclosing-big-pass`, `regpu-writeback-safety` new this session).
- **B speed test:** temporary switch `REGPU_PACKET_SPIN_US` (spin per type-3 packet, live via `env`) slowed normal frames to traced speed (a traced frame: 22,651 packets in ~1.6 s ≈ 70 µs each). At 80–100 µs (1–2 fps) the editor edits went through (layer X 7.00 → −0.50, held stick 4 s) and the vinyl stayed cut (green top 262). Short nudges with `Fm3 "capture"` also stayed cut, including one whose traced frame was a rebuild (1317–1333 draws vs 1229–1232 idle): the session-10 "traced = complete" result doesn't hold in a clean build. Switch removed afterwards (result recorded here). New script `tools/analysis/greentop.py`.
- **B2:** (1) `FM3_WRITEBACK 1024x1024` set at the pre-race screen: race noise gone but no vinyls/badges; photo mode/countdown still showed other cars' decals + noise (they use a separate close-up livery). (2) Traced a race restart: **no livery build at race load**; the 1024×1024 resolves at `01005000`/`01805000`/`01C05000` (colour base `100`) are something else (race start, photo mode). (3) Trace from boot (temporary `play.cmd` lines): the livery is built at **boot ≈22.6 s**: 2048×512 strips to `0B508000`/`0B908000`/`0BD08000`/`0C108000` (declared heights 2048/1536/1024/512, so the hook sees them as 2048×2048, 2048×1536, …: my first `2048x512` filter matched only the last strip), side-view passes, regions (1288×600, 1416×544, 1416×568, …) into the texture, then the strips again with colour base `0C0`; at ≈28.9 s the 1024×1024 livery (`04E12000`) the same way. (4) `FM3_WRITEBACK=2048x2048,1024x1024` + new ReGPU code that writes back an image again when a region is drawn into it: **no noise, no wrong decals in race, countdown and photo mode** (you confirmed); the car shows the editor's cut. Cost with `Now`: 13 × ~55 ms for the 2048 image. (5) Final: rule in `src/gpu_host.cpp` (2048×2048 and 1024×1024, 8888, top 0 → `Deferred`) and the ReGPU part queues a Deferred write-back of just the region's rectangle (patch `regpu-enclosing-writeback`, 8th ReGPU patch, `setup.ps1 -NoBuild`: all 8 applied). You confirmed no visual change vs (4); log: `livery writeback:` lines at boot, no stalls.
- Livery rule also matches the three non-livery 1024×1024 images above (written back Deferred, harmless so far).
- **Issue H (end of session):** a car vanished after painting another car; the damaged save then crashed on buying (hl360 too). Reproduced on a clean copy of hl360's save (Scion lost after buying+painting a Supra and painting the Fiat; hl360 kept it). Every losing run had my poster bug (big-pass width match on ordinary passes); with it fixed, plus an FM4-style reuse check for late write-backs and a stale written-back mark cleared, the car survives the same sequence (patch `regpu-writeback-safety`). Details in "Start here". `src/indirect.cpp` logs the caller stack on a null indirect call.
- **B solved (end of session).** Your test with big vinyls on every tab: bumpers complete, sides and hood cut → only the regions drawn as big passes (scale 0.72–0.77) were cut. Stale-resolve check ruled out live (`REGPU_NO_STALE_RESOLVES 1`: no change). A fresh capture of a rebuild (`out/logs/s11_cap4.txt`) showed the right-side compose (`ps a9a8e665892877de`) reading `t0=1400x534@0EC90000`, a **4×MSAA side-view pass resolved in three tiles** (rows 0–223, 224–447, 448–533 to dests 0x134000 apart, pitch 1408) with **no `also drawn into` line for tiles 2–3**; the region image (`out/res/0CABC000_1024x1109_v0.png`) had green to host row 182 = region row 252 = `t0` row 224 exactly. Cause 1: the big-pass image is 1378 wide (pitch·64/65 of the 1400 surface) and `ResolveIntoEnclosing` required width == copy pitch (1408). Fixed → right side whole up to the windows (green top 262 → 215, px ×2), left side bottom and a hood corner still missing. Cause 2: the last tile's rect is 8-row rounded (88 copied, 86 declared; hood pass 80 vs 76) and was refused for running past the image; now clipped. → you confirmed: ~99% covered on every side; green top 124, px 80,235. Both in ReGPU patch `regpu-enclosing-big-pass` (9th; switches `REGPU_NO_ENCLOSING_PASS_WIDTH`, `REGPU_NO_ENCLOSING_ROW_CLIP`). Live A/B by nudging shows nothing: edits recompose from 1× side views; the 4× tiled passes run at car load.

### 2026-11-08 — session 10 (repo audit, licence, hl360 launcher, vinyl issues B/B2)
- Read-only audit for going public: no game files in the repository, its history (28 commits at the time) or on GitHub (private, one branch); `.gitignore` covers discs, `game/`, `dumps/`, `out/`, `build/`, `reference/`. Licence: every related project is MIT (FM2/FM4 ports, ReGPU, XenonRecomp); added `LICENSE` (MIT, braaap-stututu); `NOTICE.md` keeps the FM4/FM2 credit.
- `boot hl360.cmd`: starts the hl360 baseline from its folder (its `fm3.user.toml`, Baseline profile), refuses if an `fm3.exe` already runs.
- hl360 has neither vinyl issue. Its race dump gave the correct race livery (DXT5 1024×1024). RenderDoc captures of hl360's editor (`out/rdc/`, scripts in `tools/analysis/`) gave its side-view pass and stencil for comparison.
- ReGPU patch `regpu-capture-vertex-floats`: traced frames log small vertex buffers in full (7th ReGPU patch).
- B investigation, chronological (several early readings were wrong and are marked):
  **B: vinyls cut along a straight line in the editor.** State: vinyl regions now reach the car texture (patch `regpu-resolve-into-enclosing`). The cut is earlier, in the game's **704×267 side-view vinyl pass** (surface pitch 720, 1×, EDRAM colour base 0, depth/stencil base `099`; resolved twice per edit with rect 0,0-704,272, declared height 267): rows ≥ ≈181 of that pass come out empty. The side view is stored upside down, so the car's **upper** body is what's missing.
  1. While you move a layer: `Fm3 "capture"` (twice; not every frame rebuilds). In the log, find the resolves with `rect 0,0-704,272` and the draws before them (stencil setup on `98d34caab368b2cc`, mask `5d606c137f9a8ddc`/`cb430dd1cfd2f804` with a k_8 1024×1024 mask, vinyl layers `7061152fe7f7fc72`/`a0d8cbb7f2f8ce9a`, n 141).
  2. Look at the pass's resolved image in `out/run/out/res` (`<addr>_704x267_v0.png`, check the file time is this run's) and find the cut row exactly.
  3. Leads, in order: (a) where ReGPU puts that pass's **depth/stencil** on the host (EDRAM depth base 0x99 = tile 153, i.e. right after 17 tile rows of a 720-wide colour surface): does something else alias those host rows, or is the host depth surface shorter than the pass? Check `C().depth` sizing/offset for base 099 and `REGPU_TRACE_TILE_PHASE`; (b) the **stencil reset** draws #72/#74 go through a 4× view (surface `05820168`, pitch 360) and are scaled by `REGPU_NO_DEPTH_VIEW_SCALE`'s path — turning that off moved the line a little, so the reset area is involved: log the reset rectangles' real vertex values (the log prints only 8 dwords; dump the whole buffer in the capture instead of `peek` later, the ring is reused); (c) compare with **hl360** on the same screen (`reference/.../hl360-forza3`, it has a vinyl editor too) to see whether its side view is whole.
  4. Already ruled out (don't repeat): the mask's content (a big vinyl stops at a straight line), `REGPU_NO_FOREIGN_MSAA` 0/1, window offset (0), `REGPU_NO_STENCIL`. (The "pass everywhere" mask test I did was invalid: that shader keeps alpha == 0, so `float4(1,1,1,1)` blocked everything.)

  **hl360 reference (2026-11-08):** hl360 has **neither** vinyl issue (same car and track; you painted every layer tab a different colour, covering all it can). Its race frame dump (`dumps/hl360/frame_0001`, decoded PNGs in `out/logs/hl360_race_png`) shows the race livery as a **DXT5 1024×1024** texture (`15349000` in that run; find it by its solid vinyl colours, e.g. `tools/dumpdecode.py decode` then rank by saturation) with every panel fully covered. Its right-side panel (upside down, window hole) has exactly the shape of the editor's k_8 mask, so the mask is right and the editor cut (B) is ours.
  In the **editor**, an hl360 dump (`dumps/hl360/frame_0002`) shows the car's 8888 2048×2048 composite as **all zero in guest memory**, like ours: hl360 keeps it (and the side-view pass) on its GPU too, so memory dumps can't compare B. For B, compare with a **RenderDoc** capture of hl360's editor (RenderDoc 1.47 installed) while a layer moves: its 704×267 side-view pass draw by draw against ours.
  **hl360 under RenderDoc (2026-11-08, `out/rdc/hl360_editor_frame*.rdc`, 5 captures in the vinyl editor; #3 = frame 2494 mid-move):** hl360's side-view pass found by its EDRAM names (`RT @ 0t, <9t>` colour, `RT @ 153t, <9t>` depth/stencil; Xenia converts EDRAM between the 1× and 4× views with full-surface transfer draws). Events map 1:1 to ours: 704/720 = #71, 730/746 (depth @0t as 4×) = #72, 756–810 (depth @153t 1×→4×→1×) = #73/#74, **828 = mask (#75)**, **861/867 = vinyl layers (#76/#77)**. Stencil per 30-row band (rows 0–299): **0 everywhere already before the resets** (events 756–799), still 0 after them (810), 1 inside the mask over the **full 0–269 rows** after 828, changed only under the vinyl after 867. So in hl360 the stencil is clean before this pass (likely the previous pass's resolve clear); in ours rows ≥≈181 keep old non-zero stencil and block the vinyls. **Next (needs a ReGPU logging change + rebuild):** log the full vertex data of small rect-list draws (#71–#74 rectangles; today only 8 dwords) and the clear rectangle each resolve applies to depth/stencil (the side-view pass resolves twice with `control 00100300`), then find which one leaves rows ≥181 uncleared. Scripts: `tools/analysis/rd_list.py` (every draw: viewport, scissor, targets) and `tools/analysis/rd_save.py` (save targets incl. depth/stencil as PNG), run with `qrenderdoc.exe --python <script>` and `RD_CAPTURE`/`RD_OUT`/`RD_OUTDIR`/`RD_EVENTS` env vars. Launch: `renderdoccmd capture -d <hl360 dir> -c out\rdc\<name> <hl360 dir>\fm3.exe`, F12 in game to capture.
  **Reset rectangles are fine** (the capture log's `rect corners` lines, which I had missed): 4× view #72/#74 = (−0.5,−0.5)–(319.5,127.5) → 640×256 in 1×; 1× #71/#73 = (639.5,−0.5)–(699.5,266.5), plus a second rectangle the log doesn't print. Full coverage either way, which is why `REGPU_NO_DEPTH_VIEW_SCALE` barely moved the line. hl360's post-mask stencil (`out/rdc/e2494/e828_stencil_vis.png`) is the car-side silhouette (mask texels from (262,270), our draw #75 gets the same UVs 0.256/0.264), rows 0–≈250, window hole ≈170–245. **Our cut is a straight full-width line at ≈181**: not a mask shape, so suspect EDRAM/surface bookkeeping (depth @153t = 272 rows below the colour; #72 resets a 4× depth at base 0, over the colour area). **Next: live switch sweep in our build** (big green vinyl, nudge between flips): `REGPU_NO_STENCIL` (redo properly; earlier attempt likely without a redraw), `REGPU_NO_TILE_MERGE`, `REGPU_NO_TILE_MERGE_LATER_CLEAR`, `REGPU_NO_CLEAR_EXTEND`, `REGPU_NO_ALIASED_DEPTH_CLEAR`, `REGPU_SURFACE_BY_BASE`, `REGPU_RECT_BACK_STENCIL`.
  **(Superseded below: the "big passes lose rows below 256" reading was wrong.)** **Session 10 correction (B): the side view is fine and the stencil isn't involved.** Fresh capture: the 704×267 side view is the full silhouette with all vinyls (yesterday's "cut at 181" on a circle was the window hole). Live sweep with nudges via the virtual stick (`Fm3 "stick L 0.6 0 120"`), green top edge measured: `REGPU_NO_STENCIL`, `NO_TILE_MERGE`, `NO_CLEAR_EXTEND`, `NO_ALIASED_DEPTH_CLEAR`, `SURFACE_BY_BASE`, `RECT_BACK_STENCIL`, `NO_TALL_VIEW`, `NO_MSAA_SCALE`, `NO_FOREIGN_TILE`, `NO_RECT_SNAP`: no change; `NO_TILE_MERGE_LATER_CLEAR`, `NO_BIG_PASS`, `NO_TALL_PASS`: vinyl gone entirely. **The loss is in the big passes (EDRAM surfaces wider than 1280): everything below their first 256 rows is missing.** The right-side region compose (draw `ps a9a8e665892877de`, pitch 1440, rect 1416×568, host scissor 0–410 rows at scale 0.723) has green only to row 252 of the region. The whole car texture shows it too (your observation, vs hl360's race livery in the same atlas layout): per 128-row band (1024 scale), ours 81.5 / **5.7** / 51.2 / **14.9** / 14.7 / 11.4 / 11.1 / 20.0 % non-black, hl360 70–91 % everywhere: in each 512-row strip the lower 256 rows are (nearly) empty, so seats, dashboard and front/rear/roof panels are missing too, not only vinyls. **Next:** a ReGPU change logs small vertex buffers in full in captures (`vf%u floats:`, ≤ 64 dwords; built, not yet committed); capture a vinyl edit and read the region/strip draws' quad corners: do they reach row 568/512 (ReGPU loses rows: big-pass scaling, 256-row tile handling) or stop at 256 (the game splits the pass into tiles and ReGPU drops the later ones)?
  **Session 10, later (B): it's a timing difference, not geometry.** Full vertex logging (ReGPU patch `regpu-capture-vertex-floats`) shows every region drawn as one full quad, e.g. (0,0)–(1416,568) with UVs covering the whole side view and atlas rows 512–1080, so the game splits nothing. The decisive observation: **vinyl edits made during traced (captured) frames produce a complete car texture and a complete car on screen** (green over the whole side up to the windows; four captures in a row, `out/logs/s10_multi/`), and **one edit in normal frames brings the cut back** (green top edge 129–158 → 261–271, green px 7109 → 2564). The game does the same work either way: same 8 strips, region updates and side-view resolves per edit frame at normal speed (`REGPU_TRACE_RESOLVES`, `out/logs/s10_normal_resolves.txt`). The editor's selection flash (yellow) also covers the full side. Ruled out with live A/B (nudges via `Fm3 "stick L 0.5 0 200"`, green edge measured with a scratch script like `greentop.py`): stencil, mask, side view, resets, region geometry, big/tall-pass rules, tile merge, draw workers (off by default anyway), stale-resolve check, and the per-frame texture cache (a drop-on-resolve fix and a cache-off switch were tried, changed nothing, and were removed). `REGPU_NO_TEXTURE_PREHASH` changes the result (less green) but doesn't fix it. **What traced frames change besides the texture cache is speed** (every packet and draw logged synchronously): so look for a race between the CPU and ReGPU's GPU work: something the game reads back or waits on (a fence/event/query, a CPU read of a resolved image, the texture pre-hash workers' timing), or a resolve whose host copy is read before the GPU has written it. Next ideas: slow normal frames down without tracing (e.g. a frame cap or a sleep switch) to see whether speed alone fixes it; log fences/queries the editor waits on; compare the order of CP packets around the region resolves at normal speed vs traced.
  **→ B2 shortcut to try first:** the race livery is the composite downscaled to 1024×1024 and compressed; our race-load trace has **four 1024×1024 resolves** (`rect 0,0-1024,1024`, pitch 1024): whole-image copies the existing write-back can do. Test live before writing new ReGPU code: in our build, `Fm3 "env FM3_WRITEBACK 1024x1024"`, then load a race and look at the car (session 9 only tried the 2048 sizes). If the car is right but without vinyls, the 1024 downscale reads the in-memory 2048 texture that lacks the region copies: then the region write-back below is needed too.

  **B2: a car with vinyls is noise in races.** At race load the game resolves the vinyl regions (e.g. `rect 0,0-1416,568`, pitch 2048, declared height 1536) **into the car's 8888 2048×2048 texture in memory** (no resolved image there), then its CPU code compresses that to DXT for the race. ReGPU writes nothing to memory; the `FM3_WRITEBACK` experiment can't help because `WriteResolveToGuest` writes a whole image at its own width, not a rectangle at a tile (x, y) of a 2048-pitch tiled image.
  1. Plan: a ReGPU write-back of a **resolve rectangle into a tiled destination at its place**: from the destination address compute (x, y) as `ResolveIntoEnclosing` does (tile = 32×32 × bpp bytes, row-major tiles of pitch/32), read the rectangle back from the GPU, and write it with the Xenos 2D tiling (`TextureTiledOffset` in `texture.cpp`) at (x + i, y + j) with the image's pitch. Then a game rule in `src/gpu_host.cpp` asks for it (`Writeback::Mode::Now`) for colour resolves whose pitch is 2048 and whose declared height + y = 2048 (car textures); check the race-load resolve list with `REGPU_TRACE_RESOLVES` (session 9 trace: `out/logs/s9_raceload_resolves.txt`).
  2. Also the 1024×1024 resolves at race load (four, pitch 1024): find out what they are (another livery level? the body texture?) before deciding whether they need writing back too.
  3. Verify: the car's race texture (find the DXT bound on the car's draws in a capture) and the car on the grid; FM2's port fixed the same thing by writing its race livery back.

### 2026-11-07 — session 9 (timer overlay, exploring past the intro race)
- Added the on-screen timer you asked for: ReGPU patch `regpu-timer-overlay.patch` (4th ReGPU patch; `setup.ps1` verifies it). A 136×34 white-on-black GDI panel copied onto the back buffer by the presenter after the game's frame (and during the shader-compile hold); text = `LogSeconds()`, which both logs now export. The game's own `src/log.cpp` is the one linked (it defines the same symbols as ReGPU's), so the timer and `fm3.log` share one clock. The shader-compile panel moves below it. You confirmed it works.
- You explored career menus, the upgrade shop, paint booth, vinyl apply/editor, My Cars, a solo race and photo mode, with 12 timed screenshots → issue list A–G in the status section.
- Two read-only searches of the FM2 and FM4 ports (both repos are one squashed commit, so only code comments/docs count as evidence). FM2 has its own renderer (not ReGPU) with `FM2_*` switches; FM4 vendors a ReGPU fork that is not a superset of upstream (~90 switches each side only). Results per issue are in the table.
- Reported first as you asked; you picked issue A.
- **Issue A fixed.** Reading `GetCubeTexture` first turned up a real but minor bug: for cubes read from memory, level 0 of face i was written at `offset + faceBytes·i` but copied from the running offset (all faces = face 0, or other levels' bytes). Fixed behind `REGPU_NO_CUBE_FACE_STAGING`; A/B screenshots looked the same, so not the cause. The showroom capture (`out/logs/s9_showroom_capture.txt`) then showed two cubes bound on the car draws, both format `?7` = **k_2_10_10_10**, neither resolved: `19781000` (mipped, real data in memory: `C1E0781E` = grey 30/1023 per channel, fetch exp adjust +3) and `1832E000` (no mips, all zero in memory, no resolve into it: unused or live-only slot; left alone). ReGPU's `Info()` had no entry for format 7, so `DecodeTexture` failed and the faces were zero-filled. Added the format (8 bits of 10 kept). Measured A/B at almost the same angle: near-black pixels on the car 116,945 → 65,802, paint mean R 112.8 → 124.9, highlights 9.0 → 11.7%; mirror, xD badge and exhaust tip back. You confirmed shiny paint and chrome in the dealer (R8) and the chrome rim list.
- **Issue C fixed.** Entering My Cars renders nothing (resolve trace `REGPU_TRACE_RESOLVES`, live): the pictures are stored with the car, made when it changes. Added a live experiment to the write-back hook, `FM3_WRITEBACK="WxH,..."` (set with `Fm3 "env FM3_WRITEBACK 512x256"`, re-read on change; kept as a tool). Repaints: (1) 256x256+512x512 written back → thumbnail noise, big picture now **empty** (its buffer `120DC000`, 512×256 8888, all zero); (2) traced repaint: once per save, `512x256` resolve to `14C5F000` and `256x128` to `1205F000` (8888, swap bit set), plus 5×256x256 icon passes; (3) write-back of 512x256+256x128 → both pictures correct, colours right. Permanent rule (8888, top 0, those two sizes, `Now`) built; a 4th repaint without the experiment: fixed, log `picture writeback: 141A6000 512x256`, `1206B000 256x128`. FM2's port had the same bug (its sizes 256x128 + 1024x1024).
- **Issue B (vinyls), partly fixed.** Resolve trace while editing: the car's 2048×2048 8888 texture is rebuilt in four 2048×512 strips (dest base +0/512/1024/1536 rows, each declaring the rows left) and, per edit, one rectangle per changed region at a tile (x, y) inside it (e.g. 1416×568 at 512,512: `0BAC9000` = tile 1040 of a 64-tile-wide 32×32 tiling). ReGPU made each an image of its own; the car read only strip 1. First try (strips only, GPU copy) was too narrow (regions are partial and at another host scale); then the enclosing match picked a strip's own image instead of the base (fixed: outermost wins). Now vinyls reach the car. **Still open:** a straight cut along the car: the game's 704×267 side-view vinyl pass (stencil-masked: a 1024×1024 k_8 mask sampled at the pixel position, alpha == 0 passes) already loses its rows ≥≈181. Ruled out: the mask (a big vinyl stops at a straight line), colour foreign-MSAA (`REGPU_NO_FOREIGN_MSAA` 0/1 same), window offset (0), stencil (`REGPU_NO_STENCIL` no change), 4× stencil-reset scale (`REGPU_NO_DEPTH_VIEW_SCALE` moves the line a little). Next: that pass's host depth/stencil placement (EDRAM depth base 099, pitch 720), and hl360 on the same screen.
- **Race-load crash (new path once the car had vinyls), fixed.** A thread at `822974B0` is created at race load; its `ThreadObject` was kept alive only by the title's handle, so a handle closed before the host thread first ran freed it: the thread read a freed `GuestThread` (garbage id, TLS -1) and crashed in `KeTlsGetValue`. `src/kernel/thread.cpp`: the host thread now gets its own strong reference from `StartThread`. The crash handler had hidden it (it faulted on a stale `ctx` before writing anything): `src/crash.cpp` now flushes early and reads guest registers under SEH.
- **Race livery noise (B2)** found after that: see the issue table; not caused by the session-9 changes.
- Possible follow-up: if reflections show banding, keep all 10 bits with a native `R10G10B10A2_UNORM` upload (the 2D path already uploads BC/half-float textures natively; the cube path always decodes to 8-bit).
### 2026-11-07 — session 8f (smooth bands fixed)
- You parked on the start grid with the chase cam rotated (bands visible) and paused. Two 16×16 boxes measured (smooth band at (875,635), grainy at (990,620)) across `ps` outputs: the slot-0 sample alone already differs (detail 9.5 vs 23.4), while its mip level and derivatives are identical (≈ level 1.4). So the **road image's content** was banded, and my earlier "t0 looks uniform" by eye was wrong.
- Capture: road image `05A1B000` level 0 had a **sharp top half and a blurred bottom half**; a phantom `0599B000` 512×768 image (256 rows earlier) held the fresh half. Cause: the half resolve `rect 0,256-512,512 dest 05A1B000` went through ReGPU's tile rule (`base = dest - top·pitch`). The existing `REGPU_RESOLVE_IMAGE_BASE` didn't apply (it only takes partial-width rectangles).
- Fix (ReGPU, `regpu-resolve-into-image.patch`): if an image already exists at the destination with the same width covering the rows, place the rectangle in it. Tiles name their own row, where no image starts, so FM3's scene tiles are unchanged (screen checked: no bar). Verified: you saw the bands gone; the capture shows the road image sharp throughout.
- Why only after rotating the chase cam: before that the game rebuilds the road image whole (correctly placed); turning the camera starts the half-at-a-time rebuilds.
- hl360 comparison (earlier): no bands there. Your console: none. Consistent with a ReGPU-only bug.

### 2026-11-07 — session 8d (smooth road bands)
- You confirmed from your console: the whole road is high-res asphalt; the smooth bands are a bug. Key clue from you: they appear **only in the chase camera after rotating it** (not in hood/bumper cam), and reset when cycling cameras.
- No new strips are rendered while rotating (40 s resolve trace). The composition shader `0d1ce823ed690669` (one full-target axis-aligned rect, prim 8) turns each pixel into rotated strip coordinates (`pc[1]`–`pc[4]`), samples DXT1 strip pairs t1/t2 and t3/t4, blends each pair by `pc[5].x`, and combines them with the base DXT5's luma/alpha (`pc[6]`). The camera angle selects the strips and weights.
- Tried and **reverted** (unproven): measuring the rect-list right angle in screen pixels. The composition rect turned out to be axis-aligned, so it can't be the cause.
- **Dumped all 64 DXT1 strips from guest memory** (`peek` on the physical mirror `A6375000`, 32 KB each, tiled, pitch 128, endian 8in16; decoded with `dumpdecode.decode_texture`, PNGs in `out/strips/`): every strip is **nearly flat** (detail 0.2–0.4 vs ~9 for the road), even/odd strips at mean 72 / 51. They should carry rotated, pre-filtered asphalt. So the CPU-side compression at load produces flat strips: either from wrong input (the 8888 strips as written back) or because the compressor code itself misbehaves in the recompile (VMX128-heavy code is a candidate).
- The 8888 strip memory is reused after load (now holds index lists), so it can only be inspected during the load.
- **Next:** a write watchpoint on a DXT1 strip at load (`FM3_WATCHW`, crash.cpp) to name the compressor function; then check its input and its vector instructions.

### 2026-11-07 — session 8e (smooth bands, continued; not solved)
- **Watchpoint** (`FM3_WATCHW=A6375000`, temporary `play.cmd` line, removed again; it watches all three physical aliases): 3 writes, all at boot: allocator memset (0.3 s), then `sub_8252EE40` copying 0x7F80 bytes from `4656DDFC` to `A6371100` (1.3 s, a resource loaded from the game files). **Nothing writes the strips at track load.** So the four bound DXT1 strips are static textures, and "flat strips = broken compressor" was wrong.
- **Composition constants** with and without bands (you rotated the chase cam and paused): identical. `pc5 = (0,0,0,1)`, `pc6 = (0,0,0,1)`: the strips get weight 0 and the composed road image is just the base texture's luma/alpha. The composition is not where the bands come from.
- **Road shader inputs** shown live one by one with their real coordinates (slot 0 colour and alpha, slot 1 alpha, slot 2 both lookups, slot 7, slot 11 `.x`@`o[3]` and `.y`@`o[4]`): all clean, no bands. The road shader paints the whole road (tint test), yet its output has bands. `REGPU_SKIP_SCREEN_PASSES` changes nothing.
- **Open leads:** (1) the shader's lighting/specular arithmetic (normals from `o[0]`/`o[1]`, `pc[223]`–`pc[255]` terms); compare intermediates numerically between a band and a non-band pixel rather than by eye; (2) whether hl360 shows the same bands in the rotated chase cam (yes = game-logic/CPU side shared by both; no = ReGPU-side); (3) LOD/derivatives: bands look like a lower mip; compare `ddx/ddy` of slot 0's coordinates across a band edge.
- Tooling added in the scratchpad (not in the repo): `crop.py` (crop and enlarge shots), `strips.py` (dump/decode via `peek`), `compose_pc.ps1` (capture a draw's constants).
- **hl360 check (you, rotated chase cam): no bands.** So the bands are **ReGPU-specific** (hl360 and your console are both clean).
- A block-wise detail measurement (final vs composed texture vs shader intermediates `r0`/`r1`/`r4`/`r6`/`r9`/`r10`) pointed at `r9`, but that was confounded by road markings landing in the "normal" blocks; not a finding.
- **Next:** park on plain asphalt (no lines or markers in view), rotate the chase cam until bands show, pause; then read intermediates numerically at a band pixel and a non-band pixel (e.g. `ps` edits that output a scaled single term, compared with `pngstat`), to find the first term that diverges. Also compare the slot-0 fetch's LOD: output `ddx`/`ddy` of its coordinates across a band edge.

### 2026-11-07 — session 8c (road streaks, square shadow)
- You reported the streaks still there after the sun fix. Live shader edits (`ps <hash> <expr>` on the road shader `08a4eb8fbebd07fe`) showed each input separately: slot 0 (`8888 512×512`, a runtime-composed road detail image) carries the streak pattern; slot 11 (shadow) does not.
- The detail image: rebuilt half per frame (`rect 0,0-512,256` / `0,256-512,512`), mip chain resolved after it (256² at +1 MB, then 128², 64², … padded to 32×32 / 4 KB; ReGPU assembles that chain fine). Its fresh half had the streaks. The composition draw (ps `0d1ce823ed690669`) reads `t0 = DXT5 512×512` (base asphalt) and **`t1–t4 = DXT1 32×512`** strips (32 KB apart). Those strips, decoded, were **pieces of car paint**.
- 90 s resolve trace while you drove: no 32-wide resolves and nothing into the strip region, so they're made at load. Strip write-back rule (32×512): 64 strips caught at load (`05B75000`–`05F65000`), but Deferred → no change; Now → the fine dashed lines are gone (your test).
- `REGPU_CAPTURE_ON_RESOLVE=05B75000` (temporary line in `play.cmd`, removed again) captured the load frame: ~49 strip draws alternating two shaders, both reading the base DXT5 with rotated coordinates and min/max tracking (compression preparation); A uses colour·luma, B uses alpha.
- **Square shadow (regression I caused):** the road's shadow map `18708000` is resolved as an unsigned **fraction** (`dest_info 00000C82`), and ReGPU already returns fractions correctly. My patch divided it by 65535 anyway (lit ≈ 0 → square) and converted the white border too. Now the conversion uses both the copy's and the fetch's number format (identity when equal) and skips the border (`gBorderHit` in `TFetchRaw`). You confirmed the square is gone. `patches/regpu-fixed16-fetch.patch` regenerated; `setup.ps1` verifies both ReGPU patches.
- Photo mode shows a whitened band in the top third (you said: ignore for now, not seen in races).

### 2026-11-07 — session 8b (sun white-out)
- You drove to a spot facing the sun and paused, with the debug server on (later runs via `play.cmd`, made at your request; you start the game and report when in position, and I don't repeat the launch steps).
- Switch sweep facing the sun: only `REGPU_FAKE_OCCLUSION` removed the white-out (brightness 152 → 38). fps 39–40, 53 with fake answers, 55 without coverage draws.
- `REGPU_TRACE_ZPD_RESULTS`: one query per frame answering **zpass 6,472,508**. A capture showed it beginning before the 6-vertex sun quad in tile 1 and ending after tile 2's, with 621 tile-2 draws inside. The trace showed 3 plain ZPD events + 1 end per frame, i.e. a count-on/off pair per tile.
- Fix in ReGPU (`RendererOcclusionBegin`: a start while counting = pause, then resume in a new D3D query part; the end sums them; swap finalises a paused query; `REGPU_NO_ZPD_SPANS` A/B). The ZPD trace now prints `RB_SAMPLE_COUNT_CTL` (00000002 for all four events in FM3, so the control register doesn't separate begin from end here; the pairing does) and is no longer capped at 12 lines per run.
- Result: zpass 5,780; normal glare; 60 fps. Saved as `patches/regpu-zpd-tile-spans.patch` (split from the working tree with a scratch index); `setup.ps1` verifies both ReGPU patches.

### 2026-11-07 — session 8 (visual bugs: bar, asphalt, sun)
- You reported: race playable to the finish; input + audio fine; three visual bugs (screen-wide bar, sun white-out + slowdown, asphalt streaks worse in motion).
- `tools/fm3ctl.ps1` (own TCP client for ReGPU's debug server, port 7341). The debug pad's presses reach the game (A skips the intro; START pauses after the countdown, not on "GO!").
- **Bar:** a hard edge at row 512. hl360's grid dump shows FM3 resolves the 2x-MSAA scene in two tiles (`1280×512` + `1280×208`, contiguous), and ReGPU's trace shows it handles the tile replay. On a paused frame I swept ~60 live switches with a row-brightness metric; only `REGPU_NO_FOREIGN_MSAA` removed the band (rows 496–511: 114 → 64). Made it an FM3 default.
- **Asphalt:** swept texture/sampler switches with an edge-energy metric (only `REGPU_NO_FETCH_BOUNDS` helped partly, a red herring). `capture` → pixel shaders bound to the shadow maps → tinting each found the road shader `08a4eb8fbebd07fe`. Its slot 11 = `18708000` (256×256 `k_16_16` VSM). The resolved images themselves were clean (`out/run/out/res`). Fetch constant `8200D802 … 01281690 … 00000219`: clamp to border (white), unsigned, fraction. The resolve had been signed integer with exp bias +10 (`dest_info 000A6C81`). ReGPU's fetch setup never applied the fetch's number format/sign to resolved fixed-16 images → fix in ReGPU (`regpu-fixed16-fetch.patch`; `setup.ps1` now applies ReGPU patch stacks too).
- **My mistake, caught by you:** the first version of the shader helper used a loop writing `v[c]`; fxc can't unroll that (X3500/X3511), so **160 shaders failed to compile** and the scene went black with a broken UI. Rewritten with vector selects: 0 failures. Lesson: after any change to ReGPU's HLSL preamble, boot and grep `failed to compile` before looking at anything else.
- **FM4's vendored ReGPU A/B:** added CMake `FM3_REGPU_DIR` / `FM3_REGPU_FM4` (crash.cpp has an `#if`), but building FM4's copy from `reference/` was **blocked by the permission classifier** (third-party code in the reference clones). Not needed for these two fixes.
- My scripted A presses carried into the race (the car drove off); the scene changes in the first sweep were you rotating the camera, so that sweep was discarded and redone on a paused frame.
- Stray file: ReGPU's `hlsl` debug command cut my path at the first space and wrote `G:\claude`; moved it into `out/ps_road.hlsl`. Use paths without spaces for that command.

### 2026-11-07 — session 7 (FFmpeg + Phase 3: runtime port, first boot)
- **FFmpeg** (you asked me to get the right one): BtbN release `latest`, asset `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` (80,407,192 B). SHA-256 `9cb911fc…04c0` matches both GitHub's asset digest and BtbN's `checksums.sha256`. Downloaded to an empty scratch folder, extracted to `thirdparty/ffmpeg` (gitignored). Version n8.1.3-34-ga35157abb9 (2026-11-07), LGPL v3 (`--enable-version3`, no `--enable-gpl`), with `xma1`/`xma2` decoders; DLLs avcodec-62, avutil-60, swresample-6.
- **Runtime port:** copied FM4's `src/` from its git objects (51 files) and adapted it:
  - fibers re-addressed (`hooks/addresses.h`: 82A7B098 / 82A7B128 / 82A7B178 / 82A7B258 / 82A82210; FM4-identical object layout confirmed in disassembly);
  - single-image `recomp_modules.cpp` (from `ppc_config.h`);
  - `Fm2Timebase` → `GuestTimebase`; `FM2_*`/`FM4_*` env switches → `FM3_*`; log `fm3.log`; user dir `Documents\fm3` (`FM3_USER_DIR`);
  - removed: the test launcher call, the FM2 ring-dump addresses in `crash.cpp`, FM2/FM4 ui.zip patches (behind `FM3_LEGACY_UI_PATCHES`), benchmark mode and the draw-worker profiler (FM4's vendored ReGPU API);
  - `regpu::RendererLogCrashContext` → upstream `regpu::LogCrashContext`.
  - Not copied: boot screen (FM4 art), static shader scan (`.fxobj`), pipeline seed, updater, launcher, `.rc`.
- CMake: mftb patch at configure time (`tools/patch_mftb.py`, own), FFmpeg linkage and DLL copies, `winhttp`.
- **Build:** 4 compile errors (benchmark calls), then 2 link errors (FM4-vendored ReGPU symbols), then `fm3.exe` links (40 MB).
- **First boot** (`out/run`, `FM3_USER_DIR=out/user`, game = hl360's verified `game/disc1`): runs, swaps, intro video. Screenshots `out/shots/boot_{08,20,40,70}s.png`: ESRB, white logo fade, R8 door, LMP cars. 59.6 fps. Audio trace: 188 callbacks/s, 0 dropped. XMA: 0 packets in the intro (FM3's video audio likely isn't XMA; check in menus/race).
- **Credit fix:** the FM4 repo is Aiden-Piercee's (`Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU`); its MIT notice is FM2's (© zer0ducksgiven). I had credited zer0ducksgiven alone in `patches/README.md` and the README; corrected, with `NOTICE.md` holding the licence text.

### 2026-11-07 — session 6b (dependencies)
- You installed FFmpeg via winget and extracted FidelityFX-SDK 2.1.0 to `G:\claude code projects\tools\`.
- FidelityFX: the layout matches (`Kits/FidelityFX/upscalers/fsr3/include/gpu/fsr1/ffx_fsr1.h`, `signedbin/*_dx12.{dll,lib}`; DLLs dated 2025-12-09: loader, upscaler, framegeneration, denoiser, radiancecache). All 5 DLLs: Authenticode **Valid**, CN=Advanced Micro Devices. Junction `thirdparty/fidelityfx` → SDK root (gitignored, no copy). FM2/FM4 used v2.3.0; 2.1.0 compiles fine for ReGPU.
- `cmake -S . -B build/rel` (full game) configures; `ninja regpu` 20/20 (warnings only: `-Wswitch` in AMD's `ffx_api_dx12.h`, `-Wformat` DWORD in ReGPU `renderer.cpp:10629`); `regpu_linkcheck` links.
- FFmpeg: winget's `Gyan.FFmpeg` = `ffmpeg-9.0.2-full_build`, `--enable-gpl --enable-static`, executables only → unusable for linking (and GPL). Left installed, not referenced.

### 2026-11-07 — session 6 (Phase 2: first recompilation)
- `game/default.xex` copied from hl360's verified `game/disc1` (SHA-256 matches). `xexdump` → `out/fm3.bin` + `out/fm3.sections`.
- **New tools (own code):** `build/tools/ppcdis.exe` (XenonRecomp's disassembler over the loaded image), `tools/fm3_config.py` (helpers, EH words, setjmp/longjmp candidates; `--write`), `tools/fm3_check.py` (output checks), `tools/genstubs.py`, `tools/recomp.ps1` (whole pipeline). FM4's `fm4_config.py`/`fallthrough.py`/`genstubs.py` were read for the approach, not run.
- **Helpers:** all 8 match exactly once: restgprlr `82530230`, savegprlr `825301E0`, restfpr `8253147C`, savefpr `82531430`, restvmx14 `82531828`, savevmx14 `82531590`, restvmx64 `825318BC`, savevmx64 `82531624`.
- **.pdata:** 32,352 functions (0x8F8244 bytes); 413 carry exception data, with handler words `82A89918` (349), `82B6747C` (63), `82A91B00` (1) → `invalid_instructions`.
- **Q1 confirmed in disassembly:** setjmp `82534B20` (no .pdata; optional hook through the word at `82CE72D8`, then f14–f31 @0, r13–r31 @152, v64+) and longjmp `825345E0` (r1 @144, LR @308, unwind flag @312): the same CRT layout as FM4. The other run-matches: `82A82224`/`82A82418` = inside SwitchToFiber; `82A91994` = EH context restore.
- **Q7 confirmed:** thunk `82A7B2B8` = `b 0x82A82210`; body `82A82210`–`82A8261C` (0x40C, ends `b 0x82B67B1C`, no .pdata). The new-fiber trampoline `82A82200` (0x10, no .pdata) was added as a function too.
- **XenonAnalyse:** 262 jump tables, **251 of them only found by FM4's back-trace patch** (so Q6's guess was right). The 2 "unresolved" `bctr`s (`82923EE4`, `82923F1C`) are indirect tail calls through function-pointer tables `82C08488`/`82C08528` (index r3·10+r4, 40 words); all 31 non-null entries are already function starts.
- **First recompile:** 15 unresolved branches in 3 functions with no .pdata that each hold a byte-offset jump table (`82354668`, `825F16B8`, `829D5508`). XenonAnalyse found the tables, but the sweep ended each function at its `bctr` and made every case a function of its own. Fixed with explicit `functions` sizes (0x15C, 0xAC, 0x1F8) → **0 errors**. (My first guess, that `nop`s broke the trace, was wrong: the tables' `base` field is the first table instruction, not the `bctr`.)
- **Q2/Q3** entries added with hl360's sizes (11 + 4).
- **Q4 cross-check** (hl360 `fptr_functions.toml` + `codegen.toml` + forza3-recomp manifest, plus our own data/`lis+addi` scan, with jump-table loads, inline tables and `loc_` labels filtered out): of 19,469 pointer targets, 19,453 are already function starts. XenonRecomp's gap sweep finds the pointer-only functions by itself; **no fptr entries needed**. The remaining 16 are the SEH resume targets above. Parsing note: the forza3-recomp manifest has addresses in comments ("data ref 0x…"), so only entry keys are read.
- **Compile check:** `cmake -S . -B build/ppc -DFM3_PPC_ONLY=ON` + `ninja fm3_ppc`: 180/181 steps, no errors. `src/ppc_hooks.h` (own, same contract as FM4's) provides `PPC_MSR_CHANGED`, `PPC_MM_STORE_U32` (17 uses), `ppc_half_to_float` (298) / `ppc_float_to_half` (2), and `PPC_CALL_INDIRECT_FUNC` (42,574) → fatal-on-miss `PPCLookupIndirect`.
- Gotcha: Windows PowerShell 5.1's `>` writes UTF-16, which the Python tools can't parse. `recomp.ps1` redirects through `cmd /c`.

### 2026-11-07 — session 5 (Phase 1 skeleton)
- Studied FM2-ReGPU (`c5e71cd`) and FM4-ReGPU (`69bdae4`) layouts. FM2 has its own embedded renderer (`src/gpu`), so **FM4 is the template for ReGPU linkage** (its CMake, `src/gpu_host.cpp`). Found FM4's ReGPU is a vendored, diverged copy (config decisions).
- `git init`; added the two submodules from GitHub at the pinned commits; fixed their CRLF checkout.
- Copied FM4's 5 XenonRecomp patches. The copies from FM4's working tree were CRLF and every hunk failed; re-extracted with `git show HEAD:patches/…` (LF) and all 5 applied cleanly.
- Wrote: `CMakeLists.txt` (game), `tools/hosttools/{CMakeLists.txt,xexdump.cpp}`, `tools/setup.ps1`, `tools/vcenv.cmd`, `src/gpu_host.cpp`, `README.md`, `patches/README.md`, `src/README.md`, `config/README.md`.
- `setup.ps1` first run: PowerShell 5.1 + `$ErrorActionPreference='Stop'` threw on git's stderr, and per-patch reverse checks fail on overlapping patches; fixed both (scratch-index tree comparison). Second run: submodules OK, "all 5 patches already applied", `build\tools` 36/36 steps, clean.
- **xexdump on FM3** (`hl360-forza3/game/disc1/default.xex`, SHA-256 matches): base `82000000`, size `D60000`, **entry `82529DD8`**. Code sections: `.text` 821E0000+987BCC, `_TEXT` 82B67C00+39C, `.embsec_` 82B68000+4E18, `PAGELK` 82B6D000+984, `BINK` 82B6DA00+10728 (ends `82B7E128`, matching the code range above). Data includes `BINKCONS`, `BINKBSS`, `BINKDATA`, `.XBMOVIE`, `.tls` 82CE7C00+1821.
  - XenonUtils reads 8-byte section names as C strings, so full-length names (`BINKCONS`, `.embsec_N…`) run into the next bytes. Our xexdump cuts at 8; **XenonRecomp itself has the same behaviour**, so watch for odd section-name matches in Phase 2 logs.
- Top-level CMake configure checked: it stops with the intended "out/ppc is empty" message.
- Nothing installed.
- **Session 5b (publish):** you created `braaap-stututu/FM3-ReGPU` and asked for the project there. Remote `origin` added; its one commit (`24563c0`, an empty `githubfileignorepls`) became our parent, so no force-push and nothing of yours removed. Scanned the staged files for personal data first (none: no email, user paths only as `G:\claude code projects`). Pushed `65f82d0` (25 files; `reference/`, `dumps/`, ISOs, `build/` stay ignored). `gh` CLI is not installed; push went through Git Credential Manager.

### 2026-11-06 — session 1 (setup)
- Audited the toolchain (table above). LLVM is the only blocker for building the baseline; the game ISO is also needed.
- Cloned 6 repos (commits in FM3-QUIRKS.md header); initialised hl360's `rexglue-sdk` submodule recursively (needed the long-paths fix).
- Mined both ReXGlue FM3 ports and the XR/ReGPU/FM2/FM4 repos; wrote `FM3-QUIRKS.md` (26 quirks + GPU fix table + tooling map).
- Main cross-project findings: both ports independently agree on setjmp/longjmp, the 15 boundaries, fptr lists, fibers and `cache:`. They disagree on readback mode (`fast` vs `some`) and on the car-shadow fix (TS's resolve+fetch number-format fix is the more complete one). The SwitchToFiber address "conflict" is thunk (`0x82A7B2B8`) vs body (`0x82A82210`).
- Nothing recompiled, nothing installed.

### 2026-11-06 — session 1b (LLVM)
- Installed and verified LLVM 23.1.5 with your approval (details under config decisions). Confirmed that hl360's `scripts\devenv.cmd` resolves clang/clang++/clang-cl/lld-link from `C:\Program Files\LLVM\bin` and cmake 3.31.6 / ninja 1.12.1 from VS Build Tools. vcvars prints a harmless "vswhere.exe not recognized" line.
- The hl360 baseline is now blocked only on the Disc 1 ISO; the SDK build can go ahead without it.

### 2026-11-07 — session 2 (hl360 baseline build + first run)
- You supplied both ISOs in the project root; MD5s match.
- First `build_sdk.cmd` failed: all 6 patches rejected because of CRLF conversion from the global `core.autocrlf=true`. Fixed repo-locally (see config decisions); then all patches applied.
- Built the SDK (840/840 steps, clang 23.1.5, no source changes), then `fm3tool`, then ran `install_game.cmd` with both ISOs: Disc 1 3,059 files, Disc 2 55 files, 8 DLC packages installed. `default.xex` SHA-256 matches; the full 3,057-file manifest verifies.
- `build.cmd release`: codegen + 186 steps in about 1.5 minutes → `fm3.exe` (61 MB), `fm3-launcher.exe`, `fm3tool.exe`.
- Wrote the baseline `fm3.user.toml` and launched `fm3.exe`. The window opened and rendered the intro (R8 at Montserrat), holding 60.0 fps average over a minute; see the frame-pacing observation above.
- You then completed the intro race on the baseline. It reaches the race and runs at about 60 fps in-race; after the race it drops to a locked 30 fps (cause unknown; see Known-broken).
- Capture tooling question: **RenderDoc + texture/resolve dumps are complementary, not redundant.** Note that `--texture_dump_dir` is **not** in stock ReXGlue v0.10.0 (grep finds nothing in the SDK). It was forza3-recomp's local, unpublished SDK hook; only its shape survives in TS's `tools/analysis/port_dump_to_canary.py` and `texdecode.py`. So on the baseline it would be a small SDK patch we write ourselves (`patches/sdk/0007-…`). On the ReGPU side the equivalents already exist (`REGPU_DUMP_RESOLVES`, `REGPU_DUMP_AT`, built-in RenderDoc hook).

### 2026-11-07 — session 4b (hand-off)
- Wrote the project `CLAUDE.md` (orientation, layout, baseline commands, toolchain, gotchas) and replaced the stale "Baseline plan" with the phased *Work plan* (Phase 0 done, Phase 1 next). No code or build changes.

### 2026-11-07 — session 4 (capture route + dump decoder)
- **`tools/dumpdecode.py`** (own code, pure Python 3, no numpy): `list` / `decode` / `diff`.
  - Implements Xenos 2D tiling (same formula as the SDK's `GetTiledOffset2D`), endian modes 8in16/8in32/16in32, and the fetch-constant swizzle (dword 3 bits 1–12).
  - Formats: 8, 1_5_5_5, 5_6_5, 6_5_5, 8888(_A), 2_10_10_10, 8_8, 4_4_4_4, 10_11_11, 11_11_10, DXT1, DXT2_3, DXT4_5, 24_8, 24_8_FLOAT, 16, 16_16, 16_16_16_16, 16/16_16/16_16_16_16 FLOAT, 32, 32_FLOAT, 32_32_FLOAT, DXN, DXT3A, DXT5A, CTX1. Base level only.
  - Validated on: the intro video (linear k_8 Y/U/V); a tiled 8888 resolve with the R/B-swap bit (correct only with the swizzle applied: the red R8 came out blue without it); and a full race frame (455 textures: DXT1/DXT4_5/DXN/DXT5A/16_16/24_8/2_10_10_10/8888/32F all decode coherently, 47 s).
  - `diff` matches by **content hash first, then by (w,h,fmt) + first-use order**, and compares resolves with addresses stripped. **Guest addresses differ between runs** (allocation order shifts by 0x1000s), so address matching left about 390 textures "only in A" on the first try.
- **`tools/fm3_route.ps1`** (own code; forza3-recomp's `drive.ps1` only read for the PostMessage approach). Functions: Start-Fm3 `-Fresh`, Connect-Fm3, Send-Key, Save-Shot (PrintWindow, DPI-aware), Test-Paused / Wait-Paused / Resume-Race, Request-Dump, Get-FrameStats, Stop-Fm3, Invoke-Fm3Route.
  - Uses a dedicated profile `E0000BA5E11E0002` passed on the command line (CLI outranks `fm3.user.toml`: SDK `cvar::Outranks`). `-Fresh` deletes only that profile's save. Your `Baseline` save (`E0000BA5E11E0001`) is untouched.
  - **Observed fresh-profile flow:** ESRB → Turn 10 → Microsoft → Dolby → R8 intro video → controls/loading screen (~72 s) → R8 car intro cutscene → intro race at Montserrat (start grid; the car doesn't move without throttle). Return skips cutscenes and toggles the in-race pause menu.
  - **Lessons:** timed Return presses drift out of step (Return both skips and pauses), so the route is state-aware. Pause is detected by the RESUME bar's shape (red inside x 86–312 / y 202–230, not red just outside), confirmed twice 300 ms apart (the red R8 in the intro video gave false positives), and re-confirmed 2 s later (a pause menu can appear before the race and then the game loads on). Captures under 100 textures are flagged.
  - Dump of a full race frame takes a few seconds; the timeout is 90 s.
- **Hook fixes (0007 rev 2/3):** (a) it hashed every texture *per binding* (2,315 draws → about 16 s for one frame); now it hashes once per distinct texture per frame. (b) Frame numbering restarted at `frame_0001` each session and wrote into leftover folders (this mixed my first intro test with a race capture); now it skips existing names.
- **Determinism result** (runA vs runB, start grid paused, both fresh profiles): 419 identical content + 6 equal after decode out of about 471; 27 differ, all render targets (HDR 2_10_10_10 buffers, the 256×256 k_16_16 car footprint/VSM map, 24_8 depth, small 8888 RTs), because the pause lands a few frames apart. The resolve sequence is identical (28, addresses ignored). **Interpretation:** static assets compare exactly; per-frame render targets need the same scene to compare, so judge them by eye or with tolerance, or later pair with input replay.
- **FM3 finding:** the start-grid frame has a **256×256 k_16_16 + k_24_8 pair showing top-down car footprints** (draws ~582/583). That's very likely the car shadow/variance map behind FM3-QUIRKS Q19, and the exact texture to check in the ReGPU port.
- Captures on disk: `dumps\hl360\runA`, `runB` (~160 MB each), `dumps\diff_runA_runB_grid\` (diff PNGs), `dumps\route\` (exploration shots). All game-derived; never commit.

### 2026-11-07 — session 3 (30 fps check, RenderDoc, dump hook)
- Relaunched the baseline (`fm3_002.log`); you raced again and noted the times. Pause menu = 60 fps; **post-race finish menu = locked 30.0 fps** (details under Known-broken).
- Installed RenderDoc 1.47 with your approval (signature verified; see the toolchain table).
- Wrote SDK patch `0007-frame-dump-hook` (see Patches applied). Rebuilt the SDK incrementally (30 steps, clang 23, clean) and `fm3.exe` (4 steps). The new `rexgpu-xenos.dll` was copied beside the exe by the build.
- Patch generation trick: staged the tree with 0001–0006 applied (`git add -A`), edited, then `git diff` = only 0007's hunks. Stripped the 4 `thirdparty/*` submodule-dirty gitlink entries from the diff, then `git reset`. `git apply --reverse --check` confirms the patch matches the tree.
- **Test capture:** booted, created `dumps\hl360\TRIGGER` after 40 s. It was consumed and wrote `frame_0001` at guest swap 2358: 54 draws, 4 distinct textures, 2 resolves, 5 MB. That frame was the intro video (three linear k_8 planes, 1280×720 + 2× 640×360 = Y/U/V). Decoded the 1280×720 plane with my own minimal decoder: a clean image (the R8's door), so trigger → index → raw dump → decode works.
- Running forza3-recomp's `tools/texdecode.py` directly was **denied by the auto-mode permission classifier** (external code). I used my own script instead; a proper decoder of our own is on the next-steps list.
- New folder: `dumps\hl360\` in the project root (captures; game-derived data, never commit).
