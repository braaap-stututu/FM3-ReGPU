# FM3 ReGPU recomp

Static recompilation of **Forza Motorsport 3 Ultimate Collection (USA)** (Xbox 360, title `4D53084D`,
`default.xex` v0.0.0.8, SHA-256 `aa15372c13bc4c899b5ef90a1cfb5634d389b0a3620cc9715f6023d61a9b8742`)
for Windows x64, on [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) +
[ReGPU](https://github.com/zer0ducksgiven/ReGPU) (Direct3D 12). Layout and build follow the FM2 and FM4 ReGPU ports.

(i tried to make this a fork of the FM2 ReGPU project but github wouldn't let me)

No game data is in this repository. You need your own disc dump.

## Status

**"Playable."** The game boots, can run and finish the audi r8 race and do career races. Performance on my 9800X3D and 7900XTX at 1440p native with 4x MSAA currently hovers around 300-360fps in races when around ai cars and maxes out at around 360fps in time trial or if no ai cars are visible/near (360 is the games tick rate, going higher is not possible). The difference is GPU time for drawing the AI cars' paint and materials (about 0.19 ms per car), which is being worked on. Controller and keyboard work. Wheel support integrated from my FM4 fork but not yet tested. The random crash on some race loads (an AI driver with an empty name) has a workaround and hasn't come back; its cause is still being looked into. Vinyls cover nearly the whole car now, with small gaps at the edges of some cars. Disc 2 (the Content Install Disc: 6 car packs, 2 track packs) installs from its disc image and works in game; since session 18 the DLC packs' database patches are applied too (they never ran before: the game looks for them as loose files). Music now plays in menus and races. Optional: skip the intro, or start straight into a quick race. Always make regular backups. Running log and roadmap: [NOTES.md](NOTES.md);
game-specific quirks: [FM3-QUIRKS.md](FM3-QUIRKS.md).

Fixed so far (all confirmed in game):

| Issue | Cause | Fix |
|---|---|---|
| Washed-out bar across the 3D view | ReGPU's "foreign MSAA" path on FM3's two-tile 2×MSAA scene | off by default (`src/gpu_host.cpp`) |
| Screen turns white facing the sun, with a slowdown | the sun's occlusion query counted a whole tile's worth of draws | ReGPU patch `regpu-zpd-tile-spans` |
| Fine dashed streaks on the asphalt | road strips rendered at track load never reached memory | 32×512 resolves written back at once (`src/gpu_host.cpp`) |
| Smooth bands on the asphalt in a turned chase camera | half of the road detail texture was resolved 256 rows too early | ReGPU patch `regpu-resolve-into-image` |
| Square shadow around the car (as in Xenia-based ports) | 16-bit fixed shadow maps read with the wrong number format | ReGPU patch `regpu-fixed16-fetch` |
| Vinyls noisy, cut along a straight line, or a car vanishing from My Cars after painting | livery pieces resolved into the wrong image / written back too late | ReGPU patches `regpu-resolve-into-enclosing` … `regpu-writeback-safety` ([VINYLISSUES.md](VINYLISSUES.md)) |
| Livery broken (all but one section) on some race loads | each load builds the livery in a new image just above the old one; pieces went into the old one | ReGPU patch `regpu-livery-enclosing-recent` |
| Other cars' shade flickering on your car | FM3 clears the car's shade map through a 4x MSAA view; only depth clears were scaled | ReGPU patch `regpu-msaa-color-clear-scale` |
| Sun glare far too big / no sun flare with the faster renderer | occlusion query answers (raw sample count, worker command lists) | ReGPU patches `regpu-worker-queries`, `regpu-zpd-scale`, `regpu-draw-thread-pieces` |
| Low frame rate near other cars (~70 fps at 1440p 4x MSAA) | fence waits and a busy command processor | fence/draw-thread defaults in `src/gpu_host.cpp`, ReGPU patch `regpu-early-fence-writebacks` |
| Slower than needed around AI cars | every translated pixel shader carried the alpha test's `discard` (even with the test off), which can stop the GPU skipping hidden pixels early | shader variants without it when the test is off (ReGPU patch `regpu-split-alpha-test`, +5% on a race grid, picture identical) |
| No music (or a few seconds, then none for the session) | the music streamer gave up waiting for a decoder; then our XMA seek guard reset the music decoder between packets | codec opened at once for that start loop, no seek guard for music (`src/audio/xma.cpp`) |

Known issues and not yet checked:
- Photo mode and the pre-race camera show a whitened band over the top third of the image (not seen while driving); its edge is exactly one 4x MSAA EDRAM tile (row 256 of 720).
- Engine sound at steady revs sweeps in tone ("AAAAEEEEOOOO"); being looked into. The launcher's experimental "Engine sound crossfade" made it worse in testing: leave it off.
- Disc 2: tested with Fujimi Kaido and the Pagani Zonda R; its decals may look lower resolution than expected (not compared with console yet). Profiles and most tracks are untested. Career mode looks to be working but i haven't done much testing in this regard.
- The game can hang when alt-tabbing out during a race.
- Small vinyl gaps at the edges of some cars (Scion, Fiat, Supra rear bumper).
- The first seconds of a race after a shader-cache rebuild can run at 30–36 fps.
- Other hardware than my own all AMD win11 build.
- The launcher covers settings, profiles and the Disc 2 install; game import and photos are not ported from the FM4 launcher yet. Current focus is visual parity with console before adding more features.

Each fix can be switched off for comparison; the switches and details are in [NOTES.md](NOTES.md) and
[patches/README.md](patches/README.md).

## Requirements
- VS 2022 Build Tools (MSVC 14.44, CMake, Ninja), Windows SDK 10.0.26100
- LLVM/clang-cl ≥ 18 at `C:\Program Files\LLVM`, Python ≥ 3.11, Git
- AMD FidelityFX SDK v2.x in `thirdparty/fidelityfx` (its `Kits/FidelityFX` folder)
- An FFmpeg win64 **LGPL shared** build in `thirdparty/ffmpeg` (`include/`, `lib/`, `bin/`), e.g.
  `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` from [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases)
- The Disc 1 files of your own copy (the folder with `default.xex` and `Media`)
- Optional: your Disc 2 image (`.iso`) for the extra cars and tracks (about 3.4 GB once installed)

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
saves, writes the log to `out\run`, skips the intro movie (`FM3_SKIP_INTRO=0` to see it), and turns on ReGPU's
debug server for `tools/fm3ctl.ps1`.
The first boot compiles shaders, so expect stutter until the cache is built.

**Launcher** (`launcher/FM3Launcher`, ported from the FM4 ReGPU launcher; needs the .NET 9 SDK to build):
`dotnet build launcher\FM3Launcher -c Release`, or double-click `launcher.cmd`, which builds it if needed and
uses the same folders as `play.cmd`. Tabs: **Play** (game folder, `default.xex` check, Disc 2 content install,
language, skip intro, start in a quick race, Play),
**Profiles** (switch, create, duplicate, rename, back up, restore, export, import, delete), **Graphics**
(resolution, aspect, fullscreen, frame cap, VSync, MSAA, FSR 1/3, anisotropy) and **Settings** (shader cache,
folders, test timer, debug server, performance log, and A/B toggles: draw workers, engine sound crossfade). It edits `settings.ini` in the user folder and starts
`fm3.exe` from `out\run`. `FM3Launcher.exe --play` starts the game with the saved settings, without the window.

**Disc 2:** on the launcher's Play tab, "Disc 2 content" > "Install from .iso…" (or "From folder…" for an
extracted disc or the package files), or run `install-disc2.cmd <Disc 2 .iso>` (without an argument it takes the
`*Disc 2*.iso` next to it). The 8 content packages are unpacked into `dlc\` in the user folder; the game mounts
them after you press Start at the title screen. Close the game first.

**Intro and quick start:** `skip_intro=1` in `settings.ini` (launcher: "Skip intro") goes straight to the title
screen. `quick_race=1` ("Start in a quick race") also goes through Free Play > Quick Race by itself and starts a
race about a minute after launch; don't press anything until it loads. For developers: `FM3_CMDLINE` passes
Turn 10's developer command-line options to the game (see `src/hooks/boot_options.cpp`; its `launch` option
doesn't work in the retail game).

Keyboard: Return = A, Backspace = B, Space = X, Y = Y, Escape = Start, Tab = Back, arrow keys = D-pad,
W / S = throttle / brake, A / D = steer, Q / E = LB / RB, J L I K = right stick. Controllers work through XInput.

## Layout
| Path | What |
|---|---|
| `CMakeLists.txt` | The game build (needs `out/ppc`, `thirdparty/fidelityfx`, `thirdparty/ffmpeg`) |
| `config/` | XenonRecomp config and switch tables for `default.xex` |
| `patches/` | 5 XenonRecomp + 18 ReGPU patches, applied by `tools/setup.ps1` ([patches/README.md](patches/README.md)) |
| `src/` | Runtime: kernel/XAM, loader, threads, fibers, audio, ReGPU host glue ([src/README.md](src/README.md)) |
| `install-disc2.cmd` | Installs Disc 2's content packages from the disc image (same as the launcher's Disc 2 tile) |
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
