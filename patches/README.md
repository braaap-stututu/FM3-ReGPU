# XenonRecomp patches

Applied in this order to `thirdparty/XenonRecomp` at `ddd128bcca99fe8bfbb99bea583c972351fa6ace`
by `tools/setup.ps1` (which checks the whole stack against the working tree, so it is safe to re-run).

| # | Patch | From | What |
|---|---|---|---|
| 1 | `xenonrecomp-fm2.patch` | FM2-ReGPU / FM4-ReGPU | PowerPC divides never trap (zero divisor / overflow give a value); MSR[EE] toggles become `PPC_MSR_CHANGED` (the runtime's interrupt lock) |
| 2 | `xenonrecomp-modules.patch` | FM4-ReGPU | Optional function-name prefix per image (several images in one program); warns when unnamed imports become no-op thunks |
| 3 | `xenonrecomp-fm4.patch` | FM4-ReGPU | XenonAnalyse jump-table back-trace through registers (tables the fixed patterns miss), bound from masks / `cmplwi`; fallthrough count; XEX loader sizes the image by the security info too |
| 4 | `xenonrecomp-fm4-instructions.patch` | FM4-ReGPU | Missing/incorrect VMX128 and CR-bit instructions (e.g. `vsldoi128` decoded before PPC405 MAC forms) |
| 5 | `xenonrecomp-switch-u32.patch` | FM4-ReGPU | `switch` on the 32-bit table index; `vcmpbfp(128)` implemented instead of a trap |

Source: [Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU](https://github.com/Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU) commit `69bdae44d22a9e0f52733ff55b9f1d0dd5e55b2b` (`patches/`), MIT
licence (the notice it carries, from FM2: © 2026 zer0ducksgiven; full text in NOTICE.md). Taken unmodified from the git objects (LF line endings).

All five are generic recompiler fixes, not FM2/FM4 addresses. FM3 is a single image (only
`default.xex` on Disc 1), so patch 2's prefix stays unused for now.

## ReGPU patches (ours)

Applied to `thirdparty/ReGPU` at `57143cb79e8dd4de7b776c70867313cd0b1862f2` by `tools/setup.ps1`. Game-neutral
Xenos behaviour, each behind a `REGPU_*` switch, as ReGPU's own rules ask; candidates to send upstream.

| Patch | What | Switch |
|---|---|---|
| `regpu-fixed16-fetch.patch` | A resolved 16-bit fixed image (copy destination `k_16`, `k_16_16`, `k_16_16_16_16`) read with a **different number format than it was copied in**: back to the 16 bits the console stores (fraction copies hold fraction × 65535, or × 32767 signed; integer copies the integer), then read as the fetch asks (unsigned fraction = bits / 65535, signed fraction = value / 32767, integer = bits). The same format on both sides is unchanged; the border colour is never converted. ReGPU returned the stored values as they were. In FM3 this affects the variance shadow map (resolved as signed integers with exp bias +10, read by its blur as unsigned fractions). History: the first version also converted same-format reads and the border, which darkened the sunlit road and drew a square around the car; corrected (session 8c). It did **not** fix the road streaks (that is the strip write-back in `src/gpu_host.cpp`). | `REGPU_NO_FIXED16_FETCH=1` (start-up) |
| `regpu-zpd-tile-spans.patch` | Occlusion queries under predicated tiling: D3D replays each tile's commands, so a query's ZPD events come as a count-on/count-off pair per tile, and only the last carries the end markers. A second start while counting now pauses the query and the next one resumes it (a new D3D query part); the end sums the parts. ReGPU had counted one span from the first start to the end, including every draw of the next tile's replay. FM3's sun query went from 6,472,508 samples to 5,780: the glare no longer whites out, and the coverage re-draws of the second tile are gone. Also: `REGPU_TRACE_ZPD` logs `RB_SAMPLE_COUNT_CTL` and allows 12 more lines each time it is switched on. | `REGPU_NO_ZPD_SPANS=1` (live) |
| `regpu-resolve-into-image.patch` | A resolve with `rect.top > 0` whose destination address **already holds a resolved image of the same width covering the rectangle's rows** is placed into that image at its own rows. ReGPU treated every full-width resolve with a row offset as a predicated-tiling tile, whose destination is the tile's own row, and subtracted the offset. A real tile names an address where no image starts (FM3's scene tile 2: `18216000`, 512 rows in), so tiles are unchanged. FM3 rebuilds its 512×512 road detail image half at a time, rows 256–511 to the image's own base; as a "tile" that went to a phantom 512×768 image 256 rows earlier, the road image kept a stale blurred lower half, and the asphalt showed smooth bands once the chase camera turned (that's when the half-rebuilds start). | `REGPU_NO_RESOLVE_INTO_IMAGE=1` (live) |

Line endings: these files must stay LF (`.gitattributes`: `*.patch -text`). The global
`core.autocrlf=true` would otherwise make them CRLF and `git apply` fails on every hunk.
