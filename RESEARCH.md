# Research

Investigations that are not bugs yet: options, feasibility studies and leads with no code behind them. Open bugs
live in [OPEN-ISSUES.md](OPEN-ISSUES.md), vinyl/livery work in [VINYLISSUES.md](VINYLISSUES.md).

## Input latency: AMD Anti-Lag 2 and NVIDIA Reflex (session 21, 2026-11-11)
Question from the user: can we add AMD Anti-Lag 2 and NVIDIA Reflex? **Short answer: yes, both are possible and small, and
both are free to use (MIT).** But the latency they remove is mostly our own frame queue, so a frame-queue limit of our own
should come first: it works on every GPU and we can measure it ourselves. Research only, no code yet.

### What the two features are
Both do the same thing: when the GPU is the bottleneck, the CPU runs ahead and frames wait in queues before the GPU draws
them. Every queued frame adds a frame of input lag. The driver measures how long the GPU takes and **makes the game sleep
just before it reads input**, so that the frame reaches the GPU just as the GPU gets free. Nothing changes when the CPU
is the limit or the frame cap holds (then the queues are already empty).
- **AMD Anti-Lag 2 SDK** (GPUOpen `AntiLag2-SDK`, **MIT**, one header `ffx_antilag2_dx12.h`, no DLL to ship: it talks to
  the driver's `amdxc64.dll`). API: `Initialize(&ctx, d3d12Device)`; `Update(&ctx, enable, maxFps)` **once per frame, just
  before the game reads input** (the driver sleeps inside it; `maxFps` is an optional built-in limiter);
  `MarkEndOfFrameRendering`/`SetFrameGenFrameType` only matter with frame generation (we have none). Needs RDNA1 or newer
  (RX 5000+) and Adrenalin 24.7.1+ for DX12. **The user's RX 7900 XTX supports it, so we can test it here.**
- **NVIDIA Reflex** (low-latency part of **NVAPI**, now on GitHub as `NVIDIA/nvapi`, **MIT**, static `nvapi64.lib`, no
  DLL to ship: it calls into the driver). API: `NvAPI_D3D_SetSleepMode(device, {lowLatency, boost, minIntervalUs})`
  once; `NvAPI_D3D_Sleep(device)` **once per frame, before input**; `NvAPI_D3D_SetLatencyMarker(device, {frameID,
  SIMULATION_START/END, INPUT_SAMPLE, RENDERSUBMIT_START/END, PRESENT_START/END})` per frame (better sleep timing, and
  the latency numbers in GeForce overlay / FrameView); `NvAPI_D3D_GetLatency` reads them back. GeForce 900 series or
  newer. "Boost" keeps GPU clocks up when the CPU is the limit. **Nobody here has an NVIDIA GPU: it would need a tester.**
  (The "NVIDIA Reflex" name and logo have NVIDIA branding rules; the launcher can call it "low latency (NVIDIA)" until
  checked.)
- Driver-only switches don't help here: NVIDIA's control-panel "Low Latency Mode" does not apply to D3D12, and no driver
  can see the queues inside the guest (below).

### Where our latency is (the queue, stage by stage)
Read from our code and ReGPU's (pin 57143cb):
1. **Game main loop** (`sub_821F5DE8`, guest main thread): one iteration per guest vblank, which **we** generate
   (ReGPU `VblankLoop`, `gpu.cpp`; divider 2 + `mainthread30fps` above 60, `src/hooks/frame_time.cpp`). Reads input
   through **our** `XamInputGetState` (`src/kernel/imports_xam.cpp`).
2. **Render thread t7**: builds the PM4 stream for the main loop's frame into the guest ring, ends with `VdSwap`
   (**ours**, ReGPU `gpu.cpp`; the 360-fps limiter `PaceSwap` runs there). In a GPU-bound race t7 spends ~65% waiting
   for ring space (session 20): **the ring is full**, i.e. it holds queued work.
3. **ReGPU command processor (t9) + draw thread**: translate into D3D12 lists.
4. **D3D12 queue**: `kFrames = 3` frame slots (`renderer.cpp`); a frame waits only for the fence of the frame 3 back
   (`BeginFrameIfNeeded`), so up to ~2 frames can be queued on the GPU.
5. **Swap chain**: flip-discard, 3 buffers, tearing allowed, no frame-latency waitable object and no
   `SetMaximumFrameLatency` (`presenter.cpp`): DXGI's default of up to 3 queued presents.
At 1440p 4x MSAA in a race (~200 fps, ~5 ms a frame, GPU-bound) that is plausibly **2-4 frames = ~10-20 ms** of
avoidable lag (an estimate: not measured yet). At the cap (time trial at 360 fps, menus) the queues are mostly empty.

### How it would hook in
- **The sleep / `Update` call: at the start of the main loop's frame, before it reads input.** Two candidates, both ours:
  (a) ReGPU's `VblankLoop`, before `RaiseInterrupt` on the vblanks that release a main frame (game-neutral; the sleep
  delays the vblank, so the whole frame starts later), or (b) our frame-time hook in the main loop (`frame_time.cpp`,
  the call at `0x821F6004`). First check (log) that FM3 reads the pad after that point and only once per frame;
  `XamInputGetState` is also the place for Reflex's `INPUT_SAMPLE` marker.
- **Frame IDs**: the sim, render and present stages are 1-3 frames apart, so markers need one ID carried through:
  the swap count (`VdSwap` number = CP swap number = ReGPU frame) is the natural one; the main loop's frame gets "the
  next swap". If the main loop runs faster than t7 (it can drop frames), some IDs have no present: Reflex tolerates
  gaps, but this is the part most likely to need care.
- **Markers**: `RENDERSUBMIT_START/END` around the draw thread's frame (begin / `SubmitLists`), `PRESENT_START/END`
  around `PresenterPresent()` (ReGPU `renderer.cpp`, end of frame), all on ReGPU's `ID3D12Device` (`g_device`).
- **Code split** (ReGPU's rules): the vendor calls go in ReGPU as a game-neutral patch (`regpu-low-latency`: device,
  present markers, a `regpu::LatencyFrameStart(frameId)` entry the port calls, `REGPU_LOW_LATENCY` switch), FM3 calls it
  from its frame-start hook. Launcher: Graphics > "Low latency: off / on / on + boost" (`low_latency=` in
  settings.ini). Builds: AL2 = one header in `thirdparty/`; Reflex = the NVAPI header + `nvapi64.lib` (MIT, can live in
  `thirdparty/nvapi`). Size: roughly 150-250 lines plus the launcher option.
- **Interaction with our limiter**: `PaceSwap` (cap 360) still applies; with Anti-Lag 2 its `maxFps` (or Reflex's
  `minimumIntervalUs`) can replace it so the cap's sleep is also done at the best point.

### Do this first: our own frame-queue limit (all GPUs)
Most of the gain does not need a vendor SDK: limiting how many frames may be queued gives the same effect, roughly
"Anti-Lag 1 / Reflex without the clever timing". Options, cheapest first:
1. Swap chain with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` + `SetMaximumFrameLatency(1)` (one queued present).
2. Fewer frames in flight on the GPU (ReGPU `kFrames` 3 -> 2, or wait for frame N-1's fence instead of N-2's); the
   FM4 port has a `REGPU_FRAMES_AHEAD` switch (RESEARCH "ReGPU speed work", item 8) to look at.
3. **Gate the game itself**: in `VdSwap`/`PaceSwap` (or before the vblank that releases the main loop) wait until the
   host GPU has finished swap N-1. This empties the guest ring too, which (1) and (2) can't reach.
Each costs a little fps when GPU-bound (less overlap); that trade is what Reflex/Anti-Lag 2 time better. Behind an
`FM3_*`/`REGPU_*` switch, default off until measured.

### How to measure (before and after)
- **Our own probe** (new, small): timestamps per frame ID at main-loop start, `XamInputGetState`, `VdSwap`, CP swap,
  `PresenterPresent`, and GPU completion (fence); log the average of "input read -> present" and "present -> GPU done"
  every few seconds (`FM3_LATENCY_LOG=1`). Gives the queue depth in ms, any GPU.
- **Reflex markers / AMD**: with Reflex, `NvAPI_D3D_GetLatency` reports the same stages; with AMD, the Adrenalin overlay
  shows "PC latency" for Anti-Lag 2 games. PresentMon 2.x shows display latency on both (it doesn't see the guest
  stages, so read it as a lower bound).
- Test case: the paused CrownJewel race grid at 1440p 4x (GPU-bound, the case with queues) and a time trial at the 360
  cap (should change nothing). Report fps loss next to the latency gain.

### FH1R's "Use FIFO latest ready" (looked at on request, session 21)
FH1R (ReXGlue, Vulkan presenter) patch `0070-fifo-latest-ready-present-mode` + cvar
`vulkan_allow_present_mode_fifo_latest_ready` (default on; launcher Advanced, "Use FIFO latest ready"; docs
`framerate.md`, `sdk_patches.md`, their issue #80). It is a **Vulkan presentation mode**
(`VK_PRESENT_MODE_FIFO_LATEST_READY_KHR`, extension `VK_EXT/KHR_present_mode_fifo_latest_ready`): vsync without
tearing like plain FIFO, but at each vblank the display takes the **newest finished frame** and drops older queued ones,
instead of showing every queued frame in order. Plain FIFO, with a game faster than the refresh or at a rate that isn't
a divisor of it, builds a queue that delays frames and drops them unevenly (their #80: an AMD Windows driver without
MAILBOX showed 53 of 63 fps at irregular 1-5 vblanks on 144 Hz). Used only if the driver offers it, chosen after
mailbox/immediate/relaxed FIFO, else plain FIFO. FH1R itself says it was not measured on hardware yet.
**For us:** ReGPU is D3D12/DXGI, not Vulkan, so the mode doesn't exist as such. The DXGI equivalent is a flip-model
`Present(0, 0)` (sync interval 0 **without** `DXGI_PRESENT_ALLOW_TEARING`): no tearing, and a newer present replaces
one not yet shown (PresentMon lists those as "Discarded"). Today `presenter.cpp` has only `Present(1, 0)` (vsync on =
plain FIFO, up to 3 queued) and `Present(0, ALLOW_TEARING)` (vsync off, the user's setting). So a third "VSync: newest
frame" mode is a one-line present change plus a launcher choice; with it the cap would not be clamped to the refresh
(`FrameCap`), so the game draws faster than the display and each vblank shows the freshest frame (lower latency than
FIFO, no tearing, but GPU work on frames never shown, and uneven motion unless fps is well above the refresh).
Related, and possibly worth more: with vsync on, our limiter paces on the CPU clock at the refresh while `Present(1)`
paces on the real vblank; the two drift, so FIFO's queue can fill to 3 frames (~20 ms at 144 Hz) or repeat a frame. A
waitable swap chain with `SetMaximumFrameLatency(1)` (queue-limit option 1 above) fixes that for vsync-on players.
Neither matters with vsync off on a FreeSync display (the user's setup: tearing flag + VRR, cap 360).

### Recommendation (order)
1. Latency probe (measure what we have). 2. Our own queue limit (switch, A/B with the probe; every GPU).
3. **Anti-Lag 2** (testable on the user's GPU; one header). 4. **Reflex** with markers (needs an NVIDIA tester).
5. Launcher option. Risks: frame-ID mapping across the decoupled main loop / render thread; a sleep in the wrong place
   could lower fps or stutter (keep everything behind switches, default off until the user has driven with it).
Sources: GPUOpen AntiLag2-SDK README and `ffx_antilag2_dx12.h`; `NVIDIA/nvapi` `nvapi.h` (Reflex functions, "since
release 455") and its MIT `License.txt`.

## ReGPU speed work in other projects (session 19, 2026-11-10)
Survey of every branch of the reference clones (framework/ReGPU, ReGPU-fh1, FM2, PGR4, FM4, FH1R), read only. Our pin
(57143cb, a squash) already has rexglue-driver and most of engine-optimisation (draw workers + early fences, pipeline seed,
VS precompile, lean fetch v1, SIMD hashing, sampled upload reuse, lean ALU, ...). None of the FM2 "Turbo/Hyper" code is
public (session 19 implemented our own: patch regpu-turbo-hyper). Candidates, best first for FM3:
1. **FM2 vertex arena** (PORTED in session 19: patch regpu-vertex-arena) (`release-0.9.10`: a6edeb7, 0c9a747, 620a77c, 816898e, d430385, f893212; `origin/main:src/gpu/renderer.cpp`
   `struct VertexArena` ~2466): large vertex buffers kept in a 512 MB VRAM arena, recopied only after a guest write (write-watch:
   read-only pages, first write faults). FM3 logs "zero-copy vertex fetch": the GPU reads vertices over PCIe every draw. FM2:
   1.5 ms a frame on a 4070 at 1440p. Needs WriteWatch in our `src/memory.cpp` and an `NtReadFile` tweak; FM2 hit a tyre-mark
   bug with it (d430385). Less gain on iGPUs.
2. **Occlusion results resolved once a frame** (PGR4 vendored ReGPU, `REGPU_RESOLVE_EACH_QUERY` = old): ~3% on PGR4's grid.
   Touches our zpd-tile-spans and worker-queries patches.
3. **All samplers as a 4-entry table** (FM2 ce5bf5a): every fetch path 2 sample instructions instead of ~8; 4070 1440p
   213.8 -> 225.2 fps. Root signature change.
4. **Texture eviction budget** (PGR4 `EvictTextures`, `REGPU_TEXTURE_BUDGET_MB`, FM2 `FM2_TEXTURE_CACHE_MB`): for low-VRAM presets
   and long sessions.
5. **SMAA 1x** (FM2 9b6b512; ReGPU form in FM4's vendored copy, `smaa_passes.hlsl`): cheap AA for low-end presets.
6. **Laptop GPU pick** (`REGPU_ADAPTER`, ReGPU-fh1 4ead774) or exporting `NvOptimusEnablement` /
   `AmdPowerXpressRequestHighPerformance` from fm3.exe.
7. **Ring wait sleeps instead of spinning** (FM2 480e94a, `FM2_SPIN_RING_WAIT`): render-thread CPU 100% -> 36%, same fps; for
   APUs. FM3's equivalent is t7's `sub_824B5048` -> `sub_824AAA68` wait (needs FM3 addresses).
8. `REGPU_OPTIMIZE_LEVEL` (engine-optimisation afb4495, untested upstream), `REGPU_FRAMES_AHEAD` (FM4, latency only).
Don't port: `SplitMainListAsync(false)` (rexglue-driver 0bc61d1; our pin reverted it: the AMD hang), FM4's RGBA32F surfaces,
FH1R's perf branches (ReIGlue/Xenia, not ReGPU; ideas only: a 540p scene under a 720p HUD for an iGPU preset).

## 16-bit (half-precision) shaders for lower-end GPUs (session 18, 2026-11-09)

**Question:** could an optional shader translation that does vertex shaders (and others) in 16-bit instead of
32-bit floats help lower-end hardware?

**Short answer:** possible as an opt-in for **pixel shaders only**, and only for values the translator can prove
don't need 32 bits. Vertex shaders in 16-bit would break the game. The likely gain is small next to lowering the
resolution with FSR, which the build already has. Read from ReGPU's translator at our pinned commit
(`thirdparty/ReGPU/src/gpu/hlsl.cpp`, `renderer.cpp`); nothing built or measured.

### What ReGPU does today
- Each Xenos shader is translated to HLSL and compiled with **FXC** (`d3dcompiler_47`) as **shader model 5.1**
  (`renderer.cpp` ~3367: `ps_5_1` / `vs_5_1`). Every guest register becomes a `float4 rN` (`hlsl.cpp` ~1215 for
  vertex shaders, ~1375 for pixel shaders).
- With FXC the only 16-bit option is **`min16float`**: a hint the driver may honour or ignore.
- Real 16-bit types (`float16_t`, `-enable-16bit-types`) need **DXC and shader model 6.2**. Moving ReGPU to DXC
  means DXIL output, a new shader cache and new compile times for every shader: a project of its own.

### Why vertex shaders can't go 16-bit
- **The vertex index travels in a float register:** `r0.x = float(vindex)` (`hlsl.cpp` ~1218), passed on to
  `VFetch(..., float index, ...)`. A 16-bit float holds whole numbers exactly only up to 2048; car models have tens
  of thousands of vertices, so fetches above 2048 would read the wrong vertices (exploded meshes).
- **Positions:** world/view positions on a track run to hundreds or thousands of metres; 16-bit keeps about
  3 significant digits, so geometry would jitter.
- **Constant indexing and loop counters** (`a0`, `aL`) are computed from floats in the translated code.
- Nearly everything in a vertex shader feeds the position, so even a careful version saves almost nothing there.

### Pixel shaders: possible, with exclusions
16-bit is usually fine for colour, lighting and blending math. Not for:
- texture coordinates on large textures (2048x2048 liveries need more than 11 bits of position within the texture);
- shadow-map depth comparisons (shadow acne);
- depth output (`SV_Depth`), kill and predicate comparisons, anything that feeds an index.

So the translator needs a small dataflow pass: mark every register that (transitively) feeds texture coordinates,
depth, `a0`/`aL`, predicates or kills as 32-bit, and declare the rest `min16float4`.

### What it would gain
- **NVIDIA** reports no minimum-precision support on desktop GPUs and runs `min16float` at 32-bit: a GTX
  1050/1650 or MX laptop chip gains nothing (and loses nothing).
- **AMD** (Vega, RDNA, incl. APUs such as the 780M) and **Intel** iGPUs (Gen 8 and later) can use it. Check at run
  time with `D3D12_FEATURE_D3D12_OPTIONS` → `MinPrecisionSupport`.
- **Only passes limited by shader math or register pressure gain.** Rough estimate from what other games report: a
  few percent to the low teens on such passes, nothing on passes limited by memory bandwidth or fill rate.
- **Proton / Steam Deck:** not known whether vkd3d-proton and the driver there use the hint.

### Better levers for low-end hardware (already in the build)
- Lower internal resolution with **FSR upscaling**: pixel cost drops roughly with the pixel count.
- **MSAA 1x**.
- The CPU side: on fast GPUs FM3 was bound by ReGPU's command processor (OPEN-ISSUES P); a slow 4-core CPU will
  likely hit that before shader math.

### If we build it
1. **Measure first:** on a low-end AMD or Intel GPU, profile a race (Radeon GPU Profiler or PIX) and check whether
   pixel-shader math is actually the limit.
2. **Opt-in switch in ReGPU** (game-neutral, per ReGPU's CLAUDE.md), e.g. `REGPU_HALF_PRECISION=ps`: pixel shaders
   only, with the dataflow pass above, active only when the GPU reports support.
3. **Separate shader cache:** the cache key (`ShaderCachePath`, `renderer.cpp` ~3313) must include the switch, or
   cached 32-bit and 16-bit shaders mix.
4. **Per-shader opt-out by hash**, and numeric screenshot comparisons as in session 8 (`tools/analysis/`):
   liveries, shadows, sun glare, reflections.
5. **Launcher option** ("Low-precision shaders") only if step 1 shows a gain.

## Music plays briefly, then not again for the session (session 18, 2026-11-09)
**Seen (user):** music plays for a very short while, if at all, and is not heard again for the rest of the session.
hl360 plays music normally, so the cause is in our build.

**Solved (session 18, 2026-11-10; user-confirmed in menus and races):** lead 2 was right, plus a second cause: the codec pool made the streamer's start loop give up, and the seek guard flushed the music codec between its one-packet buffers. Details: NOTES.md session log "music fixed". Lead 1 (APC) was not it. The rest of this section is the original study.

**Ruled out (code read):** the music-player stub (`XMPGetPlaybackController` in `src/kernel/imports_xam.cpp`
answers "title controls playback, not taken over": FM4's fix for menu music cutting in and out); the FM2 easter-egg
music mute (`DealerMutesMusic()` returns false in `src/hooks/common/diagnostics.cpp`); category volume
(`XAudioGetVoiceCategoryVolume` reports 1.0).

**Lead 1 (strongest): no APC support anywhere in our kernel.** hl360 (ReXGlue, from Xenia) runs completion
callbacks. Ours skips them and logs once:
- `NtReadFile with an APC routine is not supported` (`src/kernel/imports_io.cpp` ~373): if the music streamer
  reads each chunk with a completion callback, it never hears back after the first chunks.
- `NtSetTimerEx with an APC routine (...) is not supported` (`src/kernel/imports_ke.cpp` ~797): a periodic timer
  callback that never fires would stall it the same way.

**Check:** `Select-String -Path out\run\fm3.log, out\run\fm3.prev.log, out\logs\*.log -Pattern "with an APC routine"`.
A hit names the call; the fix is to queue the callback to the calling thread and run it at that thread's next
alertable wait (`KeDelayExecutionThread`, `KeWaitFor*`, `NtWaitFor*`, `NtSignalAndWaitForSingleObjectEx` with
`alertable` set), as Xenia does, with a log line per delivery.

**Lead 2: our XMA decoder drops the stream.** FM4's drop-ahead and seek-guard fixes (we carry both) restart the
decoder when the title clears a stream's input bits or a packet doesn't seem to follow; FM2 (which has neither)
found that music streams in packet pairs with a seek between them (`92420c5`). Live A/B while music is silent:
`Fm3 "switch FM3_NO_XMA_DROP_AHEAD 1"`, then `Fm3 "switch FM3_NO_XMA_SEEK_GUARD 1"`; watch the `xma: 5 s:` lines.
Also `set FM3_TRACE_READS=music` in `play.cmd` to see whether music-file reads continue or stop (assumes loose
`.xma` files under `Media\Audio\Music`, as `imports_io.cpp` expects; not checked for FM3).

## Local multiplayer / split screen (session 18, 2026-11-09)
Neither sibling port has it. **FM2** merges every controller into player 1 (`fcdb0f8`, `f332752`: a second pad
was a signed-out player 2, career locked) and states "FM2 has no split screen". **FM4** signs in one profile on the
controller that last pressed Start or A (`d90ae09`); our FM3 code is ported from it.

**Our state:** each XInput pad is its own guest user (`ReadPad`, `XInputIndex` in `src/kernel/imports_xam.cpp`),
so player 2's input reaches the game, but only one user is ever signed in (`IsSignedIn`). Never tested.

**First step:** start split screen with two pads and `FM3_TRACE_XAM=1`, to see whether FM3 accepts a signed-out
player 2 or needs a second signed-in profile (not known). Then the rendering side: two viewports per frame, a path
no ReGPU port has tested.

## Frame rate with AI cars: ~200 fps in races vs ~300 in time trial (session 18, 2026-11-09)
**Seen (user):** races with AI cars ~200 fps, time trial without AI ~300 fps (RX 7900 XTX, 1440p 4x MSAA, cap 360).
That is 5.0 vs 3.3 ms a frame: AI cars add ~1.7 ms. Sources: FH1R (`FH1-ReImagined/ForzaHorizon1-ReImagined`,
`docs/perf.md`, GPL-3: ideas only), PGR4-ReGPU, upstream ReGPU, the FM2/FM4 ports. Read only; nothing measured on FM3.

**What we already know (OPEN-ISSUES P):** FM3's frame rate near cars follows the draw count: ~1,900 draws alone,
3,200–3,600 with AI cars close (session 13); the game's render thread waits on ReGPU's command processor; at 1440p
4x MSAA the GPU was ~90% busy after the draw thread (session 14). 1.7 ms over ~1,500 extra draws is ~1.1 µs a draw.

**What the other ports found:**
- **FH1R: cars cost draws and nothing else** ("Steam Deck frame collapses", `cbb26d4`). Free roam 2,865–3,144
  draws; 8-car race pack 5,802 draws (+70–80%); each draw costs the same 1.7–1.8 µs whatever the scene; no
  per-car textures, pipelines, render targets or evictions. "There is no per-car overhead of its own to remove."
- **FH1R: the EDRAM tiles are the biggest cost.** FH1 draws its 4x MSAA scene in 3 tiles and the command
  processor replays the scene once per tile: tile passes were 56–60% of its busy time. Their one-pass scene
  (`scene_msaa=off`, a game hook) cut command-processor time 21%, GPU 13%, benchmark 119.5 → 137.4 fps (+15%).
- **PGR4: ReGPU's tile merge** (`d0204cd`): 3,270 of 8,100 draws a frame were the second tile's copies; dropping
  them took races ~63 → ~75 fps (+19%).
- Other cars also caused hitches in FH1R (new pipelines waited for at the frame end, `664d33a`; first readbacks of
  each car's 64x64 render targets, `bbddfee`); FH1R's AI/traffic cost on the CPU side is "the command processor
  (more draws) and the audio mixer (more cars)".

**The FM3 lead: our scene draws are recorded twice, AI cars included.** FM3 draws its 720p scene in two EDRAM
tiles (rows 0–511, 512–719). ReGPU has the tile merge (`renderer.cpp` `TileMerge`, on by default,
`REGPU_NO_TILE_MERGE=1` off), **but not with the draw thread** (`gpu.cpp` ~983: "Not with the draw thread yet:
merged there, the second tile's area came out unrendered"), and the draw thread is FM3's default since session 14.
PGR4's vendored ReGPU has the same restriction. So every scene draw of every AI car goes through the command
processor and draw thread twice. Never measured on FM3.

**Steps (ranked):**
1. **Find the bound** in a paused race with AI cars: 1440p 4x vs 1280x720 1x (`out/user/settings.ini`, back up
   first), `Fm3 "status"` fps and draws. If 720p is much faster, the GPU is the limit at 1440p, not draws.
2. **Does the tile merge engage without the draw thread?** `set FM3_NO_DRAW_THREAD=1` (temporary `play.cmd`
   line), grep the log for `tile merge: surface` (logged when a surface's tiles are learnt), compare race fps and
   draws with the default. Then live `Fm3 "switch REGPU_NO_TILE_MERGE 1"` for its share.
3. **Make the tile merge work with the draw thread** (a game-neutral ReGPU patch; PGR4 and upstream would
   gain too: report it). Likely the biggest single lever near AI cars (PGR4 +19%, FH1R's equivalent +15%).
4. **Per-draw cost cuts** from FH1R, as ReGPU patches (ideas, not code): reuse the previous draw's render-target
   setup when unchanged (−7.3–7.5% command-processor time per 1,000 draws, 158 → 165 fps); check an atomic flag
   before exchanging it (a locked `xchg` per draw was 3.6–3.9% of the command processor, `c1ad9cc`); descriptor
   and binding reuse (−7.4% per draw, 133.4 → 147.3 fps); skipping unchanged constant writes measured **worse**.
5. **Unmeasured switches already in our pin:** `REGPU_SUBMIT_DRAWS=512` (PGR4 FX 35.7 → 43.9 fps; FM2,
   GPU-bound, lost 4–17%; skipped on workers, unknown with the draw thread), `REGPU_CONSTANTS_IN_RING` /
   `Config::lowPerformance` (constants are FM3's biggest command-processor bucket; ~20% on an FX-8350, slower on
   fast CPUs), `REGPU_UPLOAD_STABLE_REUSE` (FH1 +1–5%; risk: a buffer one frame stale).
6. **Fewer AI-car draws** as a low-end option: FH1 gives only the two nearest other cars LOD0 (`f95b52c`). Find
   FM3's car LOD selection (OPEN-ISSUES L) and offer a lower-detail setting for AI cars. Costs detail, not accuracy.

Caveat: time trial differs from a race in more than AI cars (ghost car, field of cars at the start); compare the
same track and camera, paused, as in session 14.

## Performance on integrated GPUs and APUs (session 18, 2026-11-09)
Low-end machines are limited on both sides: a GPU with shared memory bandwidth, and a 4-core laptop CPU sharing
its power budget with that GPU. FH1R's per-draw command-processor cost on players' PCs: 1.5–1.6 µs (recent
desktops), 2.2–2.9 (Zen 3 / Comet Lake), 3.6–3.8 (4-core Zen 1/2), ~6 (Ryzen 7 7730U laptop).

**GPU side (ranked by expected gain):**
1. **Render scale + upscaler** (in ReGPU already: `Config::upscaler` FSR1/FSR3/DLSS, quality 1.5/1.7/2/3x per
   side, HUD at full resolution; FM3 wires it, launcher says "not yet tested on FM3"). FH1R's 540p scene: +44%
   GPU-bound (35 → 50 fps). Test FM3's menus (PGR4 dropped FSR1: menu panels drawn twice at two scales, `dd2f4dd`);
   FM4 forces MSAA off under FSR3/4 (`a09fc86`). Any upscaler turns the draw workers off (not the draw thread).
2. **MSAA 1x** (launcher). FH1R: no gain when CPU-bound, real gain when GPU-bound.
3. **iGPU defaults at first launch** (FM2 `0623b1f`: D3D12 UMA → 77% render scale + FSR1, sharpening 25; an
   Iris Xe report: 42 fps at 720p). Port the detection into our launcher.
4. **Texture budget:** PGR4's vendored ReGPU evicts unused textures past 1 GB (`REGPU_TEXTURE_BUDGET_MB`; without
   it PGR4 reached 280,000 textures and ran out of heap). Our pin has no limit; on shared-memory iGPUs this matters
   more. Unmeasured on FM3: log the texture count over a long session first.
5. **Small GPU-side items:** `REGPU_LEAN_FETCH` (FH1 GTX 970 20.2 → 21.7 fps, PGR4 +1 fps); 16-bit pixel shaders
   (section above, low priority).
6. **Submission size on UMA GPUs:** FH1R ends a submission every 1,000 draws on shared-memory GPUs (device lost
   6 of 46 starts without, 0 of 10 with: "a hint, not proof").

**CPU side:**
1. **Frame cap on iGPU machines:** an uncapped main loop takes the power budget the iGPU shares (FH1R #263:
   164 loops a second on 2.9 cores). Default the cap to 60 (or the display rate) when a UMA GPU is detected.
2. **Thread gating:** FM3 sets 3 draw workers and the draw thread with no core check (`src/gpu_host.cpp` ~165–186).
   PGR4 enables workers only with ≥6 hardware threads and measured the draw thread at −7 fps on an FX; FH1R's
   decoder/recorder split is on only with ≥6 cores (−11.4% on 4 cores in free roam). Measure on 4 cores; gate.
3. **Sleeping GPU waits:** the render thread spins in D3D's ring/fence wait (`sub_824B5048`). FM2 `480e94a`:
   render thread 100 → 36% of a core; FH1R: 86 → 45% and **+10% fps on a 4-core laptop** (no change on 8 cores).
   Needs ReGPU to `WakeByAddressAll` after its guest writes (never done in our pin) plus an FM3 hook.
4. **Present without waiting for the frame's replay** (FH1R `0414`): +15.3% uncapped on 4 cores, 0% when GPU-bound.
   Check whether ReGPU's present has an equivalent wait.
5. **Per-draw cuts** (previous section, step 4) help weak CPUs most.
6. **Shader/pipeline warm-up:** FM3 uses neither ReGPU's pipeline seed (`Host::pipelineSeed`) nor
   `REGPU_OPTIMIZE_SHADERS`; first-run hitches are worse on weak CPUs (PGR4 seeds 4,681 of 5,873 pipelines).
7. **Code generation:** x86-64-v3 for the recompiled code (FH1R: CPU −5%, fps unchanged); low priority.

**Memory (8 GB laptops):** FH1R found its XMA decoder pre-allocating 183 MB of codec contexts it mostly never used
(race RSS 1,204 → 1,037 MB after the fix); our decoder keeps a codec pool too (FM2 `c830cbc`: up to ~120 MB): check.

**Found in our code:** `src/settings.h` says its `Upscaler` numbers match ReGPU's, but our pinned ReGPU has no FSR 4
(`regpu.h`: Off, Fsr1, Fsr3, Dlss), so `upscaler=fsr4` in `settings.ini` passes an out-of-range value 4
(`gpu_host.cpp` ~55). The launcher doesn't offer FSR 4; only a hand edit hits it. Map it to Off or FSR3.

## Choosing the GPU in the launcher (session 18, 2026-11-09)
**Goal:** pick which GPU (dedicated card or integrated GPU/APU) the game renders on, from the launcher.

**Today:** ReGPU creates its device with `D3D12CreateDevice(nullptr, ...)` (`thirdparty/ReGPU/src/render/presenter.cpp`
~129): the default adapter, adapter 0 in DXGI's list. Neither FM3 nor ReGPU enumerates adapters or exports the
hybrid-laptop hints, and the launcher has no GPU code. On a laptop with Optimus or AMD switchable graphics the game
can end up on the integrated GPU. Windows' own per-app choice (Settings → Display → Graphics, stored under
`HKCU\Software\Microsoft\DirectX\UserGpuPreferences` as `GpuPreference=1;` power saving / `2;` high performance)
changes which adapter is the default.

**What the other ports did:**
- **FH1R** (`docs/rendering.md`, commit `3cf09e1`): a GTX 1050 + UHD 630 laptop rendered on the Intel GPU and lost
  the device at the intro. Their ReGPU plugin now takes the first adapter in DXGI's **high-performance order**,
  `REGPU_ADAPTER=<n>` picks another, and the log names the adapter in use. Their launcher lists the GPUs
  (`launcher/forza_launcher/gpu.py`: `Win32_VideoController` through PowerShell, `lspci`/sysfs on Linux) and passes
  the card chosen in Settings (`--gpu_vendor`: nvidia/amd/intel or a PCI id; fallback: most dedicated memory).
  They note a plugin DLL can't export `NvOptimusEnablement` for the exe; **we own `fm3.exe`, so we can.**
- **FM2** (`launcher/FM2Launcher/SystemCheck.cs`): the launcher calls `D3D12CreateDevice` itself through P/Invoke and
  asks `D3D12_FEATURE_ARCHITECTURE` whether the default GPU is UMA (integrated), to choose first-run iGPU defaults.
  The same approach lists every adapter (DXGI `EnumAdapters1` + `GetDesc1`) without a helper exe.

**Design (four parts):**
1. **ReGPU (game-neutral patch):** before `D3D12CreateDevice`, enumerate with `IDXGIFactory6::EnumAdapterByGpuPreference(
   DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)` (falls back to `EnumAdapters1`), skip software adapters
   (`DXGI_ADAPTER_FLAG_SOFTWARE`, WARP), and choose by `REGPU_ADAPTER`: empty = first in high-performance order;
   `<n>` = index in that order; `vendor:device` (hex PCI ids) or a name substring = that card. Fall back to the first
   on no match, and log every adapter (name, vendor/device id, dedicated and shared memory, UMA) and the chosen one.
   Store the pipeline library per adapter (`pipelines-<vendor>-<device>.bin`): today one `pipelines.bin` is
   rejected by the other GPU's driver and rebuilt, so switching back and forth rebuilds every time. The DXBC shader
   cache (FXC output) is GPU-independent and can stay shared.
2. **fm3.exe:** export `NvOptimusEnablement = 1` and `AmdPowerXpressRequestHighPerformance = 1` (two
   `extern "C" __declspec(dllexport)` globals), so "Automatic" means the dedicated GPU on hybrid laptops even when
   the driver profile would pick the integrated one. An explicit `REGPU_ADAPTER` still overrides.
3. **Launcher:** a Graphics setting "GPU": *Automatic (fastest)* plus one entry per adapter, e.g.
   "AMD Radeon RX 7900 XTX (24 GB)", "AMD Radeon Graphics (integrated, shared memory)". Listed in C# with DXGI
   P/Invoke as FM2's `SystemCheck.cs` does (factory → `EnumAdapters1` → `GetDesc1`; UMA via
   `D3D12_FEATURE_ARCHITECTURE` on a device made on that adapter). Save it in `settings.ini` as
   `gpu=<vendor>:<device>` plus the name; **not the LUID, which changes at every boot**. Pass it as `REGPU_ADAPTER`
   (as `Install.cs` already passes `REGPU_NO_ANISO`). Grey out DLSS unless the chosen GPU is NVIDIA; choosing an
   integrated GPU can offer the iGPU defaults (RESEARCH "Performance on integrated GPUs and APUs").
4. **The log line** `gpu: ReGPU at WxH, msaa N` gains the adapter name, so A/B runs show which GPU ran.

**Caveats:**
- **Monitor on one GPU, game on the other:** on a desktop with the monitor on the card, rendering on the iGPU
  makes Windows copy each frame across to the display GPU (and the reverse). It works (flip-model swap chains are
  cross-adapter capable) but costs latency and bandwidth: show a note in the launcher. On hybrid laptops this is the
  normal path.
- **A GPU that disappears** (eGPU unplugged, driver reset) falls back to Automatic with a log line, not a crash.
- Windows' per-app setting stays as a fallback the user can also use; the launcher doesn't need to write that
  registry key.

**Effort:** small and contained: ~60 lines in ReGPU (adapter choice, log, per-adapter library name) as patch 19,
two exports in FM3, and a launcher setting with ~80 lines of DXGI P/Invoke. Testable on the user's PC
(7900 XTX + the Ryzen iGPU if the CPU has one enabled).

## Less RAM, VRAM and memory traffic for iGPUs/APUs (session 18, 2026-11-09)
**Why it matters:** an iGPU has no VRAM of its own. "VRAM" is a carve-out of system RAM (often 512 MB–2 GB) plus
shared system memory, and every texture read, render-target write and copy competes with the CPU for the same
dual-channel DDR4/DDR5 bandwidth (~50–100 GB/s, against ~960 GB/s on the user's 7900 XTX). So on an iGPU, **bytes
moved per frame** matter more than shader math. Read from ReGPU at our pin (`thirdparty/ReGPU/src/gpu/renderer.cpp`,
`texture.cpp`) plus the other ports; nothing measured on an iGPU.

### Where ReGPU spends memory and bandwidth today
- **Render targets at twice the console's size per pixel.** Every EDRAM colour surface is `R16G16B16A16_FLOAT`
  (`kColorFormat`, `renderer.cpp` ~319: 8 bytes a pixel, chosen so the 7e3 HDR format fits), and every depth surface
  `D32_FLOAT_S8X24_UINT` (8 bytes). The console's are 4 bytes each (8888 / 2_10_10_10 / 7e3 colour, D24S8 depth).
  Multiplied by host resolution² and host MSAA samples: at 1440p 4x MSAA one colour + depth pair is
  2560×1440×4×16 B ≈ 236 MB, and every write, blend and resolve moves those bytes. At 720p 1x it is ~15 MB.
- **Texture uploads go through four memory passes.** The CPU reads guest memory, untiles and writes the blocks into
  the frame's upload ring (`CopyTextureBlocks`), then the GPU reads the ring and writes a texture in a default heap
  (`CopyTextureRegion`, ~5240–5290). On a dedicated GPU the last write lands in VRAM; on an iGPU all four passes hit
  the same system RAM. DXT1/3/5, DXT5A and DXN stay block-compressed (BC1–BC5), which is good. **Small uncompressed
  formats are expanded to 4 bytes a texel on the CPU** (`DecodeTexel`: k_8, k_8_8, k_5_6_5, k_1_5_5_5, k_4_4_4_4 →
  RGBA8: 2–4x their size).
- **Upload rings: 3 × 128 MB** of upload heap (system memory), one per frame in flight (`kFrames = 3`), or
  **3 × 384 MB = 1.15 GB** when guest memory can't be mapped for the GPU (the copy path; ~2010). They grow on
  demand to 2 GB (`kRingMax`). `REGPU_RING_MB` / `Config::uploadRingMB` set the start size.
- **Guest memory mapped for the GPU** (zero-copy, `OpenExistingHeapFromFileMapping` / `...FromAddress`, ~1753–1791):
  vertex and index data are read in place. **On an iGPU this is the ideal path** (no copies). On a dedicated GPU the
  GPU reads them over PCIe each frame; FM2 still measured it faster than copying (211 vs 150 fps).
- **Change detection** samples guest textures with `ContentHash` (a 256-byte stride, not every byte), staggered over
  frames (`REGPU_TEXTURE_CHECK_STAGGER`, an FM3 default): already light.
- **No texture budget:** the texture cache (`textures`, `cubes`, `volumes`, `mipChains` maps) never evicts unused
  textures; PGR4's vendored ReGPU added a 1 GB budget after reaching 280,000 textures.
- **ReGPU never asks the OS for its memory budget** (`IDXGIAdapter3::QueryVideoMemoryInfo`), so nobody sees an iGPU
  running past its carve-out into shared memory (slower again, and the cause of FH1R's black textures and driver
  timeouts on small carve-outs, `bccb7fd`, `11496a5`).

### What the other ports did
- **FH1R** (`docs/perf.md` "Memory ... for 8 GB PCs", #640): XMA decoder pre-allocated 183 MB of codec contexts it
  mostly never used (race RSS 1,204 → 1,037 MB after the fix); no NVIDIA driver pipeline file (private bytes
  3.29 → 2.72 GB); render targets sized to the rows actually drawn (est. 250 MB) and a 192 MB texture cache
  (both unmeasured); on small carve-outs new textures go to system memory when the carve-out is nearly full
  (`bccb7fd`), and submissions end every 1,000 draws on UMA GPUs against driver timeouts (`11496a5`).
- **PGR4:** texture eviction past a budget (`REGPU_TEXTURE_BUDGET_MB`, default 1 GB, `REGPU_NO_TEXTURE_EVICT`).
- **FM2:** the vertex arena keeps large vertex buffers on the GPU when guest memory can't be mapped (GPU
  3.34–3.55 → 2.59–2.97 ms, `a6edeb7`), and detects changes by write-watching pages instead of hashing
  (`0c9a747`); the launcher's first-run iGPU defaults (77% scale + FSR1).

### Options, ranked (gain for iGPUs vs effort)
1. **Measure first (small, ReGPU):** log `QueryVideoMemoryInfo` (local and non-local: budget and usage) every
   ~10 s and at loads, the upload ring's peak (`g_ringPeak`) and the texture count. One run on an iGPU then shows
   whether FM3 overruns the carve-out and how big the ring really needs to be.
2. **Settings (no code, biggest effect):** render scale + FSR and MSAA 1x cut render-target bytes with the pixel
   and sample count (1440p 4x → 720p 1x is 16x fewer). These are the iGPU defaults to ship (section
   "Performance on integrated GPUs and APUs").
3. **Smaller upload rings on iGPUs (small):** the launcher sets `REGPU_RING_MB` (e.g. 32–64) when the GPU is UMA,
   once step 1 shows the real peak; the ring still grows if a frame needs more. Saves ~200–300 MB of RAM (more on
   the copy path).
4. **Texture budget with eviction (medium, port from PGR4):** keeps the carve-out from overflowing over long
   sessions. On UMA, set the budget from `QueryVideoMemoryInfo` instead of a fixed 1 GB.
5. **Native small formats (small–medium):** upload k_8 as `R8_UNORM`, k_8_8 as `R8G8_UNORM`, 565/1555/4444 as
   `B5G6R5`/`B5G5R5A1`/`B4G4R4A4_UNORM` with the component mapping done in the SRV (`Shader4ComponentMapping`)
   instead of expanding to RGBA8 on the CPU. 2–4x smaller and faster to upload for those textures. First count
   how many FM3 uses (a capture's texture list).
6. **Untile on the GPU (medium–large):** a compute shader reads the tiled texture straight from the mapped guest heap
   and writes the texture: two memory passes instead of four, and no CPU time per upload. Helps loads and streaming
   (hitches when new cars or track sections appear) more than steady fps. Xenia does it this way.
7. **Write textures in place on UMA (medium):** on `CacheCoherentUMA` GPUs (`D3D12_FEATURE_ARCHITECTURE`), place
   textures in a CPU-writable custom heap and write them directly (`WriteToSubresource`), skipping the ring copy.
   Driver support varies; keep the ring path as fallback.
8. **Half-size render targets where the console format allows (large, risky):** choose the host format per surface
   from the guest's (8888 → `R8G8B8A8_UNORM`, 2_10_10_10 → `R10G10B10A2_UNORM`; 7e3 → `R11G11B10_FLOAT` when
   alpha is unused, else 16F; D24S8 → `D24_UNORM_S8_UINT` on NVIDIA/Intel). Halves render-target bandwidth, but
   ReGPU uses one format everywhere because EDRAM is aliased (the same bytes read as another format by a later
   pass, depth read as 8888, resolves): every alias needs a conversion. Only after a GPU profile shows render
   targets as the bound.
9. **RAM (8 GB laptops):** check our XMA codec pool against FH1R's 183 MB finding (FM2's pool grew to ~120 MB,
   `c830cbc`); keep the pipeline library compacted (ReGPU does already); size render targets to the rows drawn
   (FH1R's idea).

Not worth it: replacing the zero-copy mapping (already ideal on UMA), finer change detection (already sampled
and staggered), 16-bit shader math for bandwidth (section above: it saves registers, not memory traffic).

## Ultrawide (21:9, 32:9) (session 18, 2026-11-09)
**Today:** the launcher already offers Aspect ratio *Auto / 16:9 / 21:9 (experimental) / 32:9 (experimental)*
(`Install.cs` `Aspects`), written as `aspect=` in `settings.ini`. `Settings::RenderSize` (`src/settings.cpp` ~140)
fits that aspect inside the resolution (bars for the rest) and never goes narrower than 16:9. **Never tested on
FM3** (no note in NOTES/OPEN-ISSUES).

**What ReGPU does at wide outputs (our pin, `renderer.cpp` ~60–90, ~286–302, ~9650):**
- Only passes at the game's full 1280-pixel pitch (`WidePass()`: the main view and its full-screen post) scale
  horizontally to the full output width; every other pass (reflection faces, shadow maps, reduced-size post)
  scales uniformly, so square targets stay square. FM3's scene tiles are 1280 wide, so the race view qualifies.
- **Hor+ in the vertex shader:** perspective geometry in a wide pass is narrowed (`vwide`) so the picture is not
  stretched; the view gains width at the sides. `REGPU_WIDE_FOV` (0–100, default 100) trades side view for a
  vertical zoom (0 = keep the 16:9 horizontal view and crop top/bottom).
- **2D is squeezed back into the middle 16:9** (`vwide[2]`; `REGPU_NO_HUD_SQUEEZE=1` stretches it with the view):
  the HUD and menus keep their shape.
- `regpu::SetGameWidescreen(true)`: a port that widens the game's own camera says so, and ReGPU then stops
  narrowing perspective geometry itself (only the `REGPU_WIDE_FOV` zoom remains).

**The missing piece in FM3: the game's frustum culling stays 16:9.** ReGPU widens the picture, but the game still
culls everything outside its 16:9 view, so objects at the sides are missing or pop in late. FM4's port lists this
as its ultrawide known issue (`TESTING.md`). **FM2 fixed it** (`6f2980b`, `src/hooks/camera_aspect.cpp`, ~50
lines): it found the camera's aspect setter (`stfs f1,268(r3)`, called every frame of a race with 16/9) with a
float scan (`FM2_SCAN_FLOAT`, the same tool FM3 already has as `FM3_SCAN_FLOAT` in `src/hooks/common/mem_scan.cpp`),
and the hook passes the wider aspect instead. Result: the game's projection and frustum culling cover the wider
view (3440x1440: 1,933 draws a frame with the hook against 1,852 without, same frame rate). Follow-ups:
- `555f54d`: the setter runs every frame in races and never in menus, so a recent call means "race on screen":
  menus stay 16:9 with black bars, races go wide.
- `f2e98ed`: also require last frame's depth-tested scenery, so loading/saving screens stay in menu mode.
- `f1f4b62`: the race's car diagram (a HUD element) kept whole inside the HUD's 16:9 area.
- Remaining in FM2: some side objects still pop in through other game-side visibility (not the main frustum).

**FH1R** (ReXGlue, ideas only): native ultrawide with proportional HUD placement (HUD anchored to the screen edges,
`67a9b7b`, `docs/graphics-menu.md`), off-screen reflection and bloom passes kept out of the wide scaling, a
correction for the cockpit instruments (`6b4c29e`), 1080-line ultrawide windows rendered at 1440 lines and
downsampled (`8432b90`), and a note that shadow cascades fitted to the 16:9 frustum cover less of a wider view
(`docs/graphics-detail.md`).

**Plan for FM3:**
1. **Test what works today, without an ultrawide monitor:** set Aspect 21:9 at 2560x1440 (the game draws
   2560x1097 with bars) and 32:9. Check a race (sides: missing objects, pop-in), the HUD, menus, the pre-race
   camera, cockpit view (instruments), rear-view mirror, sun flare position, shadows at the sides, photo mode and
   replays. Screenshots with the timer for each.
2. **Find FM3's camera aspect setter:** in a paused race, `set FM3_SCAN_FLOAT=3FE38E39,<frame>` (16/9 as a float)
   logs every guest address holding it; take the one inside the camera object, then find the function that
   stores it each frame (a breakpoint-free way: the scan at two frames, then the xex's `stfs` to that offset).
   Our `diagnostics.cpp` already reserves `g_cameraAspectWidened` for this hook.
3. **Hook it as FM2 did:** pass the wider aspect only in races (a recent setter call), call
   `regpu::SetGameWidescreen(true)` while widened, keep `REGPU_WIDE_FOV` as the side-view/zoom control, and an
   `FM3_NO_CAMERA_ASPECT=1` undo switch. Measure draws with and without (the sides now drawn).
4. **Menus:** pillarbox (FM2's choice) vs stretched backgrounds with a centred 16:9 layer; decide after step 1.
5. **Later:** HUD anchored to the edges (FH1R-style; needs per-element positions), shadow-cascade reach at the
   sides, cockpit instruments if step 1 shows them distorted, and a launcher FOV slider (`REGPU_WIDE_FOV`).

**Effort:** step 1 is a play test; steps 2–3 are about a session (one hook plus the scan); menus and HUD work after.

## Shaders: precompilation, decompilation, discovery tools and building (session 18, 2026-11-09)

### How ReGPU builds shaders today (our pin)
1. **Find the microcode:** from the command stream when a draw binds it; ahead of use by scanning guest memory for
   the shader objects a level load creates (`src/gpu/shader_scan.cpp`: the IM_LOAD_IMMEDIATE packets, header
   `0xC0nn2B00`, embedded in each object); and optionally from a list of every shader in the game's files
   (`Host::staticShaders`, compiled on first boot behind a boot screen).
2. **Translate** Xenos microcode to HLSL (`hlsl.cpp`; vertex shaders are keyed with their vertex-fetch fields
   masked, since the title patches fetch fields into copies at load).
3. **Compile** with FXC (`d3dcompiler_47`) to DXBC shader model 5.1, first with `D3DCOMPILE_SKIP_OPTIMIZATION`
   (fast; `renderer.cpp` ~3516) so the draw isn't held long, cached per shader in `Config::userDir`.
   `REGPU_OPTIMIZE_SHADERS` (+ `REGPU_OPTIMIZE_THREADS`) rebuilds them with `OPTIMIZATION_LEVEL1` in the
   background (~3311–3380). **FM3 has never measured it.** GPU drivers optimise DXBC again when they turn it into
   GPU code, so the gain is unknown (larger on weaker driver compilers, possibly Intel's).
4. **Pipelines** (shader pair + render state) through `CreateGraphicsPipelineState`, stored in the pipeline
   library (`pipelines.bin`, compacted, keyed by driver) and listed in `pipelines.manifest` (hashes and render
   state only, no game data). **Lookahead** (`gpu.cpp`, `REGPU_NO_LOOKAHEAD=1` off) walks the commands queued
   behind a draw that needs a new pipeline and builds the whole set together. FH1R adopted the idea and measured
   worst frames 518 → 134 ms on a cold cache (`docs/perf.md` "Pipelines built ahead of their draws").
5. Draws wait for their shaders by default (`REGPU_ASYNC_SHADERS`, `REGPU_ASYNC_PIPELINES` opt-in: skip instead).
   FM3-QUIRKS Q-shader notes hl360 needed `async_shader_compilation = false` for the same reason (skipped draws
   show as missing objects).
6. `Host::pipelineSeed`: a shipped pipeline list built once on first boot (those needing a run-time shader are
   built once it exists).

**FM3 uses neither `staticShaders` nor `pipelineSeed`** (`src/gpu_host.cpp`; session 7: "Not copied: boot screen,
static shader scan (`.fxobj`), pipeline seed"). So every shader and pipeline is first met in play: the
"first-time shader compile" stalls in the session-7 race load (up to 1.37 s) and spikes in the first race.

### What the other ports ship
- **FM4** (`src/static_shaders.cpp`, 247 lines): its shaders are in `.fxobj` effect files, loose under
  `Media/Shaders` and inside each track's `bin.zip` (LZX, zip method 21), 4-byte aligned without a packet header,
  plus a few dozen in the executable. A candidate counts as a shader when its control flow decodes cleanly (valid
  opcodes to an end flag, first exec address = control-flow length, exec ranges tiling the instruction slots).
  The list is saved so later boots skip the search. **FM3 already has the pieces it needs** (`src/zip.cpp`,
  `src/lzx.cpp`); FM3 is the same Turn 10 engine generation, so its data very likely uses `.fxobj` too (check).
- **PGR4** (`src/gpu/static_shaders.cpp`, `pipeline_seed.h`, `boot_screen.cpp`): static shaders plus a pipeline
  seed generated from **players' uploaded manifests** (`tools/pipeline_seed.py`: 6,529 pipelines from 658
  manifests, 220 vertex-shader variants); first-boot seed coverage 4,681 of 5,873 pipelines once vertex-shader
  variants were precompiled too (`58e81a2`).
- **FM2** (`da37c3c`): the 33 vertex shaders every player compiled during play come precompiled.
- **FH1R** (`docs/rendering.md`): first start compiles all 1,688 game shaders behind a boot screen, then the
  1,183 seed pipelines; later starts load them in ~20 s. On a 4-core i5-10300H: 488 s for shaders + 70 s for
  pipelines (~10 minutes), so show progress and make it a one-time cost.

**Don't ship compiled shaders.** DXBC/DXIL translated from the game's microcode is derived game data. The ports
compile on the player's machine and ship only manifests (hashes and render state).

### Discovery and decompilation tools
| Tool | What it does for us | Where |
|---|---|---|
| `tools/scanshaders.py` (ReGPU) | Finds shader objects in the game data offline (loose files and zips; IM_LOAD packets), writes JSON. Python's `zipfile` can't read LZX (method 21) zips: for those, FM4's C++ scanner. | `thirdparty/ReGPU/tools/` |
| `tools/xenos_dis.cpp` (ReGPU) | Xenos microcode disassembler | same |
| Debug server (`tools/fm3ctl.ps1`) | `capture` (each draw's vs/ps hashes, textures), `hlsl <hash> <file>` (the translated HLSL), live `ps`/`vs` edits | ours |
| `pipelines.manifest` | Every pipeline a session built (hashes + state): the raw material for a seed | `out/user` (ReGPU user dir) |
| **XenosRecomp** (hedge-dev, MIT) | Converts Xbox 360 shader binaries to HLSL offline, using the XDK shader container's **reflection data** (named constant registers), compiled with DXC to DXIL/SPIR-V. Built for Unleashed Recompiled's translation-layer renderer, "do not expect it to work out of the box". For us: **readable HLSL with named constants** to understand FM3's effects (if `.fxobj` holds those containers); not a runtime path for ReGPU (no EDRAM model, game-specific vertex fetch). | `github.com/hedge-dev/XenosRecomp` |
| Xenia (`--dump_shaders`, `xenia-gpu-shader-compiler`) | Dumps each shader's microcode, disassembly and its own translation; the standalone compiler turns a microcode file into disassembly/DXBC/SPIR-V. A second opinion when our translation looks wrong. | Xenia source (BSD) |
| RenderDoc 1.47 (installed) | D3D12 frame capture: pipeline state, DXBC disassembly, shader debugging of our translated shaders | local |
| PIX on Windows | Per-draw GPU timing and occupancy: which translated shaders cost the most | Microsoft |
| Radeon GPU Analyzer / Profiler | Compiles DXBC/HLSL offline to RDNA3 ISA with VGPR count and occupancy; RGP times a frame on the user's 7900 XTX | AMD GPUOpen |
| Intel GPA, NVIDIA Nsight Graphics | The same for Intel iGPUs and GeForce cards | vendors |
| `fxc /dumpbin`, `dxc -dumpbin` | Disassemble a cached DXBC/DXIL file | Windows SDK |

### Options for building shaders, ranked
1. **Port FM4's static shader scan** (`Host::staticShaders`) with a boot screen: first boot compiles every
   shader in the game files; no first-play compile stalls. First check FM3's data for `.fxobj` (or run
   `scanshaders.py` on the extracted game; LZX zips need the C++ path). Effort: about a session (FM4's 247 lines
   plus FM3 paths; the zip/LZX readers are already ported).
2. **Pipeline seed** (`Host::pipelineSeed`): collect `pipelines.manifest` from the user's play sessions (and any
   testers'), merge with a small script as PGR4's `tools/pipeline_seed.py` does, embed as a header. Precompile the
   vertex-shader variants the manifests name (PGR4 `58e81a2`). Effort: small once step 1 exists.
3. **Measure `REGPU_OPTIMIZE_SHADERS`** (GPU time on the 7900 XTX and, later, an iGPU): a free A/B.
4. **Profile translated shaders** with RGA/RGP: the heaviest pixel shaders by VGPRs and time point at translator
   improvements (and tell whether the 16-bit idea above would pay at all).
5. **DXC / shader model 6** instead of FXC (large): often faster compiles on big shaders, DXIL, 16-bit types and
   wave ops, but a new cache format and shipping `dxcompiler.dll` + `dxil.dll`. Not in any ReGPU branch yet
   (upstream `engine-optimisation` only adds `REGPU_OPTIMIZE_LEVEL` for FXC, untested).
6. **Emit DXBC/DXIL directly** from microcode, as Xenia's D3D12 backend does (no compiler, near-instant
   shaders): the fastest possible first boot, but a rewrite of the translator. Not recommended for this project.
