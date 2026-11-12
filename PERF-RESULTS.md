# Performance results (session 23, 2026-11-11)

Every lead from [PERF-PLAN.md](PERF-PLAN.md) that could be tested hands-off, measured on the **frozen test grid**
(CrownJewel career race, 8th, Scion xD, world stopped at 9.0 s of the countdown clock; `grid_test=1`,
`src/hooks/grid_test.cpp`). User's PC: RX 7900 XTX, 2560x1440, 4x MSAA, cap 360, `perf_log=0` for the runs.

**Pictures:** every test's screenshots are in `out/perf/s23/` (local, not in git), named `<test>-<side>.png`
(live A/Bs: `<id>-off.png` / `<id>-on.png`; start-time: `<id>-boot1.png`, `<id>-boot2.png`, defaults boots
`P2-BASE-defaults-<n>.png`). Raw numbers: `out/perf/s23/results.csv`; the running log: `out/perf/s23/journal.txt`.
Diff tools: `tools/analysis/pngdiff.py A B` (counts, 16x9 map), `pngdiff_mask.py A B N1 N2` (leaves out the blocks
where two plain boots N1/N2 differ: the crowd), `pngdiffimg.py A B OUT [thr] [x0 y0 x1 y1 scale]` (red = differs).
Scripts: `tools/perf_suite.ps1` (boot to the frozen grid, fps, shots, interleaved live A/B, CSV), the phase scripts in
`tools/perf/session23/s23_*.ps1` (copies of `out/perf/s23_*.ps1`).


## Session 29 (2026-11-12): L45 Radeon GPU Profiler capture of the frozen grid
**Method (reusable by the other ReGPU recomps):** Radeon Developer Tool Suite 2026-10-02 (RGP 2.7.1, panel 3.5.1;
GPUOpen zip, AMD-signed; unpacked to `G:/claude code projects/tools/RadeonDeveloperToolSuite-2026-10-02-1872`, outside
the repo), driver Adrenalin 26.10.1. Headless: start
`RadeonDeveloperPanelCLI.exe -m profiling -p fm3 -a frame:12000 -o <file>.rgp --verbose` first (it waits for the
process, sets peak clocks, captures frame N), then boot (`Start-Grid` from `tools/perf_suite.ps1`). Frozen grid at
1440p 4x, one tile + half-rate mirror on (the user's settings), perf log off. ETW group not configured (admin script
`scripts/AddUserToGroup.bat`): only queue signal/wait naming is missing. RGP has no text export: read in the GUI
(`RadeonGPUProfiler.exe <file>`). Capture `out/perf/rgp/grid-1440p4x.rgp` (109 MB), the user's screenshots of
every pane in `out/perf/rgp/screens/` (local, not in git).

| Measure | Result |
|---|---|
| Frame | 3,699 µs (270.3 fps); **GPU idle 8.13%**; RGP's verdict "CPU bound" (the draw thread, as known since session 20) |
| Events | 2,813: 2,119 DrawIndexedInstanced, 521 DrawInstanced, 106 ResourceBarrier, 22 ClearRenderTargetView, 17 CopyTextureRegion, 5 ClearDepthStencilView, 10 driver-overlay dispatches; 2 queue submissions, 9 command lists, no copy/compute queue, no waits/signals; 47.9 M shaded pixels |
| **Barriers** | **0.72% of the frame**; 0.04 per draw. **No layout transitions at all:** every column (depth/stencil decompress, HiZ resummarize, DCC decompress, FMask decompress, fast clear eliminate, init mask RAM) empty. 311 cache invalidations, 143 flushes: most application barriers are a FULL drain with K/L0/L1/L2/CB/DB invalidate and L2/CB/DB flush. Driver barriers: occlusion-query resolve stalls (2-10 µs each, a few). |
| **Compression** | ON for every scene target (Render/depth targets pane); OFF only for a few tiny R10G10B10A2/RGBA16F targets (128x64, 64x64, 192x192) |
| Context rolls | 1,053 for 2,685 draws (2.55 draws per context), RGP "Low"; none completely redundant |
| Main targets | Colour RT #12 = ReGPU's EDRAM surface, **R16G16B16A16_FLOAT 2560x2048 4x MSAA** (ReGPU `kColorFormat` for every surface), 1,790 draws, 2.89 ms; depth D32_FLOAT_S8X24 4x, ~1,900 draws |
| **Clears** | `ClearRenderTargetView` events take **29-44 µs of real GPU work each** (e.g. events 2777: 43.9 µs, 2525: 29.3 µs) = slow clears: ReGPU clears EDRAM surfaces with a rectangle (`renderer.cpp` 8080, 11855, 11906), and AMD only fast-clears (metadata) a whole subresource |
| Most expensive | top 5% of events = 16% of the time; fullscreen passes (event 2774 DrawInstanced(3): 101 µs duration / 29 µs work), the big car/track draws (1355-1363: 80 µs / 17-63 µs). **Runs of DrawInstanced(6) quads at ~44 µs duration but ~6 µs work** (events 2788-2802, 2527-2550): small passes serialised behind the FULL-drain barrier pairs (end-of-frame resolve/post chain, events ~2520-2810) |
| Wavefront occupancy | mostly low (under 25%) through the scene pass with bursts: the GPU is busy but not on shader waves (bandwidth/ROP on 64-bit 4x MSAA targets, or the front end with ~2,600 small draws) |

**Decisions from it:** **L46 (keep compression) has nothing to win: parked** (no decompress anywhere, compression on).
New leads instead (PERF-PLAN L54-L56): **L54 fast clears** (whole-subresource clears where the rectangle covers the
surface's used area, or a full clear when nothing else lives in that EDRAM range), **L55 the end-of-frame barrier
chain** (merge the per-quad barrier pairs, drop full-cache flushes the next pass doesn't need; ceiling ≈ the 8% idle),
**L56 render-target format** (RGBA16F everywhere = 8 bytes a sample at 4x; a 32-bit host format for guest 8888 /
2_10_10_10 surfaces would halve ROP bandwidth on those passes, but ReGPU aliases all EDRAM surfaces in one format:
large change, check FM3's main scene format first). L47 upload heaps is not touched by this capture (no copy queue in
use; still open).

## Session 29 (2026-11-12): L47 upload ring in a GPU upload heap (resizable BAR)
ReGPU patch `regpu-gpu-upload-ring`, live switch `REGPU_GPU_UPLOAD_RING`. The driver reports **GPU upload heaps
supported, resizable BAR on** (log line). Correction to the session 29b study: the GPU always reads every draw's
constants (~10 KB a draw) straight from the upload ring; S18's `REGPU_CONSTANTS_IN_RING` -21% was the CPU's scattered
writes into write-combined memory, not slow GPU reads. Frozen grid, `Test-Live`, alternating rounds, user's settings
(one tile, half-rate mirror; the draw count alternates 2,467 / ~2,620 with the mirror in both states, 0 skipped):

| Setting | Off (system memory) | On (VRAM) | Change | Rounds | Picture |
|---|---|---|---|---|---|
| 1440p 4x | 295.0 | 333.1 | **+12.9%** | 6/6 | 0 pixels |
| 1440p 4x (second run) | 297.1 | 332.5 | **+11.9%** | 8/8 | 0 pixels |
| 720p 1x (CPU-bound; off side at the 360 cap) | 349.7 | 331.8 | **-5.1%** | 1/6 | 0 pixels |

So: GPU-bound wins big (the GPU reads ~26 MB of constants a frame from VRAM instead of over PCIe), CPU-bound loses
(the CPU writes them over PCIe into VRAM).

**Later the same day (grid recheck and the user's drives):** grid recheck 264.2 -> 314.7 fps **+19.1%** (8/8, 0 px);
race released on the grid (AI cars driving, camera still) +11-18% at ~2,200-2,450 draws. **The user's drives** (1440p 4x,
perf log on in both, same start, first ~20 s of the race at ~2,450-2,750 draws; logs `out/run/fm3.drive-ringon.log`,
`fm3.drive-ringoff.log`): ring in VRAM **~223 fps**, draw thread 3.9-4.3 ms a frame (~1.52 us a draw); ring in system
memory **~252 fps**, draw thread 3.5-3.8 ms (~1.41 us a draw): **-13% in the race.** Racing is draw-thread (CPU)
bound on this PC, and the same constants that the GPU reads faster from VRAM cost the CPU more to write there, so
splitting the ring would not help. Lead instead: fewer bytes written per draw (the non-float parts of each constant
block, ~2-3 KB, are written whole every draw). **Kept off by default; launcher tooltip says to use it only when the
graphics card is clearly the limit.**

## Session 26 (2026-11-11): L38 timer, L36 one guest tile
| Test | Setting | Before | After | Picture |
|---|---|---|---|---|
| L38 timer fix (minimized) | 720p 1x grid, Sleep(1) | 268/286 fps, 1.28-1.58 ms | 279/286 fps, 1.23-1.57 ms | - (not throttled: no change) |
| L36 one guest tile | 720p 1x grid, 3 boots each, alternating | 288.9 / 290.0 / 291.2 (3,288 draws) | 315.8 / 317.0 / 316.3 (2,647) = **+9.1%** | masked 0.004-0.008% (noise one/one 0.013%), seam rows same |
| L36 one guest tile | 1440p 4x grid, 1 boot each | 250.1 | 254.3 | 0.56% unmasked (crowd) |
| Cockpit vs chase (why cockpit is slower) | user drive, Catalunya opening straight, 1440p 4x, one tile on | chase ~243 fps / ~2,630 draws | cockpit ~215 / ~2,950 (the mirror's scene) | - |
| L42 mirror threshold 2000 | same drive, cockpit, 8 aligned windows | 214 fps / 2,943 draws | 221 / 2,864 (+3.5%) | mirror 0.2% of pixels (grid); rejected by the user |
| L41 half-rate mirror | grid cockpit, live, 5 rounds (and a noisier 4) | 253.9 | 258.8 (+1.9%; +5.1% in the noisier run) | 0 pixels |
| L41 half-rate mirror | user drive, same straight (traffic differed) | 214 | ~240 (+12%) | "looked good" |
| L43 lower-resolution mirror | grid cockpit, perf log, boots at 1 / 0.5 / 0.25 / 1 | 258.0 fps, GPU 3.72 ms | 256.8 / 258.0 / 259.8 fps, GPU 3.74 / 3.72 / 3.70 | mirror blurrier: no gain |

## Session 25 (2026-11-11): L12 asynchronous frame end
| Test | Switch | Setting | Off | On | Change | Rounds | Picture |
|---|---|---|---|---|---|---|---|
| L12 async present | `REGPU_ASYNC_PRESENT` (live) | 720p 1x (CPU-bound) | 216.5 | 255.2 | **+17.9%** | 5/5 | 0 pixels |
| L12 async present | `REGPU_ASYNC_PRESENT` (live) | 1440p 4x | 232.4 | 242.3 | **+4.3%** | 5/5 | 0 pixels |
| All constants copied (diagnostic) | `REGPU_ALL_CONSTANTS` (live) | 720p 1x | 226.2 | 212.1 | -6.2% | 0/3 | 0 pixels |
| Vertex cache memo | `REGPU_NO_VERTEX_MEMO` (live) | 720p 1x | 269.7 | 279.3 | **+3.6%** | 5/5 | 0 pixels |
| Vertex cache memo | `REGPU_NO_VERTEX_MEMO` (live) | 1440p 4x | 234.6 | 234.7 | 0 | 3/4 | 0 pixels |
| Pass-scale snapshot | old/new exe, alternating boots | 720p 1x | 279.3, 280.8 | 288.3, 284.0 | **+2.2%** | 2/2 | byte-identical (live) |
| Upload stable reuse | `REGPU_UPLOAD_STABLE_REUSE` (live, not adopted) | 720p 1x, perf log on | 207.7 | 210.2 | +1.2% | 4/4 | 0 pixels |
Frozen CrownJewel grid, minimized, `out/perf/s25/` and `out/perf/s25b/`. CPU-bound grid after part 2: ~288 fps. L12 is the FM3 default (`FM3_SYNC_PRESENT=1` off). The last row
shows how much constant writes cost (3x the bytes: -6.2%), i.e. not the draw thread's main cost.

## Session 24 (2026-11-11): CPU side
- CPU-bound grid (1280x720, msaa 1, minimized): ReGPU's draw thread 95% of a core (the limit), CP 57%, game t3 35%,
  t7 31% (`tools/perf/threads.ps1`). Lock and getenv cuts (`regpu-draw-thread-cuts`, host `FM3_WRITEBACK`
  throttle): 211.7/217.3 -> 220.7/220.2 fps (two boots each). Shader-load repeat skip (`regpu-shader-repeat-skip`):
  +2.4% live (5/5, 0 pixels). L28 register-write skip (`regpu-reg-skip`): +8.1% live (5/5, 0 pixels). Session total:
  214.5 -> 237.6 fps (+10.8%). Tried and reverted: pixel-constant block reuse -2.4%. Profile details: NOTES session 24 part 3.

## Session 24 (2026-11-11): L6 shader profile, L30 fetch variants
- RGA (`tools/rga_shaders.ps1`, RDNA3 gfx1100): car paint `e41e183a423e6f1b` 80 VGPRs, 117,056 bytes of code (236 sample
  instructions for 17 fetches, 1,064 branches); `510c814e1a4ef99e` 58 VGPRs, 97,300 bytes; `b117fbafcc1fa980` 46 / 33,040;
  `f7841c0a8245eabf` 48 / 32,672; `8be0d4632b7d3122` 12 / 1,372.
- L30 variants (all lean fetches plain): car paint 72 VGPRs / 20,412 bytes, b117 46 / 14,204, f784 38 / 13,220.
- Frozen grid, live A/B of `REGPU_FETCH_VARIANTS` with `REGPU_GPU_PASSES=1`: GPU frame 4.38/4.42/4.44/4.44 ms off vs
  4.28/4.32/4.32/4.32 on (-0.11 ms, -2.5%). fps: -0.3% (5 rounds) and +1.2% (3/3 rounds) in two sessions (CPU ceiling).
  Pictures: 0 pixels differ (live, same session). Now the default.
- **User drive with it (CrownJewel career race, ~3 laps, log 25.4-196 s, same settings as the S23 drives):** 300.6 fps
  average, 1% low 195, lowest 60-frame stretch 182, none under 180 (S23 drive 1: 258.0 average, lowest 91). Not a
  clean A/B (different laps and traffic; drive 1 also had plain multiply), but every 10 s block of lap 1 was higher
  (251/262/222/275 vs 235/235/211/236). One compile in the race: `ps dd492c7cb9ec6a9f` at 59 s (a fetch variant, in
  the background), the same shader whose first build made drive 1's worst dip (91 fps); this time no dip. No errors,
  no failed compiles, long frames only during loads. Graph: `out/perf/s24/drive-l30.html`, log `drive-l30.log`.

## Summary (read first)
**Kept / made default**
- **Lean fetch 1 + 10-bit copies are now defaults** (`src/gpu_host.cpp`, after the user's race drive; `FM3_NO_SHADER_SHORTCUTS=1` = off). Frozen grid with them: 225.6 fps vs ~221.
- **L11 ring wait sleeps (`FM3_RING_WAIT=2`) is now the default**: same fps GPU-bound and CPU-bound, same frame times,
  identical pictures, **the game uses 17-19% less CPU** (3.4 -> 2.7-2.8 cores). `FM3_RING_WAIT=0` = old spin (live).
- **L15 laptop GPU pick**: driver exports in `main.cpp` (not testable on a one-GPU PC).
- Confirmed defaults: **L1 vertex arena (+10.5%)**, **L2 alpha-test-free shaders (+4.1%)**, the draw thread (+17.5%).

**Fixed (issue found and its cause turned off)**
- **The Mild/Potato/Low/Extreme presets broke the race picture** (washed-out white rectangles; menus had been fine).
  Cause: the helper-pass pieces (`REGPU_HELPER_SCALE`, `REGPU_FREEZE_HELPERS`). They are out of every preset now;
  all six presets render correctly (phase 7).

**Gains not made default (your call, each needs a drive)**
- `REGPU_LEAN_FETCH=1` + `REGPU_PLAIN_MUL_PS=1` + `REGPU_COPIES_1010102=1`: **+5.1%** at 1440p 4x (230.9 vs 219.6, 3
  rounds), but **plain multiply turns the crowd's flags black** (found by the user while driving; see the end): out. Alone: lean fetch 2 +3.6%, plain multiply +1.8-2.7%, 10-bit copies +2.3%.
- Turbo preset: +3.5-3.7%, picture within noise.
- `REGPU_ANISO_FROM_FETCH=1`: +1.7%, softer distant textures (closer to the console).
- `REGPU_SUBMIT_DRAWS=512`: +5.3% when CPU-bound (720p 1x) but -5.7% at 1440p 4x: for CPU-bound machines only.

**Losses (leave off)**: L3 optimised shaders -6%, I1 hidden-part cull -1.0 to -1.3%, L13 draw-thread pieces -2 to -6%.
**No effect here**: I4a index hash once, I5 stable upload reuse (+1.2% only when CPU-bound), alpha-to-mask as a test,
lean vertex fetch.

**What limits this PC now**: the GPU frame is 4.39 ms (68% the main scene pass; the car paint shader alone 0.5 ms at
~1.3 ns a pixel, 17 texture fetches) and the CPU side tops out at ~231 fps, right next to it. Next code leads: L30
(fetch variants for the car shaders), then CPU-side cuts (L8, L28) to lift the ~231 ceiling. The test grid is the
same car line-up on every boot (checked: rosters, pictures); only the crowd animates differently between boots.

**Picture report:** `out/perf/s23/report.html` (open it in a browser): all 55 comparisons with a swipe slider, side by
side and a red difference picture each, numbers and verdict per card. Rebuild: `python -I tools/perf/report_s23.py
pairs out/perf/s23`, `tools/perf/make_pairs.ps1 -Dir out/perf/s23 -Pairs pairs.txt`, `python -I tools/perf/report_s23.py
html out/perf/s23`.

## How to read the numbers
- **Live A/B** (same game session): 4 interleaved rounds of off/on, 8 `status` fps samples each after 3 s. The frozen
  world makes the GPU work constant: a null A/B (an unused variable flipped) gave 214.3 vs 213.8 (-0.2%) with
  pixel-identical shots, so **about 0.5% is already real** here. Shots of the same session are exact.
- **Start-time A/B** (a restart per side): 2 boots per variant (the first may compile shader variants; the second
  is the clean one), a defaults boot between every two variants. Boot-to-boot spread of the defaults: see below.
  Pictures across boots are compared with the crowd masked out (`pngdiff_mask.py`).

## Is the test grid the same on every boot? (user's question)
3 boots, `FM3_AI_POOL_LOG=1` (`boot1..3.png`, rosters `boot1..3-roster.txt`, diff pictures `d12.png`, `d23.png`,
`d23cars.png`):
- **The grid line-up is identical**: the same 7 AI drivers (ids 1, 26, 15, 16, 5, 30, 31) in the same order, the same
  cars on the same grid spots. The roster files differ only in object addresses and frame numbers.
- **Freeze point**: 9.001 / 9.003 / 9.003 s of the countdown clock; 22.7-25.8 s after launch.
- **Picture**: 0.44-0.59% of pixels differ between boots, almost all in things that animate on their own clock: the
  grandstand crowd, the pit crew, the flags. Outside those, single pixels along the AI cars' edges (they still
  settle on their suspension at that moment; the freeze lands up to ~3 ms apart) and one AI tail light lit in one
  boot only. Within one boot two shots seconds apart are byte-identical.
- **Draws**: 3283 (boots 1, 2) vs 3262 (boot 3), and the count also moves between 3255/3262/3283 within one frozen
  session: some draws come and go with something other than game time (to look into; it does not move fps).
- **fps**: 207.0 (boot 1, first samples still warming: settle raised from 8 to 15 s afterwards), 219.5, 217.4.
- **Fixed frame steps (`FM3_FIXED_DT=1`) don't make boots identical** (`fixeddt1/2.png`, `dfix_cars.png`): the freeze
  lands at exactly 9.000 s, but the crowd still differs (0.48% of pixels) and the single-pixel specks on the AI cars
  stay (48 pixels outside the crowd vs 68 without). So they don't come from the freeze moment; something differs per
  boot. **Noise floor across boots: ~50-70 scattered pixels outside the crowd.** Exact picture comparisons: within
  one game session (live switches).

## Phase 1: live switches (one boot, defaults 213.5 fps at the start, 213.4 at the end)
| Lead | Switch | off -> on | Rounds on faster | Picture (off vs on) | Verdict |
|---|---|---|---|---|---|
| null | `FM3_NULL_AB` | 214.3 -> 213.8 (-0.2%) | 0/4 | identical | noise floor |
| L1 vertex arena | `REGPU_NO_VERTEX_ARENA` (on = arena off) | 204.2 -> 184.7 (**arena worth +10.5%**) | arena faster 4/4 | identical | **keep on** (default) |
| L2 alpha-test-free shaders | `REGPU_SPLIT_ALPHA_TEST` | 204.6 -> 213.0 (**+4.1%**) | 4/4 | identical | **keep on** (default) |
| I4a index lists hashed once | `REGPU_INDEX_HASH_ONCE` | 213.0 -> 212.7 (-0.1%) | 0/4 | identical | no gain here (CPU-side; GPU-bound PC) |
| I4b 2_10_10_10 copies in 10 bits | `REGPU_COPIES_1010102` | 213.1 -> 218.0 (**+2.3%**) | 4/4 | 0 pixels over 8, mean 0.56, worst 2 | **gain; picture differs by rounding only** (candidate default, needs a drive) |
| I5 stable upload reuse | `REGPU_UPLOAD_STABLE_REUSE` | 213.2 -> 213.2 (0%) | 1/4 | identical | no gain here |
| V1 piece: alpha to mask as a test | `REGPU_A2M_AS_TEST` | 213.1 -> 213.0 (0%) | 1/4 | identical | no gain (applies at 1x MSAA only) |
| I8 anisotropy from the fetch (live part) | `REGPU_ANISO_FROM_FETCH` | 213.1 -> 213.1 (0%) | 2/4 | 0.134% of pixels, worst 71 (texture filtering) | no gain live; the start-time test is in phase 2 |
| I1 hidden-part cull (1 px) | `REGPU_DRAW_CULL` (env) | 213.2 -> 210.4 (**-1.3%**) | 0/4 | identical; 6-shot burst with it on: no flicker | **loss**: the per-draw queries cost more than they save here |

Shots: `<id>-off.png` / `<id>-on.png` (`AA`, `L1`, `L2`, `I4a`, `I4b`, `I5`, `A2M`, `I8live`, `I1`), `I1-burst0..5.png`,
`P1-base.png`, `P1-base-end.png`.

## Phase 3: where the GPU time goes (measurement leads L26, L4, L6 first part)
Files: `out/perf/s23/P3-gpu-passes.txt` (`REGPU_GPU_PASSES=1`), `P3-gpu-shaders.txt` (`REGPU_GPU_SHADERS=1`),
`P3-gpu-stats-counts.txt` (`REGPU_GPU_PASSES=2` + `REGPU_DRAW_WORKERS=0`, new patch `regpu-gpu-stats`).
- **GPU frame 4.39 ms** on the frozen grid. The main scene pass (`colour 500000 depth 400`, 1873 draws) is
  **2.99 ms = 68%**. Everything else is small: the stretch after the main resolve 0.42 ms (596 draws: the labels are
  "from this mark to the next", so a resolve label carries the draws after it), full-screen passes 0.10 ms, each
  other pass/resolve 0.03-0.09 ms.
  -> **L24 (hardware MSAA resolve): dropped**, resolves are not where the time is. **L26** answered for this scene:
  draws, not copies/resolves (FH1R's 30-37% in transfers does not apply to FM3's grid).
- **Overdraw (L4)**: 23.8 M pixel-shader invocations a frame = **6.44 per output pixel**. Main scene pass 9.6 M =
  2.6 per pixel; most of it is one cheap shader (`ps 2B7D3122`: 7.7 M invocations, 0.14 ms). Two full-screen post
  shaders (`B0C87A00` 3.7 M = one screen, `pass ... depth 2D0` 4.3 M).
- **The car paint (`vs 5AEF757B ps 423E6F1B`) is the most expensive shader: 0.50 ms for 0.39 M invocations,
  ~1.3 ns per shaded pixel** (the cheap one: ~0.02 ns). Its invocations are about the area the cars cover, so it is
  not overdraw: **a depth pre-pass would not pay (L4 closed)**; the cost is the shader itself (math/texture per
  pixel). Next in line: `CC1FA980` 0.28 + 0.09 ms (171 draws), `8245EABF` 0.17 ms, `1A4EF99E` 0.16 ms.
  -> Targets for I2 (lean fetch), I3 (plain multiply), L30 (fetch variants) and a shader profile (L6, RGP).
- With timing on the game runs at 219 fps (passes) / 194 (per shader); with statistics on at 32 fps (a query
  around every draw): measurement modes only, all off by default.

## New code this session
| Lead | What was built | Switch | Result |
|---|---|---|---|
| L11 ring wait without spinning | `src/hooks/ring_wait.cpp`: after each round of D3D's wait loop (`sub_824AAA68`) that goes on waiting, `1` = `SwitchToThread`, `2` = sleep ~100 us on a high-resolution timer | `FM3_RING_WAIT` 0/1/2, live (`Fm3 "env FM3_RING_WAIT 0"`); **default 2 since the end of this session** (phases 4 and 8) | 1440p 4x, 3 interleaved rounds: spin 220.5 fps / **3.39 cores**; yield 219.5 / 3.41; **sleep 220.1 fps / 2.73 cores (-0.66 core, -19% CPU, fps unchanged)**; shots identical. 720p 1x (phase 4): same fps, -17% CPU; frame times unchanged (phase 8) |
| L15 laptop GPU pick | `src/main.cpp`: exports `NvOptimusEnablement = 1`, `AmdPowerXpressRequestHighPerformance = 1` | none (always) | Not testable here (one GPU); harmless on desktops. Hybrid laptops should now start on the dedicated GPU |
| L4 overdraw count | ReGPU patch `regpu-gpu-stats`: pipeline statistics around every draw | `REGPU_GPU_PASSES=2` (+ `REGPU_DRAW_WORKERS=0`), start-time | Measurement tool; results in phase 3 |

## Dropped or not testable
- **L5 sampler table**: already in our ReGPU as `REGPU_LEAN_FETCH` (the samplers as an indexed table, FM2's idea);
  tested as I2 in phase 2.
- **L24 hardware MSAA resolve**: resolves are a small share of the frame (phase 3).
- **L4 depth pre-pass**: the expensive car shaders are not overdrawn (phase 3).
- **I6 `REGPU_OPTIMIZE_LEVEL`**: not in our ReGPU version (upstream only).
- **V8 car reflection cube rate**: FM3's `fastenvmaprender` / `cubemap` developer options have no reader (dead, like
  the other `fast*` flags); no cheap switch. Would need the env-map pass found and throttled in code.

## Phase 2: start-time switches (2 boots each; defaults boots 219.6, 220.7, 214.8, 220.5, 221.2, 220.7, 221.7, 220.5)
Shots `<id>-boot1.png`, `<id>-boot2.png`, defaults `P2-BASE-defaults-<n>.png`. "Picture" = pixels over 8 outside the
crowd vs the nearest defaults boot; defaults against defaults give 170-330 (the noise floor across boots).
| Lead | Variant | boot 1 / boot 2 fps | vs ~220.5 | Picture | Verdict |
|---|---|---|---|---|---|
| L3 | `REGPU_OPTIMIZE_SHADERS=1` | 219.6 / **207.3** | **-6%** once the optimised shaders are in (boot 2) | noise | **loss**: driver already optimises; fxc level 1 output runs slower here |
| I2 | `REGPU_LEAN_FETCH=1` | 227.0 / 225.7 | **+2.6%** | noise | gain; confirmed stacked in phase 5 |
| I2 | `REGPU_LEAN_FETCH=2` | 229.9 / 218.6 (boot 2 unsteady, 209-225) | ? | noise | rerun in phase 5 |
| I2 | `REGPU_LEAN_VFETCH=1` | 221.7 / 220.7 | +0.3% | noise | no gain |
| I3 | `REGPU_PLAIN_MUL_PS=1` | 226.5 / 226.5 | **+2.7%** | noise (100-157) | gain; needs a drive (0*inf cases: sun, glare, paint) |
| I8 | `REGPU_ANISO_FROM_FETCH=1` | 224.3 / 224.3 | +1.7% | **12% of pixels** (filtering as the game asks: softer ground at a distance, closer to the console) | small gain, visible: your call |
| L9 | `REGPU_SUBMIT_DRAWS=512` | 207.7 / 208.0 | **-5.7%** | noise | **loss** (GPU-bound, as in FM2) |
| L13 | `FM3_DRAW_THREAD_PIECES=1` | 214.5 / 216.1 | -2.3% | noise | loss (as in S15) |
| ref | `FM3_NO_DRAW_THREAD=1` | 182.2 / 181.7 | -17.5% | noise | the draw thread is worth 17.5% here |
| V1 | preset Turbo | 229.8 / 227.6 | **+3.7%** | noise (228 / 721) | gain, picture intact |
| V1 | preset Mild | 228.2 / 229.7 | +3.8% | **41% changed: BROKEN** | see below |
| V1 | preset Potato | 239.8 / 240.1 | +8.8% | **70% changed: BROKEN** | see below |
| V1 | preset Low | 240.8 / 237.9 | +8.6% | **49% changed: BROKEN** | see below |
| V1 | preset Extreme | 238.4 / 240.1 | +8.5% | **41% changed: BROKEN** | see below |

**Issue found: the Mild, Potato, Low and Extreme presets break the race picture** (`sheet-presets.png`: defaults,
Turbo, Mild, Potato, Low, Extreme). Large washed-out white rectangles and smeared bands over the track, the stands and
the cars. They had only been checked in menus (S19). All four shrink "helper" passes (`REGPU_HELPER_SCALE` 0.5/0.25:
any pass on a surface narrower than 1280), and part of FM3's race scene passes that test, so it renders small and
is read back full size. Phase 6 isolates the piece. The presets were already off by default; Turbo is fine.

## Phase 5: the promising pieces alone and stacked (3 rotated rounds each, 1440p 4x)
Shots `P5-<set>-r<round>.png` (`+` written as `_`). Defaults: 222.1 / 221.2 / 215.6 = 219.6.
| Set | Rounds | Mean | vs defaults | Picture (outside the crowd vs that round's defaults) |
|---|---|---|---|---|
| `REGPU_LEAN_FETCH=1` | 227.1 / 226.5 / 217.4 | 223.7 | +1.8% | noise (91-227) |
| `REGPU_LEAN_FETCH=2` | 228.3 / 229.5 / 224.6 | 227.5 | **+3.6%** | noise (84-245) |
| `REGPU_PLAIN_MUL_PS=1` | 227.0 / 221.9 / 221.6 | 223.5 | +1.8% | noise (66-259) |
| lean 2 + plain multiply + 10-bit copies | 230.5 / 226.4 / 228.7 | 228.5 | +4.1% | noise (65-267) |
| **lean 1 + plain multiply + 10-bit copies** | **230.1 / 230.9 / 231.8** | **230.9** | **+5.1%** (steadiest) | noise (101-260) |

**The CPU ceiling:** at 1280x720 without MSAA the same grid runs only ~230 fps (phase 4), so with the GPU pieces
stacked the frame meets the CPU side's limit (ReGPU's command processor and draw thread). Beyond ~+5% on this PC,
GPU savings need CPU-side savings too.
**Candidate default (your call; needs a drive: sun/glare/paint for plain multiply, liveries for lean fetch):**
`REGPU_LEAN_FETCH=1` + `REGPU_PLAIN_MUL_PS=1` + `REGPU_COPIES_1010102=1` (= Turbo without lean-v2/v-fetch/hash-once/
alpha-to-mask/cull). Not made default in this session: the frozen grid can't show issues that only show while driving.

## Phase 6: which preset piece breaks the race picture (`sheet-p6.png`, shots `P6-<set>.png`)
| Set | fps | Pixels changed outside the crowd vs defaults | Picture |
|---|---|---|---|
| `REGPU_HELPER_SCALE=0.5` alone | 227.8 | 47% | **broken**: a washed-out rectangle over the upper right (an exposure/bloom-type pass counted as a "helper", shrunk, read back at the wrong size) |
| `REGPU_HELPER_SCALE=0.25` alone | 228.8 | 70% | **broken** (the same, larger) |
| `REGPU_FREEZE_HELPERS=3` alone | 227.9 | 61% | **broken**: everything above the EDRAM tile seam (~2/3 down) washed out (a pass that runs once per tile, frozen for one tile) |
| Mild without helper scale | 230.0 | 24% | fine (softer: 67% scale + FSR 1, by design) |
| Potato without helper scale, with freeze | 235.6 | 63% | broken (the freeze) |
| Potato without both | 229.2 | 5% | fine (2x anisotropy, by design) |
| defaults | 214.6 | 0.008% | reference |

**Action taken (as asked: turn off the issue-causing piece):** `src/gpu_host.cpp` presets no longer set
`REGPU_HELPER_SCALE` or `REGPU_FREEZE_HELPERS`; Mild/Potato/Low/Extreme are now Turbo + render scale (FSR 1) + lower
anisotropy. Launcher texts updated. Both ReGPU switches still exist for whoever fixes them (ReGPU's helper-pass test is
"surface narrower than 1280"; FM3 needs its exposure/bloom chain and per-tile passes kept out). Phase 7 re-checks the
presets after the change.

## Phase 4: CPU-bound (1280x720, MSAA 1x; settings.ini edited for the run and restored)
Defaults 230.9 / 231.2 / 232.7 fps: **the CPU side's ceiling on this PC is ~231 fps**, barely above the 1440p 4x
grid (~220), so this PC is close to both limits at once. Live A/Bs: 4 interleaved rounds; start-time: 2 boots.
| Lead | Result | Compare 1440p 4x |
|---|---|---|
| null A/B | 232.1 -> 231.8 (-0.1%) | noise floor |
| **L11 `FM3_RING_WAIT=2`** | 231.0 -> 230.7 fps (same), **CPU 3.40 -> 2.81 cores (-17%)**; mode 1: 231.2 fps, 3.52 cores | same at both: no fps cost, ~1/5 less CPU |
| L9 `REGPU_SUBMIT_DRAWS=512` | 243.9 / 242.8 = **+5.3%** | -5.7%: helps CPU-bound machines only |
| I5 `REGPU_UPLOAD_STABLE_REUSE` | +1.2% (4/4 rounds) | 0 |
| L1 vertex arena | arena worth +1.9% (4/4) | +10.5% |
| I4a `REGPU_INDEX_HASH_ONCE` | +0.2% | 0 |
| I4b `REGPU_COPIES_1010102` | -1.5% (rounds 220-232: noisy) | +2.3% |
| I1 `REGPU_DRAW_CULL=1` | -1.0% | -1.3% |
| L13 `FM3_DRAW_THREAD_PIECES=1` | 216.9 / 215.8 = -6.5% | -2.3% |
| `FM3_NO_DRAW_THREAD=1` | 180.1 / 181.0 = -22% | -17.5% |
| L35 texture reloads | 0 in 5 s (frozen grid) | |
Shots `P4-*.png`; the five heaviest pixel shaders as HLSL: `ps_<hash>.hlsl` (car paint `e41e183a423e6f1b`: 17 texture
fetches through the full fetch helper, forward-only control flow on the game's constant booleans).

## Phase 7: the presets after the fix (`sheet-p7.png`, shots `P7-<preset>.png`)
**All six render correctly in the race now** (no white rectangles).
| Preset | fps | Pixels changed outside the crowd |
|---|---|---|
| defaults | 222.8 | |
| Turbo | **230.5 (+3.5%)** | 59 (noise) |
| Mild | 216.5 (samples 213-220, unsteady) | 24% (67% scale + FSR 1: softer, by design) |
| Potato | 215.9 (samples 211-229, unsteady) | 5% (2x anisotropy) |
| Low | 229.7 | 27% (59% scale) |
| Extreme | 233.6 | 27% (50% scale, no anisotropy boost) |
On this PC the presets beyond Turbo can't gain (the CPU-side ceiling is ~231 fps, and FSR 1 adds a pass); they are for
weaker GPUs, where the scale cuts count. Mild/Potato's unsteady samples look like one-off pipeline builds in that boot
(not checked further).

## Phase 8: L11 frame pacing (spin vs sleep, 4 interleaved rounds of 8 s, `frametimes`)
| Mode | avg ms | p50 | p99 | p99.9 | max | over 20 ms |
|---|---|---|---|---|---|---|
| 0 spin | 4.527-4.528 | 4.44-4.46 | 5.36-5.50 | 5.77-5.92 | 5.93-6.50 | 0 |
| 2 sleep | 4.532-4.541 | 4.45-4.49 | 5.36-5.41 | 5.73-5.80 | 5.75-5.93 | 0 |
Same pacing (the sleep's tail is if anything a little tighter). With phase 4 this made `FM3_RING_WAIT=2` the default.
Final build sanity boot (ring wait 2 by default, presets fixed): 221.3 fps, frozen at 9.003 s (`final-defaults.png`).

## Drive test by the user (+5.1% set: lean fetch 1, plain multiply, 10-bit copies; `play.cmd`, perf log off)
CrownJewel, 2560x1440 4x, cap 360. Graph: `out/perf/s23/drive-test.html` (`tools/perf/drive_graph.py`, fps per 60
frames from the log's swap stamps; log kept as `out/perf/s23/drive1-fm3.log`).
| Segment | Average | 1% low (60-frame stretches) | Lowest stretch | Stretches under 180 fps |
|---|---|---|---|---|
| Career race, 2 laps and a bit (GO to the finish, log 27.6-160.4 s) | **258.0 fps** | 180 | 91 (log 59.3 s) | 4 |
| Time trial (log 206.4-276.9 s) | **331.6 fps** | 283 | 281 | 0 |
**The stutters were one-time shader builds.** In the first lap (log 47-59 s) four pixel shaders were compiled
(300-793 ms each, background) and ~30 pipelines built (1766 -> 1795); the worst dip (59.3 s) follows the 793 ms
compile of `ps dd492c7cb9ec6a9f`. All five compiled shaders are old ones (microcode cached since 11-07): what was new
is their lean-fetch/plain-multiply variant, which the shader cache keys apart. My grid runs compiled only the
variants visible from the grid. The time trial on the same track had no stutter (variants cached by then). One dip
without a compile (log 35.2 s) coincides with track textures streaming in (786 -> 1111 textures over the next 15 s):
the game's own streaming, not the switches. The race end (log ~160 s) also refilled the vertex arena ("full,
starting over"), but that is the finish/menu transition, not driving.

## Issue found by the user while driving: plain multiply turns the crowd's waving flags black
`REGPU_PLAIN_MUL_PS=1` (I3): the flags the spectators wave render as black silhouettes. It is in the grid shots too
(`flags-crop.png`: the left grandstand from `P5-defaults-r1`, `P5-plainmul-r1`, `P5-lean1-r1`, `I4b-on`: only plain
multiply is black). A shader there relies on the console's 0 * inf = 0. **My picture check missed it:** the cross-boot
diffs masked the crowd areas (they always differ between boots), and the flags sit inside that mask. Lesson: for
start-time switches, also look at the masked areas by eye (or compare a crop), and prefer live switches.
**Action:** plain multiply is out of `play.cmd`'s test set and out of the Turbo preset (so out of every preset);
`src/gpu_host.cpp`. Without it the grid set is lean fetch 1 + 10-bit copies (phase 5 alone: +1.8% and +2.3%; not
measured together). A per-shader fix (Pinyon's idea: IEEE multiplies everywhere but the shaders that need 0*inf = 0,
or a NaN-to-0 scrub on their outputs) could bring the gain back.

## User's recorded race (lean fetch 1 + 10-bit copies) and the camera views
Log `out/perf/s23/drive2-fm3.log`, graph `drive-test-2.html`, video overview `vid2/overview.png`; log = video + 33.8 s.
- No shader compiles in the race: the five variants were built at boot from last run's pipeline list.
- Race (recording + overlay running): 246 fps average GO to pause. By view in the recording: chase lap 1 231 fps,
  first cockpit stint 196, chase + rewind 217, cockpit laps 2-3 232. The cockpit difference follows the traffic (3rd
  with cars close vs leading), not the view alone.
- **Frozen grid, every view, 3-4 interleaved readings each** (RB cycles 5 views; `cam2-sheet.png`, `cam2.txt`):
  cockpit 221.8 (221.5-222.0), chase 223.1, far chase 221.5, hood 233.0, bumper 231.9. **Cockpit costs the same as
  the chase cameras**, ~5% more than hood/bumper (the interior). The first switch into cockpit read 192.8 fps once
  (a one-time cost on entering it), never again.
- Audio (open issue G): no silences in the recording's sound (none below 20% of the surrounding level), 8 short dips
  to 30-50% (could be gear changes). The log shows the streams behind it: hundreds of XMA codec reopens and
  discontinuity flushes every 5 s, ~26,000 decoded samples dropped at stream restarts per 5 s. Session 22's lead
  stands: FM3 applies no XMA trims; port FM2's trim handling.
