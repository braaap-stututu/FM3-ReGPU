# NOTES.md — FM3 ReGPU recomp running log

## ▶ Current status / next step  (updated 2026-11-08, end of session 11)

### ▶ Start here next session
All work is committed and pushed (`main`). **B2 is fixed** (session 11, you confirmed: no noise or other cars' decals in races, countdown or photo mode). What's left is **B**: the vinyls stop at a straight line along the car, in the editor and now also in races/photo mode (the race livery is copied from ReGPU's composite, so it shows the same cut). Open issues other than B are in **[OPEN-ISSUES.md](OPEN-ISSUES.md)**; the session-10/11 logs below have the chronology.

**Test setup (you):** `play.cmd` → Career → the Scion xD in `out/user` (big green vinyl + three orange circles on the RIGHT SIDE). Measure the editor's side view with `python -I tools/analysis/greentop.py <shot>` (median top row of the green over x 450–750: **262 = cut**, ~130–160 = complete; green px in the car box: 38655 when cut). In the editor's move mode I can nudge the layer myself (`Fm3 "stick L 0.5 0 120"`); after each edit the car flashes the highlight colour (blue) over the full body for a frame or two, so measure a few seconds later.

**B: what's ruled out now (don't repeat):** stencil, mask, side view, resets, region geometry, big/tall-pass rules, tile merge, draw workers, stale-resolve check, per-frame texture cache (session 10), and **speed** (session 11): normal frames slowed to traced speed (80–100 µs per command packet, ≈ a traced frame's 70 µs) still cut, with the edits confirmed going through. **Traced edits no longer come out complete** in a clean build (cut at 262 after a traced rebuild frame), so session 10's "traced = complete" depended on the cache experiment its exe still had compiled in. B is therefore a plain rendering/bookkeeping bug, not a race.

**B: next steps.**
1. **Use the boot build instead of the editor.** At boot (≈22 s, before the menus) the game builds the Scion's 2048×2048 livery (four 2048×512 strips + the vinyl regions, second strip pass with colour base `0C0`) and then the 1024×1024 one (≈29 s), and both are now written back to memory. So: `peek` the 2048 texture (base in the log: `livery writeback: <base> 2048x2048`) and decode it with `tools/analysis/strips.py` (8888, tiled, pitch 2048) to see exactly which rows/regions are missing, and compare with hl360's race livery (`dumps/hl360/frame_0001`, DXT5 1024×1024, decoded PNGs in `out/logs/hl360_race_png`: every panel full).
2. **Trace that boot build:** `REGPU_CAPTURE_ON_RESOLVE=<2048 base>` (set in `play.cmd` temporarily; the base is stable across boots if nothing else moves: 0B508000 in session 11) traces the frame of the build: every strip, region, side-view pass and the `also drawn into` lines, from boot, without the editor.
3. Lead from session 11: one traced editor rebuild frame showed the **whole car as noise** for a moment, and the log had `resolve at 0DB36000 (1378x534) is stale: the memory was reused` (a big region's side-view pass, 1378 wide) at those times. A region image judged stale is read from memory instead of the GPU: check what `ResolveCurrent`/the stale check does with the region images (`REGPU_NO_STALE_RESOLVE`-type switch, if any, or log `ResolvedFor` decisions for the 1378/1416-wide images).

**Other open items:** see OPEN-ISSUES.md (D photo-mode/pre-race band, E shadow flicker, F foliage dither, G engine audio cutouts with a strong lead, Phase 4/5 items).

**Session 9 summary:** issues A (matte paint/black chrome, k_2_10_10_10 decode) and C (My Cars pictures) **fixed**, B (vinyls) partly fixed, a race-load crash fixed, new B2 (race livery noise). An on-screen timer (top-left, the log clock to 0.1 s; ReGPU patch `regpu-timer-overlay`, `REGPU_TIMER`, FM3 default on, `FM3_NO_TIMER=1` hides it) lets your screenshots point into the log. You then explored beyond the intro race (career menus, upgrade shop, paint/vinyl editors, a solo race, photo mode). Log of that run kept as `out/logs/session9-explore.log` (times below are its `[time]`/the timer). **New issue list (not fixed yet; FM2/FM4 comparison reported to you, waiting for your go):**

| # | Issue (timer) | Our log / first lead | FM2 / FM4 port |
|---|---|---|---|
| A ✅ | Out-of-race 3D (showroom 66.6, career-menu car 576.1, paint booth 1382.1): paint dull/matte, chrome (badges, chrome rims 655.4) **matte black** | **Fixed (session 9, you confirmed):** the showroom reflection cube (`19781000`, 256×256, mipped, read from memory) is k_2_10_10_10, which ReGPU couldn't decode → black cube. ReGPU patch `regpu-cube-2101010` | FM2: same open bug ("paint turns matte, loses reflections" in garage); null cube SRV reads black. FM4 ReGPU: cube keep/stale/mips fixes; upstream has `REGPU_NO_CUBE_KEEP`, `REGPU_NO_LIVE_CUBE`, `REGPU_DUMP_CUBES` |
| B ◐ | Vinyls/decals invisible when applying (1509.2) and in the vinyl editor (1567.6), sometimes partly visible | **Partly fixed:** the car's 2048×2048 texture is built from strip and region resolves into tile positions inside it; ReGPU kept each as its own image (fixed: patch `regpu-resolve-into-enclosing`). **Still cut along a straight line along the car, only when edited in normal frames; edits in traced frames come out complete** (session 10): a timing issue in ReGPU, see "Start here". | FM2 fixed its vinyls in its own renderer (tall/big passes, foreign-MSAA stencil clears); not the cause here |
| (B, session-start notes) | `resolve … (352x131) stale` at 1483; editor uses big EDRAM surfaces (1368: bases up to B40000, 1539: 1400000) | **FM2 fixed it** in its own renderer: tall 1024-row / 2048 "big pass" livery targets, stencil clears through foreign-MSAA views rescaled, tile shift for foreign views (`FM2_NO_TALL_PASS`, `_BIG_PASS`, `_FOREIGN_MSAA`, `_MSAA_SCALE`, `_FOREIGN_TILE`). Upstream ReGPU has `REGPU_NO_FOREIGN_TILE`; we force `REGPU_NO_FOREIGN_MSAA=1` (bar fix) — may matter here |
| C ✅ | My Cars: car picture = noise blocks, centre thumbnail missing (1782.5) | **Fixed (session 9, you confirmed):** on a car change (paint saved) the game resolves a 512×256 picture and a 256×128 thumbnail, then copies them on the CPU into the car's saved images. Write-back rule in `src/gpu_host.cpp` (`FM3_NO_PICTURE_WRITEBACK`). Old saved pictures stay broken until the car changes | **FM2 fixed it**: thumbnails are rendered, resolved, then compressed/saved by the CPU → resolve write-back of those sizes, with R/B swap handled. Old saved thumbnails stay broken |
| D | Pre-race menu (119.3) and photo mode (1219.9): upper third slightly whitened (edge ≈ y 410 of 1125 in the shot ≈ row 262 of 720) | — | FM4: photo mode's 64-bit accumulation shares an EDRAM base with the 32-bit scene; fixed with vendored-only `REGPU_COLOR_SURFACE_PER_BPP` (not in upstream). FM2: photo-mode tile fixes |
| E | Flickering shadow on the player car's right rear from other cars (266.2, intro race) | — | **FM4 fixed a match**: soft car-shadow cascades; other cars' silhouettes come from a depth copy read as 8888 bytes → vendored-only `REGPU_DEPTH_AS_8888` + `REGPU_CASCADE_*` (FM4 `gpu_host.cpp`) |
| B2 ✅ | **Race: a car with vinyls is covered in noise** (session 9); photo mode/countdown: other cars' decals and noise | **Fixed (session 11, you confirmed):** the game builds each car's livery at load (boot/garage) as an 8888 2048×2048 texture (close-ups) and a 1024×1024 one (racing): first strip/base resolve = whole image, other strips and vinyl regions resolved into it; its CPU then compresses them to DXT from memory. Rule in `src/gpu_host.cpp` writes both sizes back Deferred (`FM3_NO_LIVERY_WRITEBACK`, `FM3_LIVERY_WRITEBACK_NOW`); ReGPU patch `regpu-enclosing-writeback` writes each strip/region drawn into a written-back image too (`REGPU_NO_ENCLOSING_WRITEBACK`). Cars now show the same cut as the editor (B) | FM2's port wrote its race livery back (1024x1024) |
| F | Dither/stipple on foliage and fences (365.8) | We render at host MSAA 1× (log: `anti-aliasing 1x`) | Upstream ReGPU dithers alpha-to-coverage 2×2 at 1× (Xenia-style); FM4 uses plain D3D coverage (hard cut). A/B: `REGPU_NO_ALPHA_DITHER=1` (live) |
| G | Engine audio cutouts holding RPM (1027.1) | `xma: 5 s` stats: **0 discontinuity flushes in menus, 110–200 per 5 s in a solo race, 500–730 in the 6-car intro race**, each with codec reopens and 4–11k decoded samples dropped at "stream restarts". Our seek guard (`FM3_NO_XMA_SEEK_GUARD`) + drop-ahead (`FM3_NO_XMA_DROP_AHEAD`) fire ~30×/s while driving | Both ports have the queue depth/top-up/boost/consumed-means-delivered fixes we already carry. FM2: engine loops get a fresh codec per restart plus a seamless-loop table and crossfades (`seamless_loops.h`) |

Also logged (not reported as visible bugs): `unimplemented import XamParseGamerTileKey/XamReadTileToTexture` (1598.9, gamer picture), `XamSwapDisc/XamSwapCancel` (2070), `XamShowMarketplaceUI` (2077); `vfs: no mapping for "Update:\media\cars\driver\*.gr2"` at each race load (title-update files we don't mount).

**Session 8 status:** FM3 is playable on our build. Boots, menus, races to the finish; controller, keyboard and audio work. All visual bugs you reported in session 8 are **fixed and confirmed by you**:

| Bug | Cause | Fix (where) | A/B switch |
|---|---|---|---|
| Screen-wide bar (rows ~360–511) | ReGPU "foreign MSAA" view path on FM3's two-tile 2×MSAA scene | `src/gpu_host.cpp` default `REGPU_NO_FOREIGN_MSAA=1` | `FM3_FOREIGN_MSAA=1` |
| Sun white-out + slowdown | Occlusion query counted from tile 1's start to tile 2's end (6.47M samples) | ReGPU patch `regpu-zpd-tile-spans` | `REGPU_NO_ZPD_SPANS=1` |
| Fine dashed road streaks | Road strips rendered at track load, read from memory, never written back | `src/gpu_host.cpp`: 32×512 resolves → `Writeback::Mode::Now` | `FM3_NO_STRIP_WRITEBACK=1` |
| Square car shadow (my regression) | Fixed-16 fetch conversion applied to same-format reads and the border | ReGPU patch `regpu-fixed16-fetch` (corrected) | `REGPU_NO_FIXED16_FETCH=1` |
| Smooth road bands (rotated chase cam) | Road image's lower-half rebuild filed 256 rows early as a "tile" | ReGPU patch `regpu-resolve-into-image` | `REGPU_NO_RESOLVE_INTO_IMAGE=1` |
| (S9) My Cars picture/thumbnail noise or missing | 512×256 + 256×128 car-picture resolves read by the CPU, kept on the GPU | `src/gpu_host.cpp`: those sizes (8888) → `Writeback::Mode::Now` | `FM3_NO_PICTURE_WRITEBACK=1` |
| (S9) Matte paint, black chrome out of races | Showroom reflection cube is k_2_10_10_10, unsupported in ReGPU → decoded black | ReGPU patch `regpu-cube-2101010` (+ cube face staging bug) | `REGPU_NO_2_10_10_10=1` |
| (S11) Car with vinyls: noise/other cars' decals in races, countdown, photo mode (B2) | Livery textures built on the GPU, compressed to DXT by the CPU from memory | `src/gpu_host.cpp` livery rule (2048², 1024², Deferred) + ReGPU patch `regpu-enclosing-writeback` | `FM3_NO_LIVERY_WRITEBACK=1`, `REGPU_NO_ENCLOSING_WRITEBACK=1` |

**Open / next (pick from here):**
1. **B (vinyls cut):** the "Start here" block at the top.
2. **Everything else** (D photo-mode/pre-race band, E shadow flicker, F foliage dither, G engine audio cutouts, Phase 4/5 items, leftovers): **[OPEN-ISSUES.md](OPEN-ISSUES.md)**, with evidence, sibling-port findings and a first step for each.

**How we work (agreed with you):**
- **You start the game with `play.cmd`** (project root: debug server on, saves in `out/user`, log in `out/run/fm3.log`) and drive to where a test needs to be, then report. Don't repeat the launch steps unless `play.cmd` changes; say so when it does. Temporary test lines in it get removed afterwards.
- I attach with `tools/fm3ctl.ps1`: `Fm3 "status"`, `Fm3 "capture"`, `ps <hash> tint|<expr>`, `hlsl <hash> out/hlsl/x.hlsl` (no spaces in that path), `switch NAME 0|1`, `Save-Fm3Shot`. I pause the game myself if needed. Rebuilding needs the game closed: ask first, then link with `tools/vcenv.cmd ninja -C build/rel fm3`.
- Method that worked: pause → shots → live `switch` A/B → `capture` (draw list in the log, resolved images in `out/run/out/res`, textures in `out/run/out/capture/tex`) → tint candidates to find the draw → `hlsl` → show single terms with `ps` edits → **measure** (box/edge metrics) rather than judge by eye → compare with hl360 when unsure what is correct.

### Previous status (session 7)
**Phase 3 goal reached: `fm3.exe` boots and renders.** The runtime (ported from the FM4 port, see `NOTICE.md`) runs FM3's recompiled code on ReGPU. From a fresh user folder, with no input: ESRB screen at ~8 s (correct, see `out/shots/boot_08s.png`), logo fades, then the **R8 intro video plays** (40 s: the door close-up hl360's dump showed; 70 s: the Le Mans prototype shot). **59.6 fps average** (4,140 swaps in 69.5 s, vblank 60 Hz, frame cap 60), 54 draws/frame in the video like hl360. 15 guest threads, GPU interrupt and ring buffer OK, audio driver at 188 callbacks/s with 0 dropped frames. No unimplemented-import warnings so far; only `ExGetXConfigSetting 0003/0001` and kernel variable ordinals 0x156/0x1BF are unknown.

**How to run:**
```powershell
$env:FM3_USER_DIR = "G:\claude code projects\FM3-ReGPU recomp\out\user"   # keep test saves out of Documents
cd "G:\claude code projects\FM3-ReGPU recomp\out\run"                       # fm3.log is written here
& ..\..\build\rel\fm3.exe "G:\claude code projects\FM3-ReGPU recomp\reference\rexglue recomp reference\hl360-forza3\game\disc1"
```
Build: `tools\vcenv.cmd cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link`, then `tools\vcenv.cmd ninja -C build/rel fm3` (≈ 2 min incremental; the first build compiles `out/ppc`, several minutes). Screenshots: dot-source `tools/fm3_route.ps1`, set `$Fm3Run`, then `Connect-Fm3` / `Save-Shot` (works on our window too).

**Next steps (Phase 4):**
1. Input: check keyboard/XInput reach the game (Return to skip the video), then get past the controls/loading screen to the intro race, as hl360 does in ~2.5 min.
2. Profiles/DLC (Q11–Q16): the FM4 XAM code is untested on FM3; Disc 2 DLC lives in hl360's `user/`.
3. Compare frames against hl360 (`dumps/hl360/runA`) once the start grid is reachable; Phase 5's dump hook.
4. Leftovers from Phase 2 (not hit yet): `blrl` trap in `sub_82A918F0` (SEH funclet call), SEH policy.
5. Still your call: whether to delete the empty `githubfileignorepls`. (Licence decided 2026-11-08: MIT, `LICENSE`, copyright braaap-stututu; borrowed code keeps its notices in `NOTICE.md`.)

---

### Previous status (session 6)
**Phase 2 (first recompilation) is done.** `tools/recomp.ps1` turns `game/default.xex` into `out/ppc` (45,760 functions, 179 files, 164 MB) with **0 unresolved branches**, and **all of it compiles** with clang-cl 23 (`-DFM3_PPC_ONLY=ON` → `fm3_ppc.lib`, 118 MB, no errors). The pipeline is reproducible: re-running it with `-Prepare` gives the same config, the same switch tables and byte-identical output. The repo is on GitHub: **https://github.com/braaap-stututu/FM3-ReGPU** (`main`).

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

**Deps (session 6b; FFmpeg resolved in session 7):** **FidelityFX SDK 2.1.0 is wired in** (`thirdparty/fidelityfx` is a junction to `G:\claude code projects\tools\FidelityFX-SDK-2.1.0`; all 5 signedbin DLLs Authenticode-valid, AMD). The full game CMake configures, and **`regpu.lib` builds against our `out/ppc` with FSR3 on**; `regpu_linkcheck` links with no unresolved externals. **FFmpeg still missing:** the winget `Gyan.FFmpeg` 9.0.2 you installed is the static **GPL** full_build (only `ffmpeg/ffplay/ffprobe.exe`, no `include/`, `lib/` or DLLs), so it can't be linked. Needed: BtbN `ffmpeg-n8.x-latest-win64-lgpl-shared-8.x.zip` (what FM2 names) extracted so `thirdparty/ffmpeg/{include,lib,bin}` exist. Only audio (`src/audio`) needs it.

(Session 6 next steps, now done: FFmpeg LGPL shared build; Phase 3 runtime port.)

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
- **Our project, ReGPU** (`57143cb`), session 8, in this order, applied and verified by `tools/setup.ps1`; details in `patches/README.md`. All game-neutral, each behind a `REGPU_*` switch:
  1. `regpu-fixed16-fetch`: resolved 16-bit fixed images read with the fetch's number format when it differs from the copy's (not the border colour).
  2. `regpu-zpd-tile-spans`: occlusion queries count only inside each tile's ZPD on/off pair (sun glare). Also: `REGPU_TRACE_ZPD` logs `RB_SAMPLE_COUNT_CTL`.
  3. `regpu-resolve-into-image`: a resolve with a row offset into an existing image of the same width is part of that image, not a tile (road bands).
- **Our project, host rules** (`src/gpu_host.cpp`, not patches): `REGPU_NO_FOREIGN_MSAA=1` default (bar); 32×512 colour resolves written back `Now` (road strips); vblank 60 Hz (Q24).
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

**Phase 1 — Project skeleton ✅ (session 5; licence: MIT, session 10)**
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

**Phase 3 — Boot to first frame ✅ (session 7)**
- ✅ Runtime ported from FM4 (`src/`, NOTICE.md); `indirect.cpp`, `PPCMsrChanged`, `PPCMmioStore32` come with it; `PPCLookupIndirect` is fatal on a miss.
- ✅ Fiber overrides with FM3 `hooks/addresses.h`; mftb patch (`tools/patch_mftb.py`, 25 reads in 11 files); vblank 60 Hz (Q24).
- ✅ Goal: ESRB/logos and the intro video render, ~60 fps.
- ☐ Carried to Phase 4/5: `sub_82A918F0` blrl override and SEH policy (not hit yet); `cache:` utility drive (Q8: FM4's VFS may already mount it; check at race start); game-speed check against hl360 (timing of the intro/video).

**Phase 4 — Menus & content ▶ (partly done)**
- ✅ Intro, controls/loading screen, intro race to the finish, pause menu, photo mode reachable (session 8, you).
- ☐ Disc 2 DLC (Q11–Q14), licence mask all bits (Q12), profile XUID/gamertag rules (Q15), language+country pairs (Q16); career and menus beyond the intro race.

**Phase 5 — Race & GPU parity ▶ (started, session 8)**
- ✅ Road: strip write-back at load (Q18 part) and road-image half placement (ReGPU patch). ✅ Car shadow correct (Q19: no square, unlike hl360). ✅ Sun glare (occlusion under tiling). ✅ Screen bar (foreign MSAA).
- ✅ (session 9) Showroom/garage reflections (k_2_10_10_10 decode), My Cars pictures (512×256/256×128 write-back), race-load thread crash.
- ◐ (sessions 9–10) Vinyls: editor B (timing-dependent cut) and race livery B2 open: see "Start here".
- ☐ Photo mode whitened top-third band (OPEN-ISSUES D).
- ☐ Car thumbnails (Q18 rest), audio queue depth (Q26), XMA path check.
- ☐ ReGPU-side dump hook in the **same index.txt/tex_*.bin format**, then `dumpdecode.py diff` hl360-grid vs ReGPU-grid (would speed up finding the next parity bugs).
- ☐ Post-race finish menu at exactly 30.0 fps? Full-lap fps measurement.

**Phase 6 — Polish ☐** Frame-rate unlock only after confirming FM3's sim timing (Q24), launcher/settings, mods (Q9 re-signing, PF+3).

## Session log
### 2026-11-08 — session 11 (B2 fixed; B is not timing)
- **B speed test:** temporary switch `REGPU_PACKET_SPIN_US` (spin per type-3 packet, live via `env`) slowed normal frames to traced speed (a traced frame: 22,651 packets in ~1.6 s ≈ 70 µs each). At 80–100 µs (1–2 fps) the editor edits went through (layer X 7.00 → −0.50, held stick 4 s) and the vinyl stayed cut (green top 262). Short nudges with `Fm3 "capture"` also stayed cut, including one whose traced frame was a rebuild (1317–1333 draws vs 1229–1232 idle): the session-10 "traced = complete" result doesn't hold in a clean build. Switch removed afterwards (result recorded here). New script `tools/analysis/greentop.py`.
- **B2:** (1) `FM3_WRITEBACK 1024x1024` set at the pre-race screen: race noise gone but no vinyls/badges; photo mode/countdown still showed other cars' decals + noise (they use a separate close-up livery). (2) Traced a race restart: **no livery build at race load**; the 1024×1024 resolves at `01005000`/`01805000`/`01C05000` (colour base `100`) are something else (race start, photo mode). (3) Trace from boot (temporary `play.cmd` lines): the livery is built at **boot ≈22.6 s**: 2048×512 strips to `0B508000`/`0B908000`/`0BD08000`/`0C108000` (declared heights 2048/1536/1024/512, so the hook sees them as 2048×2048, 2048×1536, …: my first `2048x512` filter matched only the last strip), side-view passes, regions (1288×600, 1416×544, 1416×568, …) into the texture, then the strips again with colour base `0C0`; at ≈28.9 s the 1024×1024 livery (`04E12000`) the same way. (4) `FM3_WRITEBACK=2048x2048,1024x1024` + new ReGPU code that writes back an image again when a region is drawn into it: **no noise, no wrong decals in race, countdown and photo mode** (you confirmed); the car shows the editor's cut. Cost with `Now`: 13 × ~55 ms for the 2048 image. (5) Final: rule in `src/gpu_host.cpp` (2048×2048 and 1024×1024, 8888, top 0 → `Deferred`) and the ReGPU part queues a Deferred write-back of just the region's rectangle (patch `regpu-enclosing-writeback`, 8th ReGPU patch, `setup.ps1 -NoBuild`: all 8 applied). You confirmed no visual change vs (4); log: `livery writeback:` lines at boot, no stalls.
- Livery rule also matches the three non-livery 1024×1024 images above (written back Deferred, harmless so far).

### 2026-11-08 — session 10 (repo audit, licence, hl360 launcher, vinyl issues B/B2)
- Read-only audit for going public: no game files in the repository, its history (28 commits at the time) or on GitHub (private, one branch); `.gitignore` covers discs, `game/`, `dumps/`, `out/`, `build/`, `reference/`. Licence: every related project is MIT (FM2/FM4 ports, ReGPU, XenonRecomp); added `LICENSE` (MIT, braaap-stututu); `NOTICE.md` keeps the FM4/FM2 credit.
- `boot hl360.cmd`: starts the hl360 baseline from its folder (its `fm3.user.toml`, Baseline profile), refuses if an `fm3.exe` already runs.
- hl360 has neither vinyl issue. Its race dump gave the correct race livery (DXT5 1024×1024). RenderDoc captures of hl360's editor (`out/rdc/`, scripts in `tools/analysis/`) gave its side-view pass and stencil for comparison.
- ReGPU patch `regpu-capture-vertex-floats`: traced frames log small vertex buffers in full (7th ReGPU patch).
- B investigation, chronological (several early readings were wrong and are marked):
  **B: vinyls cut along a straight line in the editor.** State: vinyl regions now reach the car texture (patch `regpu-resolve-into-enclosing`). The cut is earlier, in the game's **704×267 side-view vinyl pass** (surface pitch 720, 1×, EDRAM colour base 0, depth/stencil base `099`; resolved twice per edit with rect 0,0-704,272, declared height 267): rows ≥ ≈181 of that pass come out empty. The side view is stored upside down, so the car's **upper** body is what's missing.
  1. While you move a layer: `Fm3 "capture"` (twice; not every frame rebuilds). In the log, find the resolves with `rect 0,0-704,272` and the draws before them (stencil setup on `98d34caab368b2cc`, mask `5d606c137f9a8ddc`/`cb430dd1cfd2f804` with a k_8 1024×1024 mask, vinyl layers `7061152fe7f7fc72`/`a0d8cbb7f2f8ce9a`, n 141).
  2. Look at the pass's resolved image in `out/run/out/res` (`<addr>_704x267_v0.png`, check the file time is this run's) and find the cut row exactly.
  3. Leads, in order: (a) where ReGPU puts that pass's **depth/stencil** on the host (EDRAM depth base 0x99 = tile 153, i.e. right after 17 tile rows of a 720-wide colour surface): does something else alias those host rows, or is the host depth surface shorter than the pass? Check `C().depth` sizing/offset for base 099 and `REGPU_TRACE_TILE_PHASE`; (b) the **stencil reset** draws #72/#74 go through a 4× view (surface `05820168`, pitch 360) and are scaled by `REGPU_NO_DEPTH_VIEW_SCALE`'s path — turning that off moved the line a little, so the reset area is involved: log the reset rectangles' real vertex values (the log prints only 8 dwords; dump the whole buffer in the capture instead of `peek` later, the ring is reused); (c) compare with **hl360** on the same screen (`reference/.../hl360-forza3`, it has a vinyl editor too) to see whether its side view is whole.
  4. Already ruled out (don't repeat): the mask's content (a big vinyl stops at a straight line), `REGPU_NO_FOREIGN_MSAA` 0/1, window offset (0), `REGPU_NO_STENCIL`. (The "pass everywhere" mask test I did was invalid: that shader keeps alpha == 0, so `float4(1,1,1,1)` blocked everything.)

  **hl360 reference (2026-11-08):** hl360 has **neither** vinyl issue (same car and track; you painted every layer tab a different colour, covering all it can). Its race frame dump (`dumps/hl360/frame_0001`, decoded PNGs in `out/logs/hl360_race_png`) shows the race livery as a **DXT5 1024×1024** texture (`15349000` in that run; find it by its solid vinyl colours, e.g. `tools/dumpdecode.py decode` then rank by saturation) with every panel fully covered. Its right-side panel (upside down, window hole) has exactly the shape of the editor's k_8 mask, so the mask is right and the editor cut (B) is ours.
  In the **editor**, an hl360 dump (`dumps/hl360/frame_0002`) shows the car's 8888 2048×2048 composite as **all zero in guest memory**, like ours: hl360 keeps it (and the side-view pass) on its GPU too, so memory dumps can't compare B. For B, compare with a **RenderDoc** capture of hl360's editor (RenderDoc 1.47 installed) while a layer moves: its 704×267 side-view pass draw by draw against ours.
  **hl360 under RenderDoc (2026-11-08, `out/rdc/hl360_editor_frame*.rdc`, 5 captures in the vinyl editor; #3 = frame 2494 mid-move):** hl360's side-view pass found by its EDRAM names (`RT @ 0t, <9t>` colour, `RT @ 153t, <9t>` depth/stencil; Xenia converts EDRAM between the 1× and 4× views with full-surface transfer draws). Events map 1:1 to ours: 704/720 = #71, 730/746 (depth @0t as 4×) = #72, 756–810 (depth @153t 1×→4×→1×) = #73/#74, **828 = mask (#75)**, **861/867 = vinyl layers (#76/#77)**. Stencil per 30-row band (rows 0–299): **0 everywhere already before the resets** (events 756–799), still 0 after them (810), 1 inside the mask over the **full 0–269 rows** after 828, changed only under the vinyl after 867. So in hl360 the stencil is clean before this pass (likely the previous pass's resolve clear); in ours rows ≥≈181 keep old non-zero stencil and block the vinyls. **Next (needs a ReGPU logging change + rebuild):** log the full vertex data of small rect-list draws (#71–#74 rectangles; today only 8 dwords) and the clear rectangle each resolve applies to depth/stencil (the side-view pass resolves twice with `control 00100300`), then find which one leaves rows ≥181 uncleared. Scripts: `tools/analysis/rd_list.py` (every draw: viewport, scissor, targets) and `tools/analysis/rd_save.py` (save targets incl. depth/stencil as PNG), run with `qrenderdoc.exe --python <script>` and `RD_CAPTURE`/`RD_OUT`/`RD_OUTDIR`/`RD_EVENTS` env vars. Launch: `renderdoccmd capture -d <hl360 dir> -c out\rdc\<name> <hl360 dir>\fm3.exe`, F12 in game to capture.
  **Reset rectangles are fine** (the capture log's `rect corners` lines, which I had missed): 4× view #72/#74 = (−0.5,−0.5)–(319.5,127.5) → 640×256 in 1×; 1× #71/#73 = (639.5,−0.5)–(699.5,266.5), plus a second rectangle the log doesn't print. Full coverage either way, which is why `REGPU_NO_DEPTH_VIEW_SCALE` barely moved the line. hl360's post-mask stencil (`out/rdc/e2494/e828_stencil_vis.png`) is the car-side silhouette (mask texels from (262,270), our draw #75 gets the same UVs 0.256/0.264), rows 0–≈250, window hole ≈170–245. **Our cut is a straight full-width line at ≈181**: not a mask shape, so suspect EDRAM/surface bookkeeping (depth @153t = 272 rows below the colour; #72 resets a 4× depth at base 0, over the colour area). **Next: live switch sweep in our build** (big green vinyl, nudge between flips): `REGPU_NO_STENCIL` (redo properly; earlier attempt likely without a redraw), `REGPU_NO_TILE_MERGE`, `REGPU_NO_TILE_MERGE_LATER_CLEAR`, `REGPU_NO_CLEAR_EXTEND`, `REGPU_NO_ALIASED_DEPTH_CLEAR`, `REGPU_SURFACE_BY_BASE`, `REGPU_RECT_BACK_STENCIL`.
  **(Superseded below: the "big passes lose rows below 256" reading was wrong.)** **Session 10 correction (B): the side view is fine and the stencil isn't involved.** Fresh capture: the 704×267 side view is the full silhouette with all vinyls (yesterday's "cut at 181" on a circle was the window hole). Live sweep with nudges via the virtual stick (`Fm3 "stick L 0.6 0 120"`), green top edge measured: `REGPU_NO_STENCIL`, `NO_TILE_MERGE`, `NO_CLEAR_EXTEND`, `NO_ALIASED_DEPTH_CLEAR`, `SURFACE_BY_BASE`, `RECT_BACK_STENCIL`, `NO_TALL_VIEW`, `NO_MSAA_SCALE`, `NO_FOREIGN_TILE`, `NO_RECT_SNAP`: no change; `NO_TILE_MERGE_LATER_CLEAR`, `NO_BIG_PASS`, `NO_TALL_PASS`: vinyl gone entirely. **The loss is in the big passes (EDRAM surfaces wider than 1280): everything below their first 256 rows is missing.** The right-side region compose (draw `ps a9a8e665892877de`, pitch 1440, rect 1416×568, host scissor 0–410 rows at scale 0.723) has green only to row 252 of the region. The whole car texture shows it too (your observation, vs hl360's race livery in the same atlas layout): per 128-row band (1024 scale), ours 81.5 / **5.7** / 51.2 / **14.9** / 14.7 / 11.4 / 11.1 / 20.0 % non-black, hl360 70–91 % everywhere: in each 512-row strip the lower 256 rows are (nearly) empty, so seats, dashboard and front/rear/roof panels are missing too, not only vinyls. **Next:** a ReGPU change logs small vertex buffers in full in captures (`vf%u floats:`, ≤ 64 dwords; built, not yet committed); capture a vinyl edit and read the region/strip draws' quad corners: do they reach row 568/512 (ReGPU loses rows: big-pass scaling, 256-row tile handling) or stop at 256 (the game splits the pass into tiles and ReGPU drops the later ones)?
  **Session 10, later (B): it's a timing difference, not geometry.** Full vertex logging (ReGPU patch `regpu-capture-vertex-floats`) shows every region drawn as one full quad, e.g. (0,0)–(1416,568) with UVs covering the whole side view and atlas rows 512–1080, so the game splits nothing. The decisive observation: **vinyl edits made during traced (captured) frames produce a complete car texture and a complete car on screen** (green over the whole side up to the windows; four captures in a row, `out/logs/s10_multi/`), and **one edit in normal frames brings the cut back** (green top edge 129–158 → 261–271, green px 7109 → 2564). The game does the same work either way: same 8 strips, region updates and side-view resolves per edit frame at normal speed (`REGPU_TRACE_RESOLVES`, `out/logs/s10_normal_resolves.txt`). The editor's selection flash (yellow) also covers the full side. Ruled out with live A/B (nudges via `Fm3 "stick L 0.5 0 200"`, green edge measured with a scratch script like `greentop.py`): stencil, mask, side view, resets, region geometry, big/tall-pass rules, tile merge, draw workers (off by default anyway), stale-resolve check, and the per-frame texture cache (a drop-on-resolve fix and a cache-off switch were tried, changed nothing, and were removed). `REGPU_NO_TEXTURE_PREHASH` changes the result (less green) but doesn't fix it. **What traced frames change besides the texture cache is speed** (every packet and draw logged synchronously): so look for a race between the CPU and ReGPU's GPU work: something the game reads back or waits on (a fence/event/query, a CPU read of a resolved image, the texture pre-hash workers' timing), or a resolve whose host copy is read before the GPU has written it. Next ideas: slow normal frames down without tracing (e.g. a frame cap or a sleep switch) to see whether speed alone fixes it; log fences/queries the editor waits on; compare the order of CP packets around the region resolves at normal speed vs traced.
  **→ B2 shortcut to try first:** the race livery is the composite downscaled to 1024×1024 and compressed; our race-load trace has **four 1024×1024 resolves** (`rect 0,0-1024,1024`, pitch 1024): whole-image copies the existing write-back can do. Test live before writing new ReGPU code: in our build, `Fm3 "env FM3_WRITEBACK 1024x1024"`, then load a race and look at the car (session 9 only tried the 2048 sizes). If the car is right but without vinyls, the 1024 downscale reads the in-memory 2048 texture that lacks the region copies: then the region write-back below is needed too.

  **B2: a car with vinyls is noise in races.** At race load the game resolves the vinyl regions (e.g. `rect 0,0-1416,568`, pitch 2048, declared height 1536) **into the car's 8888 2048×2048 texture in memory** (no resolved image there), then its CPU code compresses that to DXT for the race. ReGPU writes nothing to memory; the `FM3_WRITEBACK` experiment can't help because `WriteResolveToGuest` writes a whole image at its own width, not a rectangle at a tile (x, y) of a 2048-pitch tiled image.
  1. Plan: a ReGPU write-back of a **resolve rectangle into a tiled destination at its place**: from the destination address compute (x, y) as `ResolveIntoEnclosing` does (tile = 32×32 × bpp bytes, row-major tiles of pitch/32), read the rectangle back from the GPU, and write it with the Xenos 2D tiling (`TextureTiledOffset` in `texture.cpp`) at (x + i, y + j) with the image's pitch. Then a game rule in `src/gpu_host.cpp` asks for it (`Writeback::Mode::Now`) for colour resolves whose pitch is 2048 and whose declared height + y = 2048 (car textures); check the race-load resolve list with `REGPU_TRACE_RESOLVES` (session 9 trace: `out/logs/s9_raceload_resolves.txt`).
  2. Also the 1024×1024 resolves at race load (four, pitch 1024): find out what they are (another livery level? the body texture?) before deciding whether they need writing back too.
  3. Verify: the car's race texture (find the DXT bound on the car's draws in a capture) and the car on the grid; FM2's port fixed the same thing by writing its race livery back.

### 2026-11-07 — session 9 (timer overlay, exploring past the intro race)
- Added the on-screen timer you asked for: ReGPU patch `regpu-timer-overlay.patch` (4th ReGPU patch; `setup.ps1` verifies it). A 136×34 white-on-black GDI panel copied onto the back buffer by the presenter after the game's frame (and during the shader-compile hold); text = `LogSeconds()`, which both logs now export. The game's own `src/log.cpp` is the one linked (it defines the same symbols as ReGPU's), so the timer and `fm3.log` share one clock. The shader-compile panel moves below it. You confirmed it works.
- You explored career menus, the upgrade shop, paint booth, vinyl apply/editor, My Cars, a solo race and photo mode, with 12 timed screenshots → issue list A–G in the status section.
- Two read-only searches of the FM2 and FM4 ports (both repos are one squashed commit, so only code comments/docs count as evidence). FM2 has its own renderer (not ReGPU) with `FM2_*` switches; FM4 vendors a ReGPU fork that is not a superset of upstream (~90 switches each side only). Results per issue are in the table.
- Reported first as you asked; you picked issue A.
- **Issue A fixed.** Reading `GetCubeTexture` first turned up a real but minor bug: for cubes read from memory, level 0 of face i was written at `offset + faceBytes·i` but copied from the running offset (all faces = face 0, or other levels' bytes). Fixed behind `REGPU_NO_CUBE_FACE_STAGING`; A/B screenshots looked the same, so not the cause. The showroom capture (`out/logs/s9_showroom_capture.txt`) then showed two cubes bound on the car draws, both format `?7` = **k_2_10_10_10**, neither resolved: `19781000` (mipped, real data in memory: `C1E0781E` = grey 30/1023 per channel, fetch exp adjust +3) and `1832E000` (no mips, all zero in memory, no resolve into it: unused or live-only slot; left alone). ReGPU's `Info()` had no entry for format 7, so `DecodeTexture` failed and the faces were zero-filled. Added the format (8 bits of 10 kept). Measured A/B at almost the same angle: near-black pixels on the car 116,945 → 65,802, paint mean R 112.8 → 124.9, highlights 9.0 → 11.7%; mirror, xD badge and exhaust tip back. You confirmed shiny paint and chrome in the dealer (R8) and the chrome rim list.
- **Issue C fixed.** Entering My Cars renders nothing (resolve trace `REGPU_TRACE_RESOLVES`, live): the pictures are stored with the car, made when it changes. Added a live experiment to the write-back hook, `FM3_WRITEBACK="WxH,..."` (set with `Fm3 "env FM3_WRITEBACK 512x256"`, re-read on change; kept as a tool). Repaints: (1) 256x256+512x512 written back → thumbnail noise, big picture now **empty** (its buffer `120DC000`, 512×256 8888, all zero); (2) traced repaint: once per save, `512x256` resolve to `14C5F000` and `256x128` to `1205F000` (8888, swap bit set), plus 5×256x256 icon passes; (3) write-back of 512x256+256x128 → both pictures correct, colours right. Permanent rule (8888, top 0, those two sizes, `Now`) built; a 4th repaint without the experiment: fixed, log `picture writeback: 141A6000 512x256`, `1206B000 256x128`. FM2's port had the same bug (its sizes 256x128 + 1024x1024).
- **Issue B (vinyls), partly fixed.** Resolve trace while editing: the car's 2048×2048 8888 texture is rebuilt in four 2048×512 strips (dest base +0/512/1024/1536 rows, each declaring the rows left) and, per edit, one rectangle per changed region at a tile (x, y) inside it (e.g. 1416×568 at 512,512: `0BAC9000` = tile 1040 of a 64-tile-wide 32×32 tiling). ReGPU made each an image of its own; the car read only strip 1. First try (strips only, GPU copy) was too narrow (regions are partial and at another host scale); then the enclosing match picked a strip's own image instead of the base (fixed: outermost wins). Now vinyls reach the car. **Still open:** a straight cut along the car: the game's 704×267 side-view vinyl pass (stencil-masked: a 1024×1024 k_8 mask sampled at the pixel position, alpha == 0 passes) already loses its rows ≥≈181. Ruled out: the mask (a big vinyl stops at a straight line), colour foreign-MSAA (`REGPU_NO_FOREIGN_MSAA` 0/1 same), window offset (0), stencil (`REGPU_NO_STENCIL` no change), 4× stencil-reset scale (`REGPU_NO_DEPTH_VIEW_SCALE` moves the line a little). Next: that pass's host depth/stencil placement (EDRAM depth base 099, pitch 720), and hl360 on the same screen.
- **Race-load crash (new path once the car had vinyls), fixed.** A thread at `822974B0` is created at race load; its `ThreadObject` was kept alive only by the title's handle, so a handle closed before the host thread first ran freed it: the thread read a freed `GuestThread` (garbage id, TLS -1) and crashed in `KeTlsGetValue`. `src/kernel/thread.cpp`: the host thread now gets its own strong reference from `StartThread`. The crash handler had hidden it (it faulted on a stale `ctx` before writing anything): `src/crash.cpp` now flushes early and reads guest registers under SEH.
- **Race livery noise (B2)** found after that: see the issue table; not caused by the session-9 changes.
- Possible follow-up: if reflections show banding, keep all 10 bits with a native `R10G10B10A2_UNORM` upload (the 2D path already uploads BC/half-float textures natively; the cube path always decodes to 8-bit).
### 2026-11-07 — session 8f (smooth bands fixed)
- You parked on the start grid with the chase cam rotated (bands visible) and paused. Two 16×16 boxes measured (smooth band at (875,635), grainy at (990,620)) across `ps` outputs: the slot-0 sample alone already differs (detail 9.5 vs 23.4), while its mip level and derivatives are identical (≈ level 1.4). So the **road image's content** was banded, and my earlier "t0 looks uniform" by eye was wrong.
- Capture: road image `05A1B000` level 0 had a **sharp top half and a blurred bottom half**; a phantom `0599B000` 512×768 image (256 rows earlier) held the fresh half. Cause: the half resolve `rect 0,256-512,512 dest 05A1B000` went through ReGPU's tile rule (`base = dest - top·pitch`). The existing `REGPU_RESOLVE_IMAGE_BASE` didn't apply (it only takes partial-width rectangles).
- Fix (ReGPU, `regpu-resolve-into-image.patch`): if an image already exists at the destination with the same width covering the rows, place the rectangle in it. Tiles name their own row, where no image starts, so FM3's scene tiles are unchanged (screen checked: no bar). Verified: you saw the bands gone; the capture shows the road image sharp throughout.
- Why only after rotating the chase cam: before that the game rebuilds the road image whole (correctly placed); turning the camera starts the half-at-a-time rebuilds.
- hl360 comparison (earlier): no bands there. Your console: none. Consistent with a ReGPU-only bug.

### 2026-11-07 — session 8d (smooth road bands)
- You confirmed from your console: the whole road is high-res asphalt; the smooth bands are a bug. Key clue from you: they appear **only in the chase camera after rotating it** (not in hood/bumper cam), and reset when cycling cameras.
- No new strips are rendered while rotating (40 s resolve trace). The composition shader `0d1ce823ed690669` (one full-target axis-aligned rect, prim 8) turns each pixel into rotated strip coordinates (`pc[1]`–`pc[4]`), samples DXT1 strip pairs t1/t2 and t3/t4, blends each pair by `pc[5].x`, and combines them with the base DXT5's luma/alpha (`pc[6]`). The camera angle selects the strips and weights.
- Tried and **reverted** (unproven): measuring the rect-list right angle in screen pixels. The composition rect turned out to be axis-aligned, so it can't be the cause.
- **Dumped all 64 DXT1 strips from guest memory** (`peek` on the physical mirror `A6375000`, 32 KB each, tiled, pitch 128, endian 8in16; decoded with `dumpdecode.decode_texture`, PNGs in `out/strips/`): every strip is **nearly flat** (detail 0.2–0.4 vs ~9 for the road), even/odd strips at mean 72 / 51. They should carry rotated, pre-filtered asphalt. So the CPU-side compression at load produces flat strips: either from wrong input (the 8888 strips as written back) or because the compressor code itself misbehaves in the recompile (VMX128-heavy code is a candidate).
- The 8888 strip memory is reused after load (now holds index lists), so it can only be inspected during the load.
- **Next:** a write watchpoint on a DXT1 strip at load (`FM3_WATCHW`, crash.cpp) to name the compressor function; then check its input and its vector instructions.

### 2026-11-07 — session 8e (smooth bands, continued; not solved)
- **Watchpoint** (`FM3_WATCHW=A6375000`, temporary `play.cmd` line, removed again; it watches all three physical aliases): 3 writes, all at boot: allocator memset (0.3 s), then `sub_8252EE40` copying 0x7F80 bytes from `4656DDFC` to `A6371100` (1.3 s, a resource loaded from the game files). **Nothing writes the strips at track load.** So the four bound DXT1 strips are static textures, and "flat strips = broken compressor" was wrong.
- **Composition constants** with and without bands (you rotated the chase cam and paused): identical. `pc5 = (0,0,0,1)`, `pc6 = (0,0,0,1)`: the strips get weight 0 and the composed road image is just the base texture's luma/alpha. The composition is not where the bands come from.
- **Road shader inputs** shown live one by one with their real coordinates (slot 0 colour and alpha, slot 1 alpha, slot 2 both lookups, slot 7, slot 11 `.x`@`o[3]` and `.y`@`o[4]`): all clean, no bands. The road shader paints the whole road (tint test), yet its output has bands. `REGPU_SKIP_SCREEN_PASSES` changes nothing.
- **Open leads:** (1) the shader's lighting/specular arithmetic (normals from `o[0]`/`o[1]`, `pc[223]`–`pc[255]` terms); compare intermediates numerically between a band and a non-band pixel rather than by eye; (2) whether hl360 shows the same bands in the rotated chase cam (yes = game-logic/CPU side shared by both; no = ReGPU-side); (3) LOD/derivatives: bands look like a lower mip; compare `ddx/ddy` of slot 0's coordinates across a band edge.
- Tooling added in the scratchpad (not in the repo): `crop.py` (crop and enlarge shots), `strips.py` (dump/decode via `peek`), `compose_pc.ps1` (capture a draw's constants).
- **hl360 check (you, rotated chase cam): no bands.** So the bands are **ReGPU-specific** (hl360 and your console are both clean).
- A block-wise detail measurement (final vs composed texture vs shader intermediates `r0`/`r1`/`r4`/`r6`/`r9`/`r10`) pointed at `r9`, but that was confounded by road markings landing in the "normal" blocks; not a finding.
- **Next:** park on plain asphalt (no lines or markers in view), rotate the chase cam until bands show, pause; then read intermediates numerically at a band pixel and a non-band pixel (e.g. `ps` edits that output a scaled single term, compared with `pngstat`), to find the first term that diverges. Also compare the slot-0 fetch's LOD: output `ddx`/`ddy` of its coordinates across a band edge.

### 2026-11-07 — session 8c (road streaks, square shadow)
- You reported the streaks still there after the sun fix. Live shader edits (`ps <hash> <expr>` on the road shader `08a4eb8fbebd07fe`) showed each input separately: slot 0 (`8888 512×512`, a runtime-composed road detail image) carries the streak pattern; slot 11 (shadow) does not.
- The detail image: rebuilt half per frame (`rect 0,0-512,256` / `0,256-512,512`), mip chain resolved after it (256² at +1 MB, then 128², 64², … padded to 32×32 / 4 KB; ReGPU assembles that chain fine). Its fresh half had the streaks. The composition draw (ps `0d1ce823ed690669`) reads `t0 = DXT5 512×512` (base asphalt) and **`t1–t4 = DXT1 32×512`** strips (32 KB apart). Those strips, decoded, were **pieces of car paint**.
- 90 s resolve trace while you drove: no 32-wide resolves and nothing into the strip region, so they're made at load. Strip write-back rule (32×512): 64 strips caught at load (`05B75000`–`05F65000`), but Deferred → no change; Now → the fine dashed lines are gone (your test).
- `REGPU_CAPTURE_ON_RESOLVE=05B75000` (temporary line in `play.cmd`, removed again) captured the load frame: ~49 strip draws alternating two shaders, both reading the base DXT5 with rotated coordinates and min/max tracking (compression preparation); A uses colour·luma, B uses alpha.
- **Square shadow (regression I caused):** the road's shadow map `18708000` is resolved as an unsigned **fraction** (`dest_info 00000C82`), and ReGPU already returns fractions correctly. My patch divided it by 65535 anyway (lit ≈ 0 → square) and converted the white border too. Now the conversion uses both the copy's and the fetch's number format (identity when equal) and skips the border (`gBorderHit` in `TFetchRaw`). You confirmed the square is gone. `patches/regpu-fixed16-fetch.patch` regenerated; `setup.ps1` verifies both ReGPU patches.
- Photo mode shows a whitened band in the top third (you said: ignore for now, not seen in races).

### 2026-11-07 — session 8b (sun white-out)
- You drove to a spot facing the sun and paused, with the debug server on (later runs via `play.cmd`, made at your request; you start the game and report when in position, and I don't repeat the launch steps).
- Switch sweep facing the sun: only `REGPU_FAKE_OCCLUSION` removed the white-out (brightness 152 → 38). fps 39–40, 53 with fake answers, 55 without coverage draws.
- `REGPU_TRACE_ZPD_RESULTS`: one query per frame answering **zpass 6,472,508**. A capture showed it beginning before the 6-vertex sun quad in tile 1 and ending after tile 2's, with 621 tile-2 draws inside. The trace showed 3 plain ZPD events + 1 end per frame, i.e. a count-on/off pair per tile.
- Fix in ReGPU (`RendererOcclusionBegin`: a start while counting = pause, then resume in a new D3D query part; the end sums them; swap finalises a paused query; `REGPU_NO_ZPD_SPANS` A/B). The ZPD trace now prints `RB_SAMPLE_COUNT_CTL` (00000002 for all four events in FM3, so the control register doesn't separate begin from end here; the pairing does) and is no longer capped at 12 lines per run.
- Result: zpass 5,780; normal glare; 60 fps. Saved as `patches/regpu-zpd-tile-spans.patch` (split from the working tree with a scratch index); `setup.ps1` verifies both ReGPU patches.

### 2026-11-07 — session 8 (visual bugs: bar, asphalt, sun)
- You reported: race playable to the finish; input + audio fine; three visual bugs (screen-wide bar, sun white-out + slowdown, asphalt streaks worse in motion).
- `tools/fm3ctl.ps1` (own TCP client for ReGPU's debug server, port 7341). The debug pad's presses reach the game (A skips the intro; START pauses after the countdown, not on "GO!").
- **Bar:** a hard edge at row 512. hl360's grid dump shows FM3 resolves the 2x-MSAA scene in two tiles (`1280×512` + `1280×208`, contiguous), and ReGPU's trace shows it handles the tile replay. On a paused frame I swept ~60 live switches with a row-brightness metric; only `REGPU_NO_FOREIGN_MSAA` removed the band (rows 496–511: 114 → 64). Made it an FM3 default.
- **Asphalt:** swept texture/sampler switches with an edge-energy metric (only `REGPU_NO_FETCH_BOUNDS` helped partly, a red herring). `capture` → pixel shaders bound to the shadow maps → tinting each found the road shader `08a4eb8fbebd07fe`. Its slot 11 = `18708000` (256×256 `k_16_16` VSM). The resolved images themselves were clean (`out/run/out/res`). Fetch constant `8200D802 … 01281690 … 00000219`: clamp to border (white), unsigned, fraction. The resolve had been signed integer with exp bias +10 (`dest_info 000A6C81`). ReGPU's fetch setup never applied the fetch's number format/sign to resolved fixed-16 images → fix in ReGPU (`regpu-fixed16-fetch.patch`; `setup.ps1` now applies ReGPU patch stacks too).
- **My mistake, caught by you:** the first version of the shader helper used a loop writing `v[c]`; fxc can't unroll that (X3500/X3511), so **160 shaders failed to compile** and the scene went black with a broken UI. Rewritten with vector selects: 0 failures. Lesson: after any change to ReGPU's HLSL preamble, boot and grep `failed to compile` before looking at anything else.
- **FM4's vendored ReGPU A/B:** added CMake `FM3_REGPU_DIR` / `FM3_REGPU_FM4` (crash.cpp has an `#if`), but building FM4's copy from `reference/` was **blocked by the permission classifier** (third-party code in the reference clones). Not needed for these two fixes.
- My scripted A presses carried into the race (the car drove off); the scene changes in the first sweep were you rotating the camera, so that sweep was discarded and redone on a paused frame.
- Stray file: ReGPU's `hlsl` debug command cut my path at the first space and wrote `G:\claude`; moved it into `out/ps_road.hlsl`. Use paths without spaces for that command.

### 2026-11-07 — session 7 (FFmpeg + Phase 3: runtime port, first boot)
- **FFmpeg** (you asked me to get the right one): BtbN release `latest`, asset `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` (80,407,192 B). SHA-256 `9cb911fc…04c0` matches both GitHub's asset digest and BtbN's `checksums.sha256`. Downloaded to an empty scratch folder, extracted to `thirdparty/ffmpeg` (gitignored). Version n8.1.3-34-ga35157abb9 (2026-11-07), LGPL v3 (`--enable-version3`, no `--enable-gpl`), with `xma1`/`xma2` decoders; DLLs avcodec-62, avutil-60, swresample-6.
- **Runtime port:** copied FM4's `src/` from its git objects (51 files) and adapted it:
  - fibers re-addressed (`hooks/addresses.h`: 82A7B098 / 82A7B128 / 82A7B178 / 82A7B258 / 82A82210; FM4-identical object layout confirmed in disassembly);
  - single-image `recomp_modules.cpp` (from `ppc_config.h`);
  - `Fm2Timebase` → `GuestTimebase`; `FM2_*`/`FM4_*` env switches → `FM3_*`; log `fm3.log`; user dir `Documents\fm3` (`FM3_USER_DIR`);
  - removed: the test launcher call, the FM2 ring-dump addresses in `crash.cpp`, FM2/FM4 ui.zip patches (behind `FM3_LEGACY_UI_PATCHES`), benchmark mode and the draw-worker profiler (FM4's vendored ReGPU API);
  - `regpu::RendererLogCrashContext` → upstream `regpu::LogCrashContext`.
  - Not copied: boot screen (FM4 art), static shader scan (`.fxobj`), pipeline seed, updater, launcher, `.rc`.
- CMake: mftb patch at configure time (`tools/patch_mftb.py`, own), FFmpeg linkage and DLL copies, `winhttp`.
- **Build:** 4 compile errors (benchmark calls), then 2 link errors (FM4-vendored ReGPU symbols), then `fm3.exe` links (40 MB).
- **First boot** (`out/run`, `FM3_USER_DIR=out/user`, game = hl360's verified `game/disc1`): runs, swaps, intro video. Screenshots `out/shots/boot_{08,20,40,70}s.png`: ESRB, white logo fade, R8 door, LMP cars. 59.6 fps. Audio trace: 188 callbacks/s, 0 dropped. XMA: 0 packets in the intro (FM3's video audio likely isn't XMA; check in menus/race).
- **Credit fix:** the FM4 repo is Aiden-Piercee's (`Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU`); its MIT notice is FM2's (© zer0ducksgiven). I had credited zer0ducksgiven alone in `patches/README.md` and the README; corrected, with `NOTICE.md` holding the licence text.

### 2026-11-07 — session 6b (dependencies)
- You installed FFmpeg via winget and extracted FidelityFX-SDK 2.1.0 to `G:\claude code projects\tools\`.
- FidelityFX: the layout matches (`Kits/FidelityFX/upscalers/fsr3/include/gpu/fsr1/ffx_fsr1.h`, `signedbin/*_dx12.{dll,lib}`; DLLs dated 2025-12-09: loader, upscaler, framegeneration, denoiser, radiancecache). All 5 DLLs: Authenticode **Valid**, CN=Advanced Micro Devices. Junction `thirdparty/fidelityfx` → SDK root (gitignored, no copy). FM2/FM4 used v2.3.0; 2.1.0 compiles fine for ReGPU.
- `cmake -S . -B build/rel` (full game) configures; `ninja regpu` 20/20 (warnings only: `-Wswitch` in AMD's `ffx_api_dx12.h`, `-Wformat` DWORD in ReGPU `renderer.cpp:10629`); `regpu_linkcheck` links.
- FFmpeg: winget's `Gyan.FFmpeg` = `ffmpeg-9.0.2-full_build`, `--enable-gpl --enable-static`, executables only → unusable for linking (and GPL). Left installed, not referenced.

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
