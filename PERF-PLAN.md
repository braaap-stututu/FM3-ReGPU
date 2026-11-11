# Performance plan

Every performance idea from NOTES.md, RESEARCH.md and OPEN-ISSUES.md (to session 22, 2026-11-10) that is **not yet
built, or built but not measured in a race**. Sorted by what it does to the picture, then by expected gain.
Sources and older numbers stay in those files; this file is the to-do list. Update it when an item is measured,
adopted or dropped.

## Where we stand (read first)
- **Since session 24 the GPU and the CPU limit this PC almost equally** (frozen grid ~225 fps at 1440p 4x; CPU-bound
  720p 1x ~238 fps, ReGPU's draw thread ~93% of a core). Gains now need both sides; see "Next up" below. The older
  bullets are kept for history.
- **On the user's PC (RX 7900 XTX, 1440p 4x MSAA) the GPU is the limit**: ~96% busy on the race grid (4.8 of 5.0 ms).
  The game's render thread t7 mostly spin-waits on ReGPU (session 20 profile), and cutting cheap draws (crowd, car
  LODs) moves nothing. Only **GPU-side** items can raise fps there; CPU-side items help weaker CPUs, laptops and power.
- The race vs time-trial gap (~198 vs ~311 fps) is ~1.3 ms of **pixel shading of the seven AI cars' materials**
  (~0.19 ms a car), not vertices or duplicate draws.
- Current race-grid baseline: ~213 fps (CrownJewel, 8th, paused, 1440p 4x, after `regpu-split-alpha-test`).
  Run-to-run spread is ±3%: use interleaved rounds (`tools/fm3_ab.ps1`) for anything small.

## Session 23 results (2026-11-11): see [PERF-RESULTS.md](PERF-RESULTS.md)
Measured hands-off on the frozen test grid (`grid_test=1`). Settled: L1 keep (+10.5%), L2 keep (+4.1%), L3 loss (-6%),
L4 closed (car shaders not overdrawn: no pre-pass), L5 = I2 (already in ReGPU), L9 loss (-5.7%), L11 built
(`FM3_RING_WAIT=2`: -19% CPU, same fps), L13 loss (-2.3%), L15 built (driver exports), L24 dropped (resolves are a
small share), L26 answered (68% of the GPU frame is the main scene pass), L35 closed (0 reloads on the frozen grid),
I1 loss (-1.3%), I2 lean1 +2.6%, I3 +2.7%, I4a 0, I4b +2.3% (rounding-only picture change), I5 0, I8 +1.7% (visible),
V1 Turbo +3.7%; **Mild/Potato/Low/Extreme break the race picture** (white rectangles). I6 not in our ReGPU. V8 no
switch. Open: L30 (the car paint makes 17 fetches through the full helper), L6 (RGP profile), the CPU-bound runs.

## Session 24 results (2026-11-11): see PERF-RESULTS.md "Session 24" and NOTES session 24 parts 1-3
Built and default: L16 (first-boot static shader scan + pipeline seed with vertex variants, `regpu-seed-variants`:
cold boot waits ~35 s, then no race hitch), L30 (`regpu-fetch-variants`: plain-fetch pixel-shader variants, car paint
117 -> 20 KB of GPU code, GPU frame -2.5%, byte-identical, user drove it: normal), L28 (`regpu-reg-skip`, +8.1%
CPU-bound), shader-load repeat skip (`regpu-shader-repeat-skip`, +2.4%), draw-thread lock cuts
(`regpu-draw-thread-cuts`, ~+2.8%). L6 done offline with RGA (`tools/rga_shaders.ps1`). **CPU-bound grid (720p 1x):
214.5 -> 237.6 fps (+10.8%)**; ReGPU's draw thread is still the CPU limit (~93% of a core), the CP ~55-62%.
Tools: `tools/perf/threads.ps1` (CPU per thread), `FM3_SAMPLE_THREAD=name:gpu draw worker` (+`FM3_SAMPLE_CALLERS`,
`FM3_SAMPLE_LINES`, `FM3_SAMPLE_RVA`, all live), `FM3_MINIMIZED=1` (all automated boots), `REGPU_DUMP_ROOTSIG`.

## Session 25 results (2026-11-11)
Draw-thread profile with whole call stacks (sampler fixed: `FM3_SAMPLE_CALLERS=N`, up to 16 frames, stack copied and
unwound after the thread resumes; a module+offset for frames without symbols; " ~ " = nearest fm3.exe return addresses
on the stack): ~10% of the draw thread was **waiting at the frame end**: ~7% asleep inside AMD's driver in `Present`,
~2.5% waiting for the replay thread (`RecordedList::Real` at the swap). **L12 built and default** (patch
`regpu-async-present`): CPU-bound grid +17.9% (216.5 -> 255.2 live), 1440p 4x +4.3% (232.4 -> 242.3), 0 pixels.
Explained, not fixable cheaply: the "~PsAlias 16-byte store" (7%) is the draw's common exit, the first store after the
draw's burst of writes (the stall is the store buffer draining); every-constant copying (3x the bytes) costs only -6.2%,
so constant writes are a small share. GPU time at 720p 1x is ~3.1 ms of a ~4.3 ms frame.

Part 2 (patch `regpu-draw-thread-memos`): vertex-cache memo +3.6%, pass-scale snapshot +2.2%, timer/overlay uploads
without the replay wait; **CPU-bound grid ~288 fps**. Upload hashing: 6 MB/frame, 75% re-checks of reused copies;
`REGPU_UPLOAD_STABLE_REUSE` +1.2% (off: stale-for-a-frame risk). L10: 18% of draws are second-tile copies (the upside).

## Next up (from session 25; ask the user which)
1. **Cockpit cam performance** (user, open; noticeably better with L12 per the user, session 25): measure per camera view while the race runs (GPU passes per view,
   `threads.ps1`), GPU or CPU? Then the matching lead below.
2. **Draw thread, still the CPU limit:** ~~L12~~, ~~VertexCache lock~~, ~~pass-scale helpers~~ (done, session 25);
   upload hashing measured (stable reuse +1.2%, user's call on the risk); **L10 tile merge next** (18% of draws are
   second-tile copies; needs FM3's rectangle tile clears handled + the draw thread); L28's second half.
3. **GPU side on this PC:** shader `510c814e1a4ef99e` (97 KB) gets no L30 variant (some texture not plain: find which),
   sampler index as a literal in `TFetchPlain`, the remaining car paint cost (math/sampling: RGA ISA), plain multiply
   per shader (I3 without the black flags), V8 reflection cubes less often (find FM3's rate).
4. **Other machines:** L14 core gating (4 cores), V4 iGPU defaults, L17-L22 memory items, V5 SMAA, I7 16-bit shaders.
5. **Stutter:** L32 (pipelines per frame in parallel), L33 long-frame sampler, L34 SIMD untile, L29 (needs FM3's
   load-done flag).

## Legend
**Picture**
- **Lossless**: same pixels by design (check with `tools/analysis/pngdiff.py` anyway).
- **Lossless in intent**: meant to be identical, but there is a known way it could differ (a frame of lag,
  float rounding, a culling mistake). Needs a numeric picture check and a drive.
- **Visible**: a deliberate quality trade (resolution, filtering, AA, frozen passes, LOD).

**Helps on** (the kind of machine it speeds up)
- **GPU-bound**: a dedicated GPU at high resolution/MSAA (the user's PC at 1440p 4x).
- **CPU-bound**: fast GPU at low resolution, or a slow/4-core CPU (ReGPU's command processor or draw thread is the limit).
- **iGPU / APU**: shared memory, small VRAM carve-out, shared power budget.
- **Low RAM / low VRAM**: 8 GB laptops, 2–4 GB cards.
- **Stutter**: first-run shader/pipeline builds, streaming hitches; not average fps.

**Live A/B**
- **Live**: flips while the game runs (`Fm3 "switch REGPU_X 0|1"` for `REGPU_*`, `Fm3 "env NAME v"` for env-read and
  `FM3_*` switches): pause on a grid and interleave rounds in the same game session.
- **Start-time**: read once at launch: needs a restart per side (temporary `play.cmd` line or `$env:` before
  `Start-Process`); the user drives back to the same grid. Compare averages over ≥2 restarts per side.
- **Build**: needs two builds; no switch possible.
- **Planned live**: not built yet, but can be built behind a live switch (do so: it makes the A/B cheap).

---

## 1. Lossless
| # | Item | Status | Helps on | Live A/B | Expected gain / note |
|---|---|---|---|---|---|
| L1 | **Vertex arena** race A/B (large vertex buffers in VRAM, re-copied on guest writes) | Built, **on by default** since S19; never measured in a race | GPU-bound (dGPU reads vertices over PCIe without it); little on iGPU | **Live**: `REGPU_NO_VERTEX_ARENA 1` (next frame). Stale-geometry check: `REGPU_VERTEX_VERIFY 1` (logs `changed with no write seen`) while driving (tyre marks, damage) | FM2: 1.5 ms/frame on a 4070 at 1440p. If no gain on FM3, consider off (risk without benefit) |
| L2 | **Alpha-test-free pixel shaders** on other scenes | Built, default on (S20, +5% on the grid) | GPU-bound | **Live**: `REGPU_SPLIT_ALPHA_TEST 0\|1` | Check other tracks, menus, photo mode, showroom: picture and no compile errors |
| L3 | **`REGPU_OPTIMIZE_SHADERS`** (FXC level 1 in the background; `+REGPU_OPTIMIZE_THREADS`) | Switch in our pin, never measured | GPU-bound; more on weak driver compilers (Intel) | **Start-time** | Unknown (drivers re-optimise DXBC). Free test. Float reassociation could change last bits: run pngdiff |
| L4 | **Overdraw count** → depth pre-pass / ordering for car draws | Not built (measurement first: D3D12 pipeline statistics, PS invocations per pass or per car draw) | GPU-bound (the AI-car cost) | Measurement: n/a. A pre-pass: **planned live** | Only pays if car pixels are shaded several times |
| L5 | **4-entry sampler table** (FM2 ce5bf5a: 2 sample instructions per fetch instead of ~8) | Not built; root signature change in ReGPU | GPU-bound | **Start-time** (root signature and shaders built once; separate shader cache key) | FM2: 213.8 → 225.2 fps (4070, 1440p) |
| L6 | **Profile translated car shaders** (Radeon GPU Profiler / Analyzer on the 7900 XTX) | **Done (S24, RGA offline: `tools/rga_shaders.ps1`)**: car paint 80 VGPRs (fine), 117 KB code from the fetch paths -> L30. RGP frame capture not done | GPU-bound | n/a (tool) | Finds the heaviest shaders (VGPRs, occupancy) → translator fixes. Also says whether 16-bit (V-items) could pay |
| L7 | **Occlusion results resolved once a frame** (PGR4, `REGPU_RESOLVE_EACH_QUERY` = old) | Not built; touches our `regpu-zpd-tile-spans` and `regpu-worker-queries` patches | CPU-bound, some GPU | **Planned live** | PGR4 ~3%. Check sun flare at the Montserrat sun spot (answers must stay the same frame) |
| L8 | **Per-draw command-processor cuts** (FH1R ideas: reuse unchanged render-target setup, no locked `xchg` per draw, descriptor/binding reuse; cache `GameSettled`/`RendererTracing` per packet) | **Partly (S24)**: ReGPU already skips unchanged D3D state per draw (viewport, scissor, stencil ref, blend factor, tables, topology, IB, PSO); lock cuts in `regpu-draw-thread-cuts`; PS-constant block reuse tried: -2.4%, reverted. Left: descriptor/constant upload cost in `DrawImpl` | CPU-bound | **Planned live** (one switch per cut) | FH1R 5–10% on its CP; the two caches ~3% of t9 (S14 profile). Skipping unchanged constant writes measured worse in FH1R: skip that one |
| L9 | **`REGPU_SUBMIT_DRAWS=512`** | Switch in our pin, unknown with the draw thread | CPU-bound (may cost when GPU-bound) | **Start-time** | PGR4 FX +23%; FM2 (GPU-bound) −4…−17% |
| L10 | **Tile merge for FM3's race scene** (ReGPU refuses surfaces that clear each tile with rectangles; also make it work with the draw thread) | Not built (ReGPU patch, game-neutral) | CPU-bound (half the scene's commands), some GPU | Live only with `FM3_NO_DRAW_THREAD=1` at start, then `REGPU_NO_TILE_MERGE` live; with the draw thread: **planned live** | PGR4 +19%, FH1R one-pass scene +15%. Large; S18 measured 0 gain as is (scene refused); FH1R (S22): merging FH1's 3 tiles saved nothing once fixed, now off there |
| L11 | **Ring wait sleeps instead of spinning** (t7 spins ~65%; FM3 hook on `sub_824B5048` → `sub_824AAA68`, ReGPU `WakeByAddressAll` after its guest writes) | Not built | 4-core CPUs, laptops/APUs (power); 0 on desktops | **Planned live** (`FM3_*` LiveSwitch) | FH1R: render thread 86 → 45% of a core, +10% fps on a 4-core laptop |
| L12 | **Present without waiting for the frame's replay** (FH1R) | **Built, default (session 25)**: patch `regpu-async-present`, `REGPU_ASYNC_PRESENT` (FM3 default; `FM3_SYNC_PRESENT=1` off). The draw thread waited at the swap for the replay thread and then ~7% of its time inside AMD's `Present` | CPU-bound, some GPU-bound | **Live** | **720p 1x +17.9%, 1440p 4x +4.3%** (5/5 rounds each, byte-identical). Up to one more frame may queue (latency). **User drove it: straight improvement, cockpit view noticeably better**; races 264/311 fps average, lowest stretch 197 |
| L13 | **Draw-thread pieces slower than serial** (why? piece replay wait in `SplitMainList`?) | Investigation | CPU-bound | **Start-time** (`FM3_DRAW_THREAD_PIECES=1`) | Pieces were 195 vs serial 216 fps at 720p 1x (S15) |
| L14 | **Thread gating on ≤4 cores** (draw workers / draw thread on with no core check) | Not built | 4-core CPUs | **Start-time** | PGR4: draw thread −7 fps on an FX; FH1R split −11% on 4 cores. Needs a 4-core measurement (affinity mask to simulate) |
| L15 | **Laptop GPU pick** (`REGPU_ADAPTER`, or export `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance`) | Not built | Laptops with iGPU + dGPU | **Start-time** | Can be the whole difference on hybrid laptops (runs on the iGPU otherwise) |
| L16 | **Static shader scan + pipeline seed** (FM4's `.fxobj` scan behind a boot screen; seed from `pipelines.manifest`) | **Built (S24)**: static shader scan (`src/static_shaders.cpp`) + pipeline seed with vertex variants (`src/pipeline_seed.h`); seed variants + slow-shader wait (patch `regpu-seed-variants`): a cold boot waits ~35 s, 15 compiles after it, no race-start hitch (NOTES S24) | Stutter (first race, new cars/tracks) | n/a (cold-cache comparison: clear `out/user/shadercache` copy, count hitches) | Removes first-play compile stalls (up to 1.37 s at a race load, S7) |
| L17 | **Texture eviction budget** (PGR4 `REGPU_TEXTURE_BUDGET_MB`) | Not built; first log the texture count over a long session | Low VRAM, iGPU, long sessions | **Planned live** | Lossless unless it thrashes (re-uploads = hitches) |
| L18 | **Smaller upload rings on UMA** (`REGPU_RING_MB` 32–64; ring still grows) | Switch exists; measure the peak first (`g_ringPeak`) | iGPU, low RAM | **Start-time** | ~200–300 MB RAM |
| L19 | **Native small texture formats** (k_8, k_8_8, 565/1555/4444 uploaded as R8/R8G8/B5G6R5/…, swizzle in the SRV instead of RGBA8 on the CPU) | Not built; count FM3's use first | iGPU (bandwidth), loading | **Start-time** (texture cache) | 2–4x smaller for those textures |
| L20 | **Untile on the GPU** (compute shader from the mapped guest heap) | Not built (medium–large) | Stutter/streaming, CPU; iGPU | **Start-time** | Two memory passes instead of four per upload |
| L21 | **Write textures in place on cache-coherent UMA** | Not built | iGPU | **Start-time** | Skips the ring copy; driver support varies |
| L22 | **XMA codec pool RAM** (FH1R found 183 MB of unused codec contexts) | Not checked | Low RAM | n/a (memory reading) | RAM only, no fps |
| L23 | **PGO build of ReGPU**, **x86-64-v3** for the recompiled code | Not built | CPU-bound | **Build** | FH1R: PGO +6%; v3: CPU −5%, fps same; FH1R ships the PGO profile in-repo (clang `-fprofile-instr-use`, 60 s drive) |
| L24 | **Hardware MSAA resolve** (`ResolveSubresource` when scale 1, no clamp/gamma/swap; ReGPU averages 4 `Load`s in `psColor` today) | Not built (Unleashed does it; shader fallback on Intel) | GPU-bound | **Planned live** | Same box average: lossless. Small; only if L26 shows resolves matter |
| L25 | ~~`non_volatile_as_local`~~ **Dropped (S22):** Pinyon Shift measured every correct variant 20–27% slower (`.text` 55.6 → 92.3 MB). Instead: **inline register save/restore** (NN-1.2: `__savegprlr/__restgprlr` ~31% of guest calls; sim −4…−6%, render −7% per draw) and **fewer MXCSR toggles** (−29% toggles, audio −6%), both XenonRecomp `recompiler.cpp` patches | Not built | CPU-bound | **Build** | `.text` +2.2% for the save/restore change |
| L26 | **GPU time by kind of work** (extend `REGPU_GPU_PASSES`: EDRAM copies/reinterprets, MSAA resolve blits, depth blits, uploads in their own buckets) | Not built (small ReGPU patch) | Measurement for GPU-bound | **Start-time** | FH1R: render-target transfers were 30–37% of its GPU frame (surprise). Decides L4/L5/L6/L24/V3: do it before them |
| L27 | **Rebase on upstream ReGPU** (15 commits past 57143cb: vertex reach `5c493d1`, submit at each new RTV/DSV, one-copy queries, texture budget, live `SetMsaa`/`SetFullscreen`) | Not done (a session: our 21 patches, conflicts likely with queries/fences) | Stutter (sparks), CPU, low VRAM | **Build**; most have their own `REGPU_NO_*` switch afterwards | View submit +3.8% parked, spark p99 80 → 28 ms (FX) |
| L28 | **Handoff cuts on the draw thread** (FH1R split CP: record only changed register writes, ~30% of them; answer Direct3D coherency waits from the shadow; memory waits poll per batch) | **Built, default (S24, `regpu-reg-skip`)**: only changed register words sent (~29% of ~660k/frame), +8.1% CPU-bound live. Left: coherency waits answered from the shadow (not looked at) | CPU-bound | **Planned live** | FH1R's split +13% on 8 cores including these; −11% on 4 cores (L14) |
| L29 | **Pipelines built at asset load** (Unleashed: hold the game's load-done flag until the loaded models' pipelines are built) | Not built; needs FM3's race/car load-done flag | Stutter (first sight of AI cars, new tracks) | n/a | Goes with L16 |
| L30 | **Fetch state as shader variants** (our `TFetch` reads `tinfo` per fetch: gamma, exponent adjust, swizzle; `TFetchRaw`: dimension, clamp/mirror; key variants on the bound fetch constants as `regpu-split-alpha-test` does) | **Built, default on (S24, `regpu-fetch-variants`)**: plain slots per draw -> `TFetchPlain`; GPU frame -2.5%, byte-identical. Open: shader `510c814e1a4ef99e` (not plain), sampler index as a literal | GPU-bound (car materials ~4 fetches each) | **Start-time** (shader cache key) | Pinyon (texture signedness only): ~5% on a GPU-bound Odin, byte-identical. More variants to build/cache |
| L31 | **Frame pacing checks** for `mainthread30fps` (log steps per swap; flip a late swap at once when two vblanks passed; vblank on a high-resolution waitable timer) | Not built | Smoothness (p95), not average fps | **Planned live** | Pinyon: p95 13.07 → 10.48 ms; one step per swap 87 → 98.6% at 120 fps |
| L32 | **Wait for missing pipelines at submit, not per draw** (a frame's new pipelines build in parallel) | Not built; first check whether ReGPU's draw thread blocks per draw | Stutter (cold cache) | **Planned live** | Pinyon: worst cold frame 305–416 → 145–166 ms |
| L33 | **Long-frame sampler** (`FM3_SAMPLE_THREAD` triggered when a frame passes N ms, title + named threads every 1 ms) + **small hooks it finds** (Pinyon: unchanged `SetPWLGamma` every frame = 30% of FH1's render thread; slow SQLite view after a race 158–186 ms) | Not built (small) | Stutter, CPU-bound | n/a (tool) | Do first for any stutter or CPU work |
| L34 | **Untile by runs + SIMD byte swaps** (ITC `71fb4f0`: copy 16 contiguous bytes per computed tiled address, SSSE3 swap; ours computes `TiledOffset2DInner` per block in `texture.cpp`) | Not built (small–medium, ReGPU) | Stutter (streaming, race start), CPU | **Start-time** or live switch | ITC race start 33.2 → 9.8 ms for 44 textures; byte-identical |
| L35 | **Texture change checks: only the bytes a texture uses** (ITC: 741 of 748 hash "changes" were row padding past an untiled texture's last row → needless reloads) and/or **mark from recorded writes** (as our vertex arena does) | Not checked; count reloads per frame first | CPU, stutter | **Planned live** | Lossless; a verify mode keeps hashing for checks |

## 2. Lossless in intent (needs a numeric picture check and a drive)
| # | Item | Status | Helps on | Live A/B | Risk / note |
|---|---|---|---|---|---|
| I1 | **Hidden-part cull** `REGPU_DRAW_CULL=1` (per-draw occlusion queries + predication; retested every other frame) | Built (S19, part of every preset); never tested alone in a race | GPU-bound (hidden car parts, the AI-car cost) | **Live**: `Fm3 "env REGPU_DRAW_CULL 1"`, off `REGPU_NO_DRAW_CULL 1` | A draw hidden last frame and visible now shows a frame late: at 64 px menu letters flickered. Check with a shot burst (`Fm3 "shots 6 dir raw"`) + pngdiff; the queries cost GPU time too |
| I2 | **`REGPU_LEAN_FETCH=2`** (plain 2D texture = one sample, filter honoured) and **`REGPU_LEAN_VFETCH`** | Built (Turbo preset pieces), never measured alone | GPU-bound (car material shaders) | **Start-time** | Meant identical; a wrongly flagged texture would sample differently. Car shaders are the target: pngdiff on cars, liveries |
| I3 | **`REGPU_PLAIN_MUL_PS`** (pixel shaders multiply as IEEE, not Xenos 0×inf = 0) | Built (Turbo), never measured alone | GPU-bound | **Start-time** | Where a shader relies on 0×inf = 0 the pixel turns NaN/black: check sun, sky, glare, reflections; S22 Pinyon: make it per shader hash with one NaN→0 scrub per output (byte-identical there); HDR paint/glass shaders are the 0×Inf-sensitive ones = our car paint: pixel-diff those first |
| I4 | **`REGPU_INDEX_HASH_ONCE`**, **`REGPU_COPIES_1010102`** | Built (Turbo) | CPU-bound (hash), GPU/bandwidth (copies) | **Live** | Hash once: an index list changed mid-frame would be missed. 1010102: copies at 10-bit instead of half float |
| I5 | **`REGPU_UPLOAD_STABLE_REUSE`** | Switch in our pin; S18 +2% on the grid, not adopted | CPU-bound | **Live** | A buffer one frame stale. Adopt only after a drive with damage/tyre marks/replays |
| I6 | **`REGPU_OPTIMIZE_LEVEL`** (engine-optimisation afb4495) | Untested upstream too | GPU-bound | **Start-time** | Like L3, higher optimisation; float differences possible |
| I7 | **16-bit pixel shaders** (`min16float` with a dataflow pass keeping coordinates/depth/predicates 32-bit) | Not built (large) | iGPU/APU, AMD/Intel low end; 0 on NVIDIA desktop | **Start-time** (separate shader cache) | Precision on liveries, shadows, glare: per-shader opt-out by hash. Only if L6 shows shaders are math-bound |
| I8 | **`REGPU_ANISO_FROM_FETCH`** (the fetch constant's own anisotropy instead of 16x on every linear-wrap texture; FH1R's default) | Switch in our pin, never tried on FM3 | GPU-bound (texture bandwidth at 1440p 4x) | **Start-time** (samplers built once) | Closer to the console (softer distant ground in FH1, which is what stock looks like); pngdiff distant road/kerbs. The launcher's anisotropy setting would then only cap it |

## 3. Visible (quality traded for speed)
| # | Item | Status | Helps on | Live A/B | Cost to the picture |
|---|---|---|---|---|---|
| V1 | **Speed presets in a race** (Turbo / Mild / Potato / Low / Extreme vs Off) | Built (S19), default off; tested only in menus at Extreme | GPU-bound low end, iGPU | **Start-time** (`FM3_PERF_PRESET`; each preset compiles its shaders once, ~40 s). Some pieces live (I1, I4, `REGPU_NO_FREEZE_HELPERS`) | Turbo = I1–I4 + `REGPU_A2M_AS_TEST` (alpha to mask as a hard test at 1x: foliage edges). Mild+: narrow passes at 50/25% scale (reflections), frozen helper passes (every 3rd frame), lower anisotropy, FSR 1. Check reflections, liveries, car pictures, sun flare, FSR 1 with 4x MSAA |
| V2 | **Variable-rate shading** ("half-rate shading": per draw 2x1/2x2 on the scene, tier 1; later distant cars/scenery by a tier-2 depth image; design in RESEARCH.md "Half-rate shading") | Not built; no VRS in ReGPU. No VRS on Vega APUs, Polaris, RDNA1, Pascal, pre-Gen11 Intel | GPU-bound (the AI-car pixel cost) | **Planned live** | Coarser shading on far cars; picking "far" draws needs care. Biggest possible AI-car saving, most work |
| V3 | **Smaller render targets** (per guest format: 8888 → RGBA8, 2_10_10_10 → RGB10A2, 7e3 → R11G11B10 when alpha unused, D24S8) | Not built (large, risky: EDRAM aliasing needs conversions) | GPU-bound bandwidth, iGPU | **Start-time** | Today RGBA16F + D32S8 (8 B/px each, ~236 MB a pair at 1440p 4x). Closer to the console but differs from now; only after L6 shows bandwidth is the limit; S22 Pinyon: HDR targets as B10G11R11 gained 0 ("shader-bound, not bandwidth") |
| V4 | **iGPU first-run defaults** (UMA detected → 77% scale + FSR 1, sharpen 25; cap 60) | Not built (launcher) | iGPU | n/a (launcher defaults) | Lower resolution. Cap also saves the shared power budget |
| V5 | **SMAA 1x** (FM2 9b6b512) as a cheap AA for low presets | Not built | iGPU, low-end GPUs | **Planned live** | Softer than 4x MSAA, better than none |
| V6 | **Crowd half data mod** (`lodsettings.py mod … crowd=x0.5 sprite=x0.5`; kept as `out/user/mods/_crowdhalf`) | Built as a mod, off | Draw-bound low-end CPUs (0 fps here: −3% draws) | Race restart (data read at track load) | 3D spectators halved; near ones keep detail |
| V7 | **Render scale + FSR 1/3 and MSAA 1x** (already in the launcher) | Built; "not yet tested on FM3" in the launcher for FSR | iGPU, any GPU-bound | **Start-time** | The biggest lever there is; settings only; S22: FSR 4 (INT8 on RX 6000/7000) via FSR SDK 2.4 + upstream `Fsr4Evaluate`; DLSS/DLAA need only the SDK + build switch; XeSS SR is one new file (RESEARCH "Upscalers") |
| V8 | **Car reflection cubes updated less often** (Pinyon: FH1 `EnvMapFrequencyScale` 0.25, Odin −2.2 ms, "looks the same"; 0 left them dark) + reload only changed faces (lossless, 0.20 → 0.04 ms) | Not built; find FM3's update rate (`xref.py options`, TrackSettings) | GPU-bound, iGPU | Live if it is a live option | Reflections lag on fast-moving cars |

## Already tried and dropped (don't repeat without a new reason)
- Reusing the previous draw's pixel-constant block when identical (S24, L8 attempt): -2.4% CPU-bound, reverted.
- `REGPU_CONSTANTS_IN_RING` / `Config::lowPerformance`: −21% (S18).
- Tile merge as it is: 0 gain, FM3's race scene is refused (S18): see L10 for the real fix.
- `perfmode` and the `fast*` flags, `trackfardistance` (dead), LOD/crowd scaling: draws drop, fps doesn't on this PC (S19).
- `fastcarrender`: +1.3%, ugly. Mesh decimation: inside noise in FM2's study, bends geometry.
- Draw cull at 64 px: small things flicker (menus, S19).
- `REGPU_UPLOAD_SAMPLED`, `REGPU_DYNAMIC_EXPIRE`: no effect (S13).
- Thread pinning/affinity: FH1R measured −1.3%.

## Suggested order
1. **Free live measurements on the race grid, one game session:** L1 vertex arena, I1 draw cull (1 px, shot burst),
   I4, I5, L2 on another track. Tool: `tools/fm3_ab.ps1` + `pngdiff.py`.
2. **Start-time A/Bs (restarts, user back on the grid):** L3 `REGPU_OPTIMIZE_SHADERS`, I2 lean fetch, I3 plain
   multiply, I8 anisotropy from the fetch, L9 submit draws, then V1 presets one by one. Re-measure the AI-car cost (race vs time trial,
   `REGPU_GPU_SHADERS=1`, `gpudiff.py`) with whatever is adopted.
3. **Measurement before code:** L26 GPU time by kind (copies/resolves vs draws, barrier batches), a per-shader price list of the AI-car materials (`ps <hash> float4(0,0,0,1)` on the grid), L4 overdraw count and L6 GPU profile of the car shaders. They decide between L5
   (sampler table), a depth pre-pass, V2 (VRS), V3 and I7.
4. **Code for the user's PC:** L5 sampler table first (proven +5% in FM2), then what step 3 points at.
5. **Code for other machines:** L15 laptop GPU pick and V4 iGPU defaults (cheap, big for those users), L11 ring-wait
   sleep, L14 core gating, L16 static shaders, then the memory items (L17–L22) and the CPU items (L7, L8, L10, L12).
