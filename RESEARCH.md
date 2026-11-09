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
