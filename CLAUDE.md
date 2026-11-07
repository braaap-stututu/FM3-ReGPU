# CLAUDE.md — FM3 ReGPU recomp

Static recompilation of **Forza Motorsport 3 Ultimate Collection (USA)** (Xbox 360, title `4D53084D`, media `6F74359D`, `default.xex` v0.0.0.8, SHA-256 `aa15372c…8742`) for Windows x64, built with **XenonRecomp + ReGPU**, the same stack as the FM2 and FM4 ReGPU ports. Two ReXGlue-based FM3 recomps are kept as reference, and one of them (**hl360-forza3**) is built locally as the **comparison baseline** for frame-dump diffs.

## Start every session here
1. Read **[NOTES.md](NOTES.md)**: the top section is current status / next step, and *Work plan (phases)* is the roadmap. **Phase 1 (skeleton) is mostly done**: git repo, pinned submodules, patched XenonRecomp + host tools build. Next is Phase 2 (first recompilation).
2. Game-specific knowledge (addresses, symptoms, fixes, and how each maps to XenonRecomp/ReGPU) is in **[FM3-QUIRKS.md](FM3-QUIRKS.md)**. Verbatim address lists are in `reference/catalogs/`.
3. **Update NOTES.md at the end of every work session without being asked**: status/next step at the top, config decisions, every patch applied and why, known-broken areas, and a session-log entry.

## Layout
| Path | What |
|---|---|
| `NOTES.md` | Running log + phased work plan (source of truth for state) |
| `CMakeLists.txt` | Game build (FM4-style ReGPU linkage); FATALs until `out/ppc` + `thirdparty/fidelityfx` exist |
| `tools/setup.ps1` | Idempotent: submodules, the 5 XenonRecomp patches (`patches/`), host tools → `build\tools\` (XenonRecomp, XenonAnalyse, xexdump) |
| `tools/vcenv.cmd` | `tools\vcenv.cmd <cmd>` = vcvars64 + LLVM on PATH |
| `src/` | Runtime (Phase 3; planned layout in `src/README.md`); `gpu_host.cpp` written |
| `thirdparty/{XenonRecomp,ReGPU}` | Our submodules, pinned (commits in NOTES.md) |
| `FM3-QUIRKS.md` | 26 FM3 quirks: address, symptom, hl360 fix, forza3-recomp fix, XR+ReGPU equivalent |
| `tools/fm3_route.ps1` | Drives the hl360 baseline: fresh-profile boot → intro-race start grid → pause → screenshots + dumps |
| `tools/dumpdecode.py` | `list` / `decode` / `diff` of patch-0007 frame dumps (pure Python, no numpy) |
| `reference/rexglue recomp reference/hl360-forza3` | **Baseline** (ReXGlue SDK v0.10.0+2, patches 0001–0006 + **our 0007 dump hook**). Built; game data extracted in `game/`, DLC in `user/` |
| `reference/rexglue recomp reference/forza3-recomp` | Second ReXGlue FM3 port (reference only) |
| `reference/framework/{XenonRecomp,ReGPU}` | Target-stack upstreams (reference clones) |
| `reference/sibling-recomps/{Forza-Motorsport-2-ReGPU,Fuzzy-Rittersport-4-ReGPU}` | Templates: FM2 = `src/` layout/kernel/audio/hooks; FM4 = ReGPU linkage, fibers, indirect table, XR patches |
| `reference/catalogs/` | Detailed catalogues the quirks file was built from |
| `dumps/` | Captures (game-derived, ~0.5 GB): `hl360/runA`, `hl360/runB` = start-grid reference captures |
| `*.iso` (root) | Your Disc 1/2 dumps (MD5-verified). Never copy them anywhere public |

## Baseline (hl360) quick reference
- Run: `reference\rexglue recomp reference\hl360-forza3\out\build\win-amd64-release\fm3.exe` (reads `fm3.user.toml` beside it; logs in `logs\`). Settings: 1280×720 windowed, 1× scale, D3D12, `readback_resolve=fast`, `frame_stats=true`, `texture_dump_dir=<project>\dumps\hl360`.
- Rebuild after SDK changes (from the hl360 folder, the game must **not** be running because the build copies DLLs):
  `cmd /c scripts\build_sdk.cmd` then `cmd /c scripts\build.cmd release` (SDK incremental ≈ 1 min, game ≈ 1.5 min).
- Scripted capture (PowerShell; state doesn't persist between tool calls, so do it in one call):
  ```powershell
  . "G:\claude code projects\FM3-ReGPU recomp\tools\fm3_route.ps1"; Invoke-Fm3Route -RunLabel myrun
  ```
  Manual capture: create `dumps\hl360\TRIGGER` while the game runs.
- Inspect: `python -I tools/dumpdecode.py list|decode|diff …`
- Known baseline behaviour: 60 fps in race (the pause menu too); **post-race finish menu is a locked 30.0 fps** (probably game-native); the intro has an uneven short/long frame rhythm at a 60 average.

## Toolchain (installed)
VS 2022 Build Tools 17.14 (MSVC 14.44, bundled CMake 3.31.6 + Ninja 1.12.1), Windows SDK 10.0.26100, **LLVM 23.1.5** at `C:\Program Files\LLVM` (not on PATH; scripts call `vcvars64` + prepend LLVM, like hl360's `scripts\devenv.cmd`), Python 3.15 (no numpy), Git 2.56 (`sh` in `C:\Program Files\Git\usr\bin`, not on PATH), RenderDoc 1.47. Host GPU: RX 7900 XTX.
Not yet installed (needed from Phase 1/5; **ask before installing**): FFmpeg win64 LGPL shared, AMD FidelityFX SDK, optional numpy.

## Gotchas learned the hard way
- **Line endings:** the global git `core.autocrlf=true` turns patch files into CRLF, so `git apply` fails. Use `.gitattributes` with `*.patch -text` (keep `.cmd/.bat/.ps1` CRLF). hl360 + its SDK already have repo-local fixes. The reference clones' working trees are CRLF too: take patch files from their git objects (`git show HEAD:path`), not by copying.
- **Long paths:** some submodules (vulkan-loader) need `core.longpaths=true` (set repo-locally).
- **Guest addresses move between runs**: diff dumps by content hash / (size, format, first-use order), never by address.
- **Timed key presses drift**: Return both skips cutscenes and toggles pause. Drive the game state-aware (`Test-Paused` checks the RESUME-bar shape twice, then re-confirms).
- **CLI cvars outrank toml** in the ReXGlue SDK (`--profile_xuid=…` overrides `fm3.user.toml`). The route uses profile `E0000BA5E11E0002` and deletes only that save; your `Baseline` save `E0000BA5E11E0001` must be left alone.
- **Don't execute third-party repo code** (e.g. `forza3-recomp/tools/*.py`): the permission classifier blocks it. Read it for ideas, write our own.
- Never link FM2/FM4 `sub_XXXXXXXX` addresses into FM3. Every hook must be re-addressed for this xex.
- `reference/` and `dumps/` hold third-party code and game-derived data: keep them out of any repository we create.
