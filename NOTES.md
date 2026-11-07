# NOTES.md — FM3 ReGPU recomp running log

## ▶ Current status / next step  (updated 2026-11-07, session 6)

**Status:** **Phase 2 (first recompilation) is done.** `tools/recomp.ps1` turns `game/default.xex` into `out/ppc` (45,760 functions, 179 files, 164 MB) with **0 unresolved branches**, and **all of it compiles** with clang-cl 23 (`-DFM3_PPC_ONLY=ON` → `fm3_ppc.lib`, 118 MB, no errors). The pipeline is reproducible: re-running it with `-Prepare` gives the same config, the same switch tables and byte-identical output. The repo is on GitHub: **https://github.com/braaap-stututu/FM3-ReGPU** (`main`).

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

**Next steps:**
1. Your OK to install: **AMD FidelityFX SDK v2.x** → `thirdparty/fidelityfx` (ReGPU's CMake refuses to configure without it) and an **FFmpeg win64 LGPL shared** build → `thirdparty/ffmpeg` (audio). Phase 3 needs FidelityFX to link the game.
2. Phase 3: port the runtime (loader, memory, kernel/XAM, threads, `indirect.cpp`, fibers with FM3 addresses, `patch_mftb`), then boot to the first frame with vblank at 60 Hz.
3. Still your call: a licence for our code; whether to delete the empty `githubfileignorepls`.

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
- **Our project, ReGPU** (`57143cb`): none.
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

**Phase 1 — Project skeleton ✅ (session 5; licence still open)**
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

**Phase 3 — Boot to first frame ▶**
- Also from Phase 2: `PPC_FUNC` override for `sub_82A918F0` (blrl trap), SEH policy, `src/indirect.cpp` for the `PPCLookupIndirect` table declared in `src/ppc_hooks.h`, `PPCMsrChanged`, `PPCMmioStore32`.
- Kernel/XAM from FM2's `src/kernel` (re-address everything; never link FM2/FM4 `sub_` addresses).
- Fiber overrides (port FM4 `fibers.cpp` with an FM3 `addresses.h`), mftb patch, `cache:` utility-drive override (Q8), `PPCLookupIndirect` fatal-on-miss.
- **Set `REGPU_VBLANK_HZ=60` from the start (Q24)** and measure game speed.
- Goal: ESRB/Turn 10 logos and the intro video render. Compare with hl360 screenshots from `fm3_route.ps1` / `dumps/route/x1_*.png`.

**Phase 4 — Menus & content ☐**
- Disc 2 DLC (Q11–Q14), licence mask all bits (Q12), profile XUID/gamertag rules (Q15), language+country pairs (Q16).
- Reach the controls/loading screen and the intro race cutscene.

**Phase 5 — Race & GPU parity ☐**
- Resolve writeback rules for road/thumbnails (Q18). Integer num_format resolve+fetch for the car VSM (Q19: the 256×256 k_16_16 + k_24_8 pair seen at draws ~582/583 on the grid).
- Audio queue depth (Q26). XMA path check (kernel exports vs APU MMIO).
- Add a ReGPU-side dump hook that writes the **same index.txt/tex_*.bin format**, then `dumpdecode.py diff` hl360-grid vs ReGPU-grid.
- Check the post-race finish menu runs at exactly 30.0 fps (game-native present interval?).

**Phase 6 — Polish ☐** Frame-rate unlock only after confirming FM3's sim timing (Q24), launcher/settings, mods (Q9 re-signing, PF+3).

## Session log
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
