# ReGPU performance plan for FH1R, from the FM3 port's work

Written 2026-11-16 (FM3 ReGPU session 40) for the FH1-ReImagined team's ReGPU branch. It turns what the FM3 port
measured and built over sessions 13-39 into an ordered plan: what each change does, how we built it, what bit us, what
it gained on FM3, and what to check first in FH1. Everything here is game-neutral ReGPU work unless marked
**[game hook]**; those need FH1's own addresses.

Sources in this repository: [PERF-PLAN.md](../PERF-PLAN.md) (every lead, status, numbers),
[PERF-RESULTS.md](../PERF-RESULTS.md) (measurements), [patches/README.md](../patches/README.md) (one row per ReGPU
patch: mechanism and switch), [patches/](../patches/) (the patches), [VRAM-UPLOAD-RING.md](VRAM-UPLOAD-RING.md),
[NOTES.md](../NOTES.md) (per-session detail), [RESEARCH.md](../RESEARCH.md) (studies, incl. our reading of FH1R).
Licence: our repository is MIT; FH1R is GPL-3, so taking our code is fine, and we only ever took FH1R's ideas.

## 0. Read first: what carries over and what doesn't

- **Our numbers are FM3's**, on one PC: Ryzen 7 9800X3D + RX 7900 XTX (resizable BAR on), 2560x1440 4x MSAA, cap 360,
  plus the 9800X3D's own Radeon iGPU (RDNA2, 2 CUs) for the low end. FH1 is a bigger open world on FM4's engine (FM3's
  successor engine), draws 3 EDRAM tiles to FM3's 2, and you measure on a 7800X3D + RX 9060 XT and a GTX 1650 Ti
  laptop. Expect the same *direction* for most items and different sizes. NVIDIA has had no test here at all.
- **Our ReGPU base** is upstream `57143cb` + our 70 patches, applied in order by `tools/setup.ps1`. Your `fh1` branch
  (read up to `35bb5d8`, 2026-11-11) came through PGR4's and Glue2GPU's ReGPU instead, so **our patches won't apply
  as files**: port them by idea, with the patch as the reference. They also stack (later patches assume earlier ones),
  so the order below follows the dependencies.
- Things your branch already has, by its commit titles (check the code): lean fetch through a sampler table
  (`4df3059`, `c83d027`), command-list splits handed to the replay thread (`0bc61d1`, +10% on PGR4's grid), draw
  workers with early fences (`2106df8`), PGR4's leaner recording and per-draw caches (`7e071fd`), the first-boot seed
  with vertex-shader variants (`3d2ab01`, `656c6dc`), a boot screen (`ff7b878`), the laptop high-performance GPU
  (`4ead774`), live frame limit/vsync/filtering (`94a60b3`). FH1R's plugin also already sets
  `REGPU_ANISO_FROM_FETCH`, `REGPU_NO_TILE_MERGE`, and builds ReGPU with PGO (+6%).
- None of our speed patches is in `fh1`'s history. The biggest wins for you are probably sections 2-4.

## 1. How we measure (the part that matters most)

Most of our wrong turns came from measuring the wrong scene or with a tool that changed the result. The method:

1. **A frozen scene that keeps rendering.** FM3 boots straight to a career race's grid and freezes the world at 9.0 s
   of the countdown (frame-time hook: dt 0); the game keeps drawing the same frame, so screenshots are byte-identical
   within a run. Null A/B on it: **±0.3% within one boot, ~±1.5% across boots**. FH1R's free-roam start or a
   benchmark spot with time stopped would do the same job.
2. **Live switches + interleaved rounds.** Every change is behind a `REGPU_*` switch that flips while running (ReGPU's
   debug server `switch` command); we run 5 rounds off/on/off/on... of 5-7 s on the frozen scene. Start-time switches
   need ≥2 boots per side, interleaved.
3. **A picture check on every change**: same-boot screenshots, pixel diff (`tools/analysis/pngdiff.py`); "0 pixels"
   below means byte-identical. Across boots only FM3's crowd differs (masked by `pngdiff_mask.py`).
4. **Pick the scene that is bound by what you change.** At 1440p 4x our grid sat at the 360 cap with ReGPU's draw
   thread the limit; so **GPU leads were measured at 4K 4x** (GPU-bound, GPU 4.1 of 4.4 ms) and **CPU/draw-thread
   leads at 720p 1x** or at 1440p under a deliberate draw-thread load. The iGPU at 720p was the "weak GPU" case.
5. **Then real races.** The frozen grid has fewer draws and no streaming. Our worst mistake: the VRAM upload ring was
   +12-19% on the grid and **-13% in real races** (section 3). We built an **AI-driven benchmark** [game hook]: the
   player's car built as an AI opponent's so the game's race AI drives it; ~3 minutes per race, results per variant,
   variants interleaved, every launcher option pinned (`tools/bench_matrix.ps1`). Compare races **at equal draw
   counts** too (`tools/analysis/bench_compare.py`), since traffic differs run to run.
6. **Don't let the profiler eat the gain.** Our full per-draw performance log cost **14-25% fps** on the draw thread
   and made a real gain (VRAM ring) look like a loss. We replaced it for drives with a **light log**: one record per
   frame into a lock-free single-producer/single-consumer ring, logged by its own thread (patch `regpu-perf-lite`):
   fps, p99, draws, GPU ms from two timestamps, near-free. Keep the heavy log only as a deliberate draw-thread load.
7. **Profiling tools we leaned on:**
   - A sampling profiler on ReGPU's draw thread by name, with whole call stacks (suspend, copy the stack, resume,
     unwind the copy), per-line and per-RVA modes (`FM3_SAMPLE_THREAD=name:gpu draw worker`, our `src/` runtime).
   - CPU per thread from the OS (`tools/perf/threads.ps1`): no cost.
   - `REGPU_GPU_PASSES` (GPU time per render pass) and `REGPU_GPU_SHADERS` (per shader), compared with
     `tools/analysis/gpupasses.py` / `gpudiff.py`; `REGPU_GPU_PASSES=2` adds pipeline statistics (pixel invocations:
     overdraw).
   - **Radeon GPU Analyzer offline** on the translated HLSL (`tools/rga_shaders.ps1`: VGPRs, code size, samples,
     branches), with the iGPU driver's own compiler DLL where RGA has no target. It found two of our biggest GPU wins.
   - One **Radeon GPU Profiler** capture (barriers, decompressions, idle): it closed four leads in an afternoon.
   - A **long-frame log** + **long-frame stacks** for stutter (section 6).
   - A byte counter per kind of upload-ring allocation (`REGPU_TRACE_RING_KINDS`): showed what to cut first.

## 2. Where FM3's frame went (to compare with FH1's)

- **1440p 4x, 8 cars on the grid:** ~2,670 draws; ReGPU's draw thread ~2.7 ms a frame (~1.02 µs a draw) = the limit
  under the cap. Each car ~183 draws (~120 main scene in ~20 material layers, ~54 shadow map) ≈ 0.19 ms draw thread +
  0.15 ms GPU. The game's own render thread mostly spin-waits on ReGPU.
- **4K 4x:** GPU-bound, ~4.1 ms: main scene draws ~1.5 ms, three 1280x720 colour resolves 0.63 ms (format conversions,
  must stay shader resolves), the game's clear rectangles 0.26 ms, post passes small. RGP: barriers 0.72% of the
  frame, no decompress or fast-clear-eliminate at all, ~8% GPU idle.
- **The race vs time-trial gap** (~1.3 ms at the time) was the AI cars' material pixel shading, not vertices or
  duplicate draws: it was fixed by making those shaders cheaper (section 4), not by cutting draws.
- **Progress** (all at FM3 defaults, same PC): race grid 1440p 4x ~213 fps (session 20) → at the 360 cap (~358 without
  the cap's limit in A/Bs); 4K 4x grid 186.8 → ~238; CPU-bound 720p 1x 214.5 → ~350; iGPU 720p races 56.6 → 78+ fps
  (options) and +27% more from fetch specialisation. Cold-boot first race: no compile hitch after a ~35 s first boot.

Your 2026-11 notes said ReGPU on the 1650 Ti laptop was CPU-side limited (GPU 63-82% busy) and the menu's main loop
took 18.8 ms vs 7.3 on Vulkan. That is the profile sections 3-4 target.

## 3. The plan, in order

Each item: **what / how we built it / nuances / FM3 result / for FH1**. Patch names are files in `patches/`
(`regpu-<name>.patch`); "live" = flips at run time.

### Phase A. Measurement first (cheap, unblocks everything)
| Item | What | FM3 patch / switch |
|---|---|---|
| A1 | Light per-frame log (above) | `perf-lite`, `REGPU_PERF_LITE=1` |
| A2 | Long-frame log (frames > 2x the recent average and > 1 ms over it, with what ReGPU did in them: idle, GPU wait, fence waits, replay waits, builds, textures created/changed + KB + ms, swap, thread CPU time) + a host callback for stacks | `long-frames`, `REGPU_LONG_FRAMES=1` |
| A3 | Per-pass / per-shader GPU time, pipeline statistics | upstream `REGPU_GPU_PASSES`, `gpu-stats` |
| A4 | Seed coverage line (pipelines drawn / not in the seed / built while drawing) | `seed-coverage` |
| A5 | Fps panel + frame-time graph on the back buffer (never in guest images) | `fps-panel` |

Nuance: with the light log, measure GPU leads from its GPU ms, not only fps, once the CPU can be the limit.

### Phase B. Draw-thread and command-processor CPU (biggest for CPU-bound and weak machines)
ReGPU's draw thread (`REGPU_DRAW_THREAD`: the command processor parses and queues, the draw thread translates and
records) is the same split as FH1R's two-thread CP. FM3 runs it serial (no draw workers). In order of gain:

| # | What and how | Nuances | FM3 result |
|---|---|---|---|
| B1 | **Asynchronous frame end** (`async-present`, `REGPU_ASYNC_PRESENT`, live): the back-buffer copy, close, submit, `Present` and frame fence run on the replay thread in stream order (new recorded-list ops `Call`/`Switch`); the next frame records into its own slot's pool list at once | The draw thread had been waiting ~10% of its time: ~7% asleep inside AMD's `Present`, ~2.5% for the replay. Every other fence signal/Present still drains the stream first. Up to one more frame may queue (latency). FH1R did this in its own backend (`6e891d4`); your `0bc61d1` moved list splits but check the swap | 720p 1x **+17.9%**, 1440p 4x **+4.3%**, 0 pixels; drivers: "cockpit noticeably better" |
| B2 | **Only changed register words sent** (`reg-skip`, `REGPU_NO_REG_SKIP=1` off, live): the CP keeps a shadow of the register file it sent (seeded from the draw thread's) and sends a run only if it differs, cut to its changed words | FM3 writes ~660,000 register words a frame (constants re-sent whole); ~29% change (FH1R measured ~30%) | **+8.1%** CPU-bound, 0 pixels |
| B3 | **Repeated shader loads dropped** (`shader-repeat-skip`, live): the CP compares each load with the last one of its type it sent | 55% of FM3's ~2,200 loads a frame were exact repeats | **+2.4%**, 0 pixels |
| B4 | **Locks off the per-draw path** (`draw-thread-cuts`): the live-shader-edit alias check takes no mutex until an edit exists; shader lookups memoised per thread | Found only with the sampler; each was 1-3% | ~**+2.8%** |
| B5 | **Per-thread memos** (`draw-thread-memos`): vertex-cache answer per buffer per frame (skips the texture mutex; a `generation` counter for arena restarts), pass sizes/scales computed once into a snapshot | Pass-scale helpers are asked ~25 times a draw | vertex memo **+3.6%**, snapshot **+2.2%** |
| B6 | **Pass-scale memo on an epoch** (`pass-epoch`, then `pass-per-draw`): every writer of the inputs other than the two surface registers bumps `g_passEpoch`; within one draw, later asks compare only the epoch (the draw thread's register copy can't change mid-draw); `ToHost` asks once, not four times | `PassScales` was 11% of the draw thread, then 7.5% | 11% → 5.5% of the thread; then **+3.8%** (under load) / **+4.6%** (342.6 → 358.3) |
| B7 | **Wake-ups only when someone sleeps** (`cpu-cuts`): CP ↔ draw-thread ring publishers notify only if the other side counted itself in before sleeping (seq_cst store/load pair); statistics clock read every 4096 calls; **index buffers bound as a view of the whole index arena/ring with a start index** | `notify_one` per record was ~4% of the CP | CP 75.9% → 64.1% of a core (4c/8t, same fps); `IASetIndexBuffer` 1,985 → 9 a frame |
| B8 | **Flat open-addressing tables** for per-draw lookups (`motion-table`): FSR's motion-vector bookkeeping was a `std::unordered_map` with a per-frame clear of ~3,300 nodes | Stamp entries with the frame instead of clearing | ~4% of the draw thread with FSR on |
| B9 | **Fetch-variant bytecode read off the draw thread** (`variant-load`): the compile worker reads cached bytecode; the draw keeps the base shader until then | Was a 20 ms frame at first sight of each variant every boot | that frame gone |
| B10 | **PGO build of ReGPU** (CMake `FM3_PGO=gen|use`, profile from the grid + cockpit + a short drive) | You already have this (+6%). Keep the profile fresh after big patches: ours went stale | **+3.2%** draw-thread bound |

Also on this list from FH1R's own work and still open for us: answer Direct3D's coherency waits from the shadow
without waiting for the recorder (we never looked).

### Phase C. Bytes per draw, then where the upload ring lives (largest single GPU-bound win)
Write-up for Vulkan too: [VRAM-UPLOAD-RING.md](VRAM-UPLOAD-RING.md). Order matters: **cut bytes first, then move the
ring**, or the move loses in CPU-bound scenes.

| # | What and how | FM3 result |
|---|---|---|
| C1 | **Fetch table entry by entry** (`sparse-fetch-table`, A/B `REGPU_FETCH_TABLE_BY_RANGE=1`): vertex fetch (`vf`) and texture entries reach the ring only for the slots the shaders read, not every entry up to the highest used one (games put vertex fetch constants near 95) | 2,996 → ~1,500 bytes a draw; races **~+5%** at equal draws (ring in system memory) |
| C2 | **Unread block parts skipped** (`unread-block-parts`): each shader's final HLSL is scanned (`readsBools/Loops/FetchLayout/User`); bool (32 B), loop (128 B), fetch-layout and user blocks written only for shaders that read them; everything for hooked draws and live edits | → ~1,060 bytes; races +0..+4% at heavy draw counts |
| C3 | **Packed float constants** (`packed-constants`): after translation, literal `c[N]` reads renumbered to N's rank in the shader's constant mask, so a draw writes its constants as **one sequential run** instead of scattered 16-byte stores (same bytes). Not for relative addressing or game-rewritten shaders | grid **+3.4%** |
| C4 | **Upload ring in VRAM** (`gpu-upload-ring`, live `REGPU_GPU_UPLOAD_RING`): a second ring per frame slot in `D3D12_HEAP_TYPE_GPU_UPLOAD` (OPTIONS16 `GPUUploadHeapSupported`, i.e. resizable BAR), persistently mapped, write-only; both rings exist so a live switch swaps at the slot's next frame start | Before C1-C3: grid +12-19%, **races -13%**. After: grid 1440p 4x **+26%** (281.6 → 355.7), 4K **+22%** (186.8 → 228.1), 12 s race starts +18% (capped), 720p 1x -0.8% |
| C5 | **Placement by measurement** (`ring-auto`, `REGPU_UPLOAD_RING_AUTO=1`, off): every 30 s, four interleaved pairs of 120-frame windows; switch if the other place wins 3 of 4 by >1.5% | 1440p **1x**: system memory +5.5%; 1440p 4x: VRAM +1.6%; 4K: VRAM by far. 36-frame windows flip-flopped. We ship a manual toggle + tooltip instead |

Nuances:
- The VRAM ring's cost is **store throughput into write-combined BAR memory**, seen as a stall at the first store after
  a draw's burst of writes (~11% of the draw thread), not partial lines (writing whole 64-byte lines in order: -0.4%).
- Reusing an identical previous constant block (`REGPU_CONSTANT_REUSE`) lost every time (-1.4..+0.6%, later -6.3%):
  compare/staging costs more than the bytes. FH1R found skipping unchanged constant writes worse too.
- Shaders reading constants straight from a system-memory ring (`REGPU_CONSTANTS_IN_RING`) was **-21%**: the GPU side
  is sensitive to where per-draw data lives. NVIDIA without ReBAR: untested; Vulkan's ~256 MB BAR window is enough.
- Never read through the VRAM mapping. Readbacks stay in READBACK heaps.

### Phase D. GPU shader cost (the AI-car / material cost; biggest on small GPUs)
| # | What and how | Nuances | FM3 result |
|---|---|---|---|
| D1 | **Alpha-test-free pixel shader variants** (`split-alpha-test`, live): ReGPU applies the alpha test at run time (`switch(pinfo.x)` + `discard`), so every shader may discard and early-Z can drop. Draws with alpha test off and alpha-to-mask off use a variant built from the HLSL without the block (pipeline flag `0x400`, stored in the pipeline manifest and seed) | Check the seed builds the variant, not the base (our `seed-variants` fix) | **+5%** grid, picture identical |
| D2 | **Plain-fetch variants** (`fetch-variants`, live): per draw, which texture slots are "plain" (2D, whole image, no 16-bit fixed conversion, exponent, border, single-axis clamp, gamma, mirror, swizzle); a variant calls a one-sample `TFetchPlain` there | The car paint's fetch paths were 117 KB of GPU code (RGA) | GPU **-2.5%**, car paint 117 → 20 KB, 0 pixels |
| D3 | **Fetch specialisation** (`fetch-specialize`, live): the variant's lean 2D fetches call `TFetch2DK` with **this draw's fetch-constant words as literals** (made from `TFetch2D`'s own text), so the compiler folds every branch the constant rules out. Then cube/3D/full-path fetches the same way (`igpu-leads`: `TFetchK`/`TFetchRawK`) | Variant id = hash of the slots' words; more variants to compile and cache (the seed covers them). Replaces D2's role | races 1440p **+3.6%**, 4K **+9%**, **iGPU +27%**, 0 pixels; cube/3D another -1.0% iGPU / -1.3% 4K |
| D4 | **Vertex shaders without the corner array** (`vs-corners`, default; `REGPU_OLD_VS_CORNERS=1` old): the point-sprite/rect-list path of every translated VS kept `float4 vq[3][16]` indexed at run time: scratch under FXC (816 B a vertex), ~240-255 VGPRs under DXC on AMD, cutting VS occupancy on **every** draw. Now positions first, pick corners, then compute outputs for the picked corner only | Found offline with RGA + the iGPU driver's own compiler. Applies to every ReGPU port | iGPU races FXC **+10.2%**, DXC **+14.5%**; 4K FXC +0.2%, DXC +2.5% |
| D5 | **Display gamma drawn into the back buffer** (`4k-leads`): the gamma ramp as a pixel shader through the presenter's copy, instead of a compute pass into an image and a copy | Swap chains can't be UAVs; debug shots grade a copy the same way | 4K **+4%** (229 → 238), within 1/255 |
| D6 | **DXC instead of FXC** (`dxc`, `REGPU_DXC=1`; `pso-lock`; `dxc-tuning`) | AMD drivers crashed on 5/16 cached boots when pipeline-library loads/stores ran concurrently with DXIL: serialise them (`REGPU_PSO_LOCK=1`, 0/16). `-HV 2018` (2021 rejects the translator's vector `?:`). `-O1` = same driver code, compiles faster. SM 6.6: no gain. Separate caches | After D4: DXC ahead on both GPUs; 4K +1.3% at equal draws. We ship it opt-in |
| D7 | **Anisotropy as the fetch asks** (`REGPU_ANISO_FROM_FETCH`) | You already default it | 4K GPU -3.8%; 9.3% of pixels slightly softer: opt-in for us |

Checked and not worth it on FM3 (save the time): lean fetch 2 (4K **+1.6% slower**; your `fh1` has a sampler-table
lean fetch: measure it against D3 rather than with it), lean vertex fetch (+0.2%), plain IEEE multiply in pixel shaders
(mixed; turns 0×inf=0 shaders black: crowd flags), 16-bit `min16float` everywhere (iGPU **slower**, blocky UVs),
FXC optimisation level 1 in the background (**-6%**).

### Phase E. VRAM and streaming
| # | What and how | FM3 result |
|---|---|---|
| E1 | **Idle EDRAM surfaces released** (`surface-release`): every EDRAM base ever drawn to kept a full-size host surface (4K 4x: 377 MB each, 33 after menus). Released after 3600 frames unselected (not bound ones); view slots per surface, reused. Plus FM3 sizes surfaces at 720 rows (`REGPU_SURFACE_ROWS`) | 4K 4x **14.3 → 5.7 GB**, races **+5.1%**; 5K 4x 82 → 154 fps. FH1's 3 tiles: work out its own row count |
| E2 | **Texture streaming off the critical path** (`texture-streaming`): untile/decode of textures ≥256 KB in row bands on 3 helper threads (identical output), next 128 MB texture heap made ahead on another thread at 3/4 use, read-from-memory cubes placed in the pool instead of committed resources (~1.2 ms each) | 18.9 MB DXT5 load 8.9 → 2.5 ms, 36.5 MB 34.7 → 19.8 ms; drive's max frame 7.3 → 5.7 ms. Open world: likely bigger for FH1 |
| E3 | **Index-list cache swept, not cleared** (`index-cache-sweep`): the expanded-index cache was `clear()`ed whole at 8192 entries, freeing every list in one draw | A 77-82 ms hitch **once a lap** gone. FH1 streams more geometry: check yours first |
| E4 | **Vertex arena** (FM2's, `vertex-arena`): large vertex buffers kept in a 512 MB VRAM arena, re-copied only when a write watch saw a guest write to their pages [needs the host's write watch] | +10.5% (grid, session 23). Check if `fh1` has it |

### Phase F. First boot and stutter
| # | What | FM3 result |
|---|---|---|
| F1 | Static shader scan of the game files + a shipped pipeline seed (you have this) **plus the variants**: seed pipelines built with D1/D3 variants, and the static-shader wait stops when ≤3 are left after 5 s (one never-drawn VS took 37-44 s) (`seed-variants`) | cold boot ~35 s, no race-start hitch |
| F2 | **Pipeline-key flag bug** (`motion-flag`, upstream fixed too): flag 4 meant both "no depth clip" and "motion-vector variant", so every no-depth-clip pipeline was built as a motion variant and left out of the manifest: rebuilt while drawing each boot | Check your branch: a seed that never covers rect clears is this |
| F3 | Scan finds confirmed unchanged after 1 s (`scan-confirm`, FM2's fix) | 22-24 half-written finds dropped per boot |
| F4 | An automated tour over every environment feeding the seed, with the coverage line (A4) as the gate | seed 3,274 pipelines, 0 missing on a re-check |

### Phase G. Power and weak machines
| # | What | FM3 result |
|---|---|---|
| G1 | **CP sleeps when idle under a frame cap** (`cp-idle-sleep`, `REGPU_CP_IDLE_SLEEP_US`, + existing `REGPU_CP_SPIN_MS=1`): ~100 µs high-resolution waitable-timer waits once the ring is empty | 60 cap: CP 98% → ~20% of a core (2.1 → 1.02 cores total), fps held. Uncapped -1%: gate on caps ≤144 / vsync |
| G2 | **[game hook] The render thread's ring-space wait sleeps ~100 µs** instead of spinning (FM3: `sub_824B5048` → `sub_824AAA68`) | -17..-19% CPU, same fps (FH1R did this first: +10% on a 4-core laptop) |
| G3 | **Bilinear upscale** option for the slowest GPUs (`igpu-leads`, `REGPU_UPSCALE_BILINEAR`) | FSR 1 was 1.65 ms on the iGPU; Low preset races +6.9% |
| G4 | Thread gating on ≤4 cores (draw thread) | not needed for FM3 (4 cores -0.3..-2.1%); your split CP needed it (-11%) |

### Phase H. [game hook] Optional half-rate passes and LOD (visible, opt-in)
How we found and skipped them is reusable; the addresses are FM3's. **Lesson: skip a whole render pass (begin, draws,
end/resolve), not just its draws**: skipping only the draw method left empty maps (car shadows blinked); skipping the
whole pass makes ReGPU keep last frame's images.
| Item | FM3 hook | Gain (draw-thread bound unless said) |
|---|---|---|
| Car shadow passes every 2nd frame | driver `sub_827B5088` (begin/method/end) | **+8.2%** (+9.3% every 3rd); 4K +2.2% |
| Reflection cube faces every 2nd frame (all six still update) | face render `sub_827FF238` | +2.1%; 4K GPU -2.4% |
| Rear-view mirror scene every 2nd frame | `sub_827FDB88` | cockpit 214 → ~240 fps |
| Distant AI cars one LOD level coarser | car detail field +596 | **+4.1%**, 0.15% of pixels |
| One guest tile (tiling scenario forced to 1 tile + `window-fixup-union` merging the prerecorded per-tile window fixups) | scenarios at `0x82BF60D0` | **+9.1%** CPU-bound (18% of draws were second-tile copies). FH1 has 3 tiles: more to win, but FH1R found ReGPU's tile *merge* saved nothing; one tile at the game level is a different route (MCLA: -9..-20% frame time) |

### Phase I. Anti-aliasing and upscaling (correctness first)
- **FSR 3 driver timeout** (`fsr4`): `Fsr3Evaluate` destroyed and re-created its context whenever the setup changed
  (the scene image switching between 8-bit and HDR, six times in a 4K load) while the GPU still had work recorded with
  the old one: freed textures under the GPU, AMD TDR. Fix: keep one context per setup for the device's life (≤4).
  Same patch adds FSR 4 (SDK 2.1 version descriptor; the old code ran FSR 4.1.2 under the FSR 3 name). FSR dispatch
  queued without a mid-frame drain + motion tables (B8): FSR 3 at 1440p 276 → ~305 fps = native.
- Post AA costs at 1440p on the 7900 XTX (`postaa`): FXAA +0.05 ms, CMAA 2 +0.17, SMAA +0.25, TAA +0.7-0.85, SSAA
  1.5x +0.6, 2x +1.6. AMD's D3D12 driver offers no EQAA levels.

## 4. Tried and dropped (don't repeat without a new reason)
| Idea | Result on FM3 |
|---|---|
| Draw workers / parallel pieces with the draw thread; spliced parallel pieces | -7.6%; -10..-12% on 4-8 threads (weak CPUs are saturated; serial wins) |
| Ring writes moved to the replay thread | -0.4% |
| `REGPU_SUBMIT_DRAWS=512` | -5.7% |
| Constant block reuse (previous draw's identical block) | -2.4%, later -6.3% |
| Constants read from a system-memory ring | -21% |
| Whole-surface fast clears instead of rectangles | GPU +0.4..+0.9%: game clear colours aren't the optimized value; closed |
| End-of-frame barrier batching | GPU -0.3% (the waits are data dependencies) |
| 32-bit render-target formats | FM3's scene is 7e3 float with alpha on every draw: no lossless format |
| VRS on AI cars | their material shading is 0.05-0.1 ms at 4K: <1% |
| Hardware MSAA resolve | no big resolve qualifies (format conversions) |
| Per-pass MSAA for 1x post passes | ~3% at 4K for a large, risky change: parked |
| Per-draw occlusion cull (`REGPU_DRAW_CULL`) | -1.3%, flicker at 64 px |
| `min16float`, SM 6.6, FXC level 1 | slower / level / -6% |
| PGO or AVX2 builds of the recompiled game code; thread pinning | no change; FH1R measured pinning -1.3% |
| Fence-free ring head publish; 64-byte ordered line writes | no change; -0.4% |

## 5. Suggested order for FH1R, by machine
1. **Phase A** (a day): the light log, long frames, coverage line. Re-measure your 1650 Ti laptop and 9060 XT scenes with it.
2. **CPU-bound machines (your laptop's limit):** B1 async present if your swap still waits, B2 register skip, B3, B7,
   B5/B6, B4 (sampler first); then C1-C3 (they also cut CPU). Expect the largest gains here.
3. **GPU-bound with resizable BAR:** C4 after C1-C3, with a manual toggle; NVIDIA needs its own measurement.
4. **Every GPU, most on small ones:** D4 (every port has the corner array), D1, D3 (with the seed covering the
   variants: F1/F2), D5.
5. **Stutter in an open world:** E2, E3, B9; E1 for VRAM at high resolutions.
6. **Laptops/handhelds/power:** G1, G2; Phase H as opt-in options once FH1's passes are found.
7. Check I (FSR context churn) if you ship FSR 3: it is a crash, not a speed item.

Each step: port behind a switch, frozen-scene interleaved A/B + pixel diff, then benchmark races at equal draw counts,
then a human drive. Questions: ask in the FM3 ReGPU repository (`patches/README.md` names every switch).
