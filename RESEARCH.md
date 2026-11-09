# Research

Investigations that are not bugs yet: options, feasibility studies and leads with no code behind them. Open bugs
live in [OPEN-ISSUES.md](OPEN-ISSUES.md), vinyl/livery work in [VINYLISSUES.md](VINYLISSUES.md).

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
