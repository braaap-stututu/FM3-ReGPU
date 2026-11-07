# FM3 ReGPU recomp

Static recompilation of **Forza Motorsport 3 Ultimate Collection (USA)** (Xbox 360, title `4D53084D`,
`default.xex` v0.0.0.8, SHA-256 `aa15372c13bc4c899b5ef90a1cfb5634d389b0a3620cc9715f6023d61a9b8742`)
for Windows x64, on [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) +
[ReGPU](https://github.com/zer0ducksgiven/ReGPU). Layout and build follow the FM2 and FM4 ReGPU ports.

(i tried to make this a fork of the FM2 ReGPU project but github wouldn't let me)

**Status:** Phase 3 goal reached. `fm3.exe` boots: the ESRB / logo screens and the intro video
render at about 60 fps on ReGPU (D3D12), and audio frames flow. No input or menus checked yet (Phase 4).
Run: `build/rel/fm3.exe <folder with the Disc 1 files>`. Running log and roadmap: [NOTES.md](NOTES.md). Game quirks: [FM3-QUIRKS.md](FM3-QUIRKS.md).

No game data is in this repository. You need your own disc dump.

## Layout
| Path | What |
|---|---|
| `CMakeLists.txt` | The game build (needs `out/ppc` from the recompiler, and `thirdparty/fidelityfx`) |
| `config/` | XenonRecomp configs for `default.xex` (Phase 2) |
| `patches/` | Patches to XenonRecomp, applied by `tools/setup.ps1` ([patches/README.md](patches/README.md)) |
| `src/` | Runtime: kernel/XAM, hooks, audio, input, ReGPU host glue ([src/README.md](src/README.md)) |
| `tools/setup.ps1` | Submodules + patches + host tools, idempotent |
| `tools/vcenv.cmd` | Runs a command with VS 2022 Build Tools x64 + LLVM on PATH |
| `tools/hosttools/` | CMake project: patched XenonRecomp, XenonAnalyse, `xexdump`, `ppcdis` |
| `tools/recomp.ps1` | default.xex → `out/ppc` + checks (`fm3_config.py`, `fm3_check.py`, `genstubs.py`) |
| `tools/fm3_route.ps1`, `tools/dumpdecode.py` | Baseline capture route and frame-dump decoder/differ |
| `thirdparty/XenonRecomp`, `thirdparty/ReGPU` | Submodules (pinned; see NOTES.md) |

## Requirements
VS 2022 Build Tools (MSVC 14.44, CMake, Ninja), Windows SDK 10.0.26100, LLVM/clang-cl ≥ 18 at
`C:\Program Files\LLVM`, Python ≥ 3.11, Git. Later phases: AMD FidelityFX SDK v2.x in
`thirdparty/fidelityfx`, an FFmpeg win64 LGPL shared build in `thirdparty/ffmpeg`.

## Build (current state)
```powershell
& .\tools\setup.ps1                            # submodules, XenonRecomp patches, build\tools\*
& .\tools\recomp.ps1 -Xex <your default.xex>    # -> out\ppc, out\check.txt
tools\vcenv.cmd cmake -S . -B build/ppc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DFM3_PPC_ONLY=ON
tools\vcenv.cmd ninja -C build/ppc fm3_ppc     # compile check of the recompiled code
```
Full game: `tools/vcenv.cmd cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link` then `ninja -C build/rel fm3` (needs `thirdparty/fidelityfx` and `thirdparty/ffmpeg`).

## Licences
Our code: to be decided. XenonRecomp (MIT, hedge-dev and contributors) and ReGPU (MIT) are
submodules. Most of `src/` and the XenonRecomp patches in `patches/` come from the MIT-licensed FM4 port
(Aiden-Piercee, building on zer0ducksgiven's FM2 port): see [NOTICE.md](NOTICE.md).
