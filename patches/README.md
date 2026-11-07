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

Line endings: these files must stay LF (`.gitattributes`: `*.patch -text`). The global
`core.autocrlf=true` would otherwise make them CRLF and `git apply` fails on every hunk.
