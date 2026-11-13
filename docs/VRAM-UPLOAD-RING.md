# The upload ring in VRAM (L47), and what made it win

Written in session 31 (2026-11-13) for other ReGPU ports and the FH1R lead's Vulkan path. Measured on an
RX 7900 XTX with resizable BAR on, Forza Motorsport 3. Details and every number: PERF-RESULTS.md "Session 29" (first
version) and "Session 31", NOTES.md "Start here (session 32)".

## What moved
Only **the per-frame upload ring itself**: ReGPU keeps one linear ring per frame slot (3 slots, 64 MB each at the
start, doubled when a frame runs out). Everything the CPU produces for a frame is sub-allocated from it:

| In the ring | Share of the CPU's writes (~5.4 MB a frame, frozen grid) | How the GPU uses it |
|---|---|---|
| Per-draw constant blocks (VS and PS) | ~70% | Root CBVs pointing into the ring: read in place |
| Small vertex uploads and texture upload data | ~1.3 MB | Read in place (vertices) or copied into textures |
| Expanded index lists | ~0.5 MB | Read in place |
| Staging for the vertex and index arenas | (in the above) | One copy batch at frame end into DEFAULT-heap arenas (now VRAM to VRAM) |

Large static vertex data was already elsewhere (the vertex arena, or guest memory mapped for the GPU) and did not move.

## How (D3D12)
- Support: `CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16)` -> `GPUUploadHeapSupported` (true only with resizable
  BAR / Smart Access Memory on).
- The ring: a committed row-major buffer in `D3D12_HEAP_TYPE_GPU_UPLOAD`, created in `D3D12_RESOURCE_STATE_COMMON`,
  persistently mapped; no barriers (buffers promote implicitly).
- Fallback: if creating or mapping fails, or without ReBAR, the ring stays an UPLOAD heap in system memory.
- Both rings exist per slot and are swapped at the frame start (after that slot's fence) by a live switch: a free A/B
  on a frozen scene. Costs one more ring's memory.
- Code: [`patches/regpu-gpu-upload-ring.patch`](../patches/regpu-gpu-upload-ring.patch) (~110 lines); FM3 default in
  `src/gpu_host.cpp` (`vram_upload_ring`, on unless `=0`).

## Readbacks
Nothing changed. Everything the CPU reads (resolve write-backs to guest memory, occlusion results, timestamps,
screenshots) stays in READBACK heaps in system memory. The VRAM ring is write-only for the CPU: never read through
that mapping (write-combined and uncached: reads crawl).

## Why it lost first and wins now: bytes per draw
The first version (session 29) was +12-19% on a GPU-bound frozen grid but **-13% in real races**, which were bound by
ReGPU's draw thread: every CPU store now crossed PCIe through a write-combined BAR mapping, the cost showing as a stall
on a later store; scattered 16-byte stores are the worst case. Session 30 cut what the draw thread writes:

1. Fetch table written entry by entry, not from 0 up to the highest used fetch constant: ~3,000 -> ~1,500 bytes a
   draw ([`regpu-sparse-fetch-table.patch`](../patches/regpu-sparse-fetch-table.patch)).
2. Unread block parts skipped: bool/loop constants, fetch layout and user data written only when the shader's final
   HLSL reads them: -> ~1,060 bytes a draw ([`regpu-unread-block-parts.patch`](../patches/regpu-unread-block-parts.patch)).
3. Float constants packed: each shader's literal `c[N]` reads renumbered by rank in its used-constant mask after
   translation, so a draw writes its constants as one sequential run, not scattered 16-byte stores (same bytes)
   ([`regpu-packed-constants.patch`](../patches/regpu-packed-constants.patch)).

Then, with the ring in VRAM (0 pixels differ in every test):

| Setting | Ring in system memory | Ring in VRAM | Change |
|---|---|---|---|
| Frozen race grid, 1440p 4x MSAA | 281.6 fps | 355.7 fps | **+26%** |
| 12 s full-throttle race starts, 1440p 4x | 296-301 fps | 352-357 fps | **+18%** (capped at 360) |
| Frozen race grid, 4K 4x MSAA | 186.8 fps | 228.1 fps | **+22%** |
| 720p 1x (CPU-bound) | 354.1 fps | 351.1 fps | -0.8% |

The draw thread's time a frame rose only ~4% with the ring in VRAM (10%+ before the cuts). Real drives were clearly
faster too: on by default since session 31, with a launcher toggle.

## For a Vulkan port
- Memory type `DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`. With ReBAR its heap covers all of VRAM; without it, AMD
  and NVIDIA still expose a ~256 MB BAR window with that type, enough for a few small per-frame rings.
- Persistent mapping, write-only, sequential writes, no reads, no partial-line read-modify-write.
- Fallback: a HOST_VISIBLE system-memory ring.
- Measure in a real CPU-bound scene, not only a GPU-bound benchmark. A per-kind byte counter on ring allocations
  (ours: `REGPU_TRACE_RING_KINDS`) showed what to cut first.
- A performance log that times every draw can hide the gain: ours cost 18-25% fps on the draw thread and made the
  ring look like a loss in logged race drives. We now use a light log (one record a frame through a lock-free queue to
  a log thread: [`regpu-perf-lite.patch`](../patches/regpu-perf-lite.patch)).
