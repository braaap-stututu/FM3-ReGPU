# FM3 ReGPU recomp

Static recompilation of **Forza Motorsport 3 Ultimate Collection (USA)** (Xbox 360, title `4D53084D`,
`default.xex` v0.0.0.8, SHA-256 `aa15372c13bc4c899b5ef90a1cfb5634d389b0a3620cc9715f6023d61a9b8742`)
for Windows x64, on [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) +
[ReGPU](https://github.com/zer0ducksgiven/ReGPU) (Direct3D 12). Layout and build follow the FM2 and FM4 ReGPU ports.

(i tried to make this a fork of the FM2 ReGPU project but github wouldn't let me)

No game data is in this repository. You need your own disc dump.

## Status

**"Playable."** The game boots, plays the intro, and the prologue race can be driven to the finish at
about 60 fps (i have not yet tested beyond this point). Controller, keyboard and audio work (as far as i can tell). Running log and roadmap: [NOTES.md](NOTES.md);
game-specific quirks: [FM3-QUIRKS.md](FM3-QUIRKS.md).

Fixed so far (all confirmed in game):

| Issue | Cause | Fix |
|---|---|---|
| Washed-out bar across the 3D view | ReGPU's "foreign MSAA" path on FM3's two-tile 2×MSAA scene | off by default (`src/gpu_host.cpp`) |
| Screen turns white facing the sun, with a slowdown | the sun's occlusion query counted a whole tile's worth of draws | ReGPU patch `regpu-zpd-tile-spans` |
| Fine dashed streaks on the asphalt | road strips rendered at track load never reached memory | 32×512 resolves written back at once (`src/gpu_host.cpp`) |
| Smooth bands on the asphalt in a turned chase camera | half of the road detail texture was resolved 256 rows too early | ReGPU patch `regpu-resolve-into-image` |
| Square shadow around the car (as in Xenia-based ports) | 16-bit fixed shadow maps read with the wrong number format | ReGPU patch `regpu-fixed16-fetch` |

Known issues and not yet checked:
- Photo mode shows a whitened band over the top third of the image (not seen in races).
- Disc 2 DLC, profiles, most tracks are untested. Career mode looks to be working but i haven't done much testing in this regard.
- The first seconds of a race after a shader-cache rebuild can run at 30–36 fps.
- Other hardware than my own all AMD win11 build.
- No launcher or improved graphics yet. Current focus is visual parity with console before trying to add features.

Each fix can be switched off for comparison; the switches and details are in [NOTES.md](NOTES.md) and
[patches/README.md](patches/README.md).

## Requirements
- VS 2022 Build Tools (MSVC 14.44, CMake, Ninja), Windows SDK 10.0.26100
- LLVM/clang-cl ≥ 18 at `C:\Program Files\LLVM`, Python ≥ 3.11, Git
- AMD FidelityFX SDK v2.x in `thirdparty/fidelityfx` (its `Kits/FidelityFX` folder)
- An FFmpeg win64 **LGPL shared** build in `thirdparty/ffmpeg` (`include/`, `lib/`, `bin/`), e.g.
  `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` from [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases)
- The Disc 1 files of your own copy (the folder with `default.xex` and `Media`)

## Build
```powershell
& .\tools\setup.ps1                            # submodules, XenonRecomp + ReGPU patches, build\tools\*
& .\tools\recomp.ps1 -Xex <your default.xex>    # recompile -> out\ppc (checks in out\check.txt)
tools\vcenv.cmd cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link
tools\vcenv.cmd ninja -C build/rel fm3
```
The first full build compiles the recompiled code (several minutes); later builds take about two.

## Run
```powershell
build\rel\fm3.exe <folder with the Disc 1 files>
```
Saves and settings go to `Documents\fm3` (`FM3_USER_DIR` overrides it); the log is `fm3.log` in the
working directory. `play.cmd` is the test launcher used during development: it uses `out\user` for
saves, writes the log to `out\run`, and turns on ReGPU's debug server for `tools/fm3ctl.ps1`.
The first boot compiles shaders, so expect stutter until the cache is built.

**Launcher** (`launcher/FM3Launcher`, ported from the FM4 ReGPU launcher; needs the .NET 9 SDK to build):
`dotnet build launcher\FM3Launcher -c Release`, or double-click `launcher.cmd`, which builds it if needed and
uses the same folders as `play.cmd`. Tabs: **Play** (game folder, `default.xex` check, language, Play),
**Profiles** (switch, create, duplicate, rename, back up, restore, export, import, delete), **Graphics**
(resolution, aspect, fullscreen, frame cap, VSync, MSAA, FSR 1/3, anisotropy) and **Settings** (shader cache,
folders, test timer, debug server, performance log). It edits `settings.ini` in the user folder and starts
`fm3.exe` from `out\run`. `FM3Launcher.exe --play` starts the game with the saved settings, without the window.

Keyboard: Return = A, Backspace = B, Space = X, Y = Y, Escape = Start, Tab = Back, arrow keys = D-pad,
W / S = throttle / brake, A / D = steer, Q / E = LB / RB, J L I K = right stick. Controllers work through XInput.

## Layout
| Path | What |
|---|---|
| `CMakeLists.txt` | The game build (needs `out/ppc`, `thirdparty/fidelityfx`, `thirdparty/ffmpeg`) |
| `config/` | XenonRecomp config and switch tables for `default.xex` |
| `patches/` | 5 XenonRecomp + 12 ReGPU patches, applied by `tools/setup.ps1` ([patches/README.md](patches/README.md)) |
| `src/` | Runtime: kernel/XAM, loader, threads, fibers, audio, ReGPU host glue ([src/README.md](src/README.md)) |
| `play.cmd` | Development launcher (debug server on, saves in `out/user`, log in `out/run`) |
| `launcher/FM3Launcher`, `launcher.cmd` | The launcher (C#, .NET 9, Photino/WebView2) and its dev start script |
| `boot hl360.cmd` | Starts the hl360 (ReXGlue) baseline build used for comparisons, if you have it under `reference/` |
| `tools/setup.ps1`, `tools/recomp.ps1` | Setup (idempotent) and the recompile pipeline |
| `tools/vcenv.cmd` | Runs a command with VS 2022 Build Tools x64 + LLVM on PATH |
| `tools/hosttools/` | CMake project: patched XenonRecomp, XenonAnalyse, `xexdump`, `ppcdis` |
| `tools/fm3ctl.ps1` | Client for ReGPU's debug server (screenshots, captures, live switches, shader edits) |
| `tools/analysis/` | Screenshot measuring and memory-decoding scripts used for debugging |
| `tools/fm3_route.ps1`, `tools/dumpdecode.py` | Baseline (hl360) capture route and frame-dump decoder/differ |
| `thirdparty/XenonRecomp`, `thirdparty/ReGPU` | Submodules (pinned; see NOTES.md) |

## Licences
Our code: MIT, see [LICENSE](LICENSE). XenonRecomp (MIT, hedge-dev and contributors) and ReGPU (MIT) are
submodules. Most of `src/` and the XenonRecomp patches in `patches/` come from the MIT-licensed FM4 port
(Aiden-Piercee, building on zer0ducksgiven's FM2 port): see [NOTICE.md](NOTICE.md). FFmpeg is linked
dynamically and stays under its own LGPL licence.
