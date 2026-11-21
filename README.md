# FM3 ReGPU recomp

Static recompilation of **Forza Motorsport 3 Ultimate Collection (USA)** (Xbox 360, title `4D53084D`,
`default.xex` v0.0.0.8, SHA-256 `aa15372c13bc4c899b5ef90a1cfb5634d389b0a3620cc9715f6023d61a9b8742`)
for Windows x64, on [ReXenon](https://github.com/zer0ducksgiven/ReXenon) (a fork of hedge-dev's
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp), the default recompiler; XenonRecomp stays selectable) +
[ReGPU](https://github.com/braaap-stututu/ReGPU) (**Vulkan** since November 2026: our ReGPU fork's `vulkan` branch, which
carries every FM3 fix from the old patch stack). The Direct3D 12 renderer this replaced lives on as branch
[`LegacyDX12`](https://github.com/braaap-stututu/FM3-ReGPU/tree/LegacyDX12) (upstream ReGPU 57143cb plus `patches/`); the
launcher's Settings > Testing "Legacy DX12 renderer" runs it when it is built (`build\legacy\fm3.exe`, or `legacy-dx12\fm3.exe`
in an install). Layout and build follow the FM2 and FM4 ReGPU ports.

(i tried to make this a fork of the FM2 ReGPU project but github wouldn't let me)

No game data is in this repository. You need your own disc dump.

## Status

**"Playable."** The game boots, can run and finish the audi r8 race and do career races. Performance on my 9800X3D and 7900XTX at 1440p native with 4x MSAA currently hovers around 300-360fps in races when around ai cars and maxes out at around 360fps in time trial or if no ai cars are visible/near (360 is the games tick rate, going higher is not possible). Below the cap, the limit near AI cars is now ReGPU's draw thread (the CPU side); at 4K it is the GPU (about 229 fps on a frozen race grid). Since session 31 the per-frame upload ring sits in VRAM (needs resizable BAR / Smart Access Memory; on by default, launcher toggle "Upload ring in VRAM"): +18-26% at 1440p 4x MSAA, picture identical ([docs/VRAM-UPLOAD-RING.md](docs/VRAM-UPLOAD-RING.md)). Controller and keyboard work. Wheel support integrated from my FM4 fork but not yet tested. The random crash on some race loads (an AI driver with an empty name) has a workaround and hasn't come back; its cause is still being looked into. Vinyls cover nearly the whole car now, with small gaps at the edges of some cars. Disc 2 (the Content Install Disc: 6 car packs, 2 track packs) installs from its disc image and works in game; since session 18 the DLC packs' database patches are applied too (they never ran before: the game looks for them as loose files). Music now plays in menus and races. Optional: skip the intro, or start straight into a quick race. **VR (experimental, not yet tested in a headset):** an OpenXR headset (Virtual Desktop, SteamVR, Oculus ...) can show the game on a virtual screen, head-track the in-car view, or show it in true stereo 3D with the HUD on its own panel (the launcher's VR tab, "Play in VR"; F8 or both stick clicks held recentre; numpad 8/2, 9/3, 4/6 move the seat, 5 resets it; the in-car camera's shake and lean are off in VR; the frame rate follows the headset's). Always make regular backups. Running log and roadmap: [NOTES.md](NOTES.md);
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
- The launcher covers settings, profiles, the Disc 1 import from its .iso and the Disc 2 install; photos are not ported from the FM4 launcher yet. An installer for other PCs is built by `installer\build-installer.ps1`. Current focus is visual parity with console before adding more features.

Each fix can be switched off for comparison; the switches and details are in [NOTES.md](NOTES.md) and
[patches/README.md](patches/README.md).

## Requirements
- Git, PowerShell (Windows' own 5.1 is enough), and about 6 GB of disk for the build toolchain
- The build toolchain, either way:
  - **Portable (nothing installed, no admin rights):** `powershell -ExecutionPolicy Bypass -File tools\get-toolchain.ps1`
    downloads everything into `toolchain\` (gitignored): the MSVC 14.44 x64 libraries and headers and the Windows SDK
    10.0.26100 from Microsoft's own Visual Studio 2022 package feed (you're asked to accept Microsoft's Build Tools
    licence; `-AcceptMicrosoftLicense` skips the question), LLVM 23.1.5, CMake 3.31.10, Ninja 1.13.2 and Python 3.14 (python.org's embeddable package), plus AMD's
    FidelityFX SDK 2.1.0 and FFmpeg 8.1 LGPL into `thirdparty\`. Each download is checked against a SHA-256 (from
    GitHub, Microsoft's feed, or pinned in the script). About 2.2 GB of downloads, kept in `toolchain\downloads` so a
    rerun only fetches what's missing (delete that folder afterwards to save space). `-Launcher` adds the .NET 9 SDK
    for the launcher. `tools\vcenv.cmd`, which every build command below goes through, then uses `toolchain\env.cmd`
    (`FM3_SYSTEM_TOOLCHAIN=1` uses installed tools instead).
  - **Installed:** VS 2022 Build Tools (MSVC 14.44, Windows SDK 10.0.26100, CMake, Ninja) at their default folder,
    LLVM/clang-cl ≥ 18 at `C:\Program Files\LLVM`, AMD FidelityFX SDK v2.x in `thirdparty/fidelityfx` (its
    `Kits/FidelityFX` folder) and an FFmpeg win64 **LGPL shared** build in `thirdparty/ffmpeg` (`include/`, `lib/`,
    `bin/`), e.g. `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` from [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases)
- The **Vulkan SDK** (LunarG, 1.3 or newer; its installer sets `VULKAN_SDK`): ReGPU's shaders are compiled to SPIR-V with
  its `dxc.exe`, and its `dxcompiler.dll` is copied beside `fm3.exe` (`FM3_DXC_DIR` picks another one). The portable
  toolchain doesn't fetch it yet. A Vulkan 1.3 GPU and driver to play.
- With installed tools also Python ≥ 3.11 with the `py` launcher (the build runs `py -3` for ReGPU's shader headers; the
  portable toolchain brings its own)
- The Disc 1 files of your own copy (the folder with `default.xex` and `Media`)
- Optional: your Disc 2 image (`.iso`) for the extra cars and tracks (about 3.4 GB once installed)

## Build
```powershell
& .\tools\get-toolchain.ps1                    # once, for the portable toolchain (skip with installed tools)
& .\tools\setup.ps1                            # submodules, ReXenon/XenonRecomp/ReGPU patches, build\tools\*, build\tools-rexenon
& .\tools\recomp.ps1 -Xex <your default.xex>    # recompile with ReXenon -> out\ppc-rexenon (-Recompiler xenonrecomp -> out\ppc)
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
language, skip intro, start in a quick race, start on the test grid, Play),
**Profiles** (switch, create, duplicate, rename, back up, restore, export, import, delete), **Graphics**
(graphics card, resolution, aspect, fullscreen, VR headset, frame cap, VSync, MSAA, edge filters, supersampling, FSR 1/3/4, anisotropy, speed preset), **Wheel stuff** (steering,
force feedback, pedals, shifter, handbrake) and **Settings** (shader cache, folders, test timer, debug server, full and
light performance logs, and toggles: draw workers, asynchronous present, upload ring in VRAM, one guest tile,
half-rate mirror, engine sound crossfade). It edits `settings.ini` in the user folder and starts
`fm3.exe` from `out\run`. `FM3Launcher.exe --play` starts the game with the saved settings, without the window.

**Controller:** the launcher works with a gamepad (Xbox, PlayStation, Steam Deck): LB/RB switch tabs, the D-pad or left
stick moves, A selects (lists open as a menu, sliders adjust with left/right), B backs out, Start plays. A bar at the
bottom shows the buttons and what the focused option does.

**Proton / Wine (the Windows build on Linux):** install with the Windows installer (or copy the install folder) and
run `launcher\FM3Launcher.exe` through Proton, e.g. as a non-Steam game in Steam with Proton forced. WebView2 can't run
under Wine, so the launcher detects it and opens its page in your Linux browser instead (served by the launcher on
127.0.0.1 under a random token; it quits when the page has been closed for 15 s). If no browser opens (Steam's
container), open the address in `launcher-url.txt` in the install folder. File pickers are Wine's own dialogs; the
game starts in the same Proton prefix. `FM3Launcher.exe --browser` does the same on Windows (and a Windows without
WebView2 falls back to it). Steam Deck Game Mode can't show the browser: set things up in Desktop Mode, then use
`--play` as the launch option. Not tested under Proton yet.

**Linux:** `powershell -ExecutionPolicy Bypass -File installer/build-linux.ps1` builds
`out/installer/dist/FM3-ReGPU-linux-<version>.tar.gz`: the same game build plus the launcher for Linux (self-contained;
needs WebKitGTK 4.1). The game runs through Proton or Wine: launcher Settings > Linux picks umu-launcher, Steam's
Proton (GE-Proton first) or Wine (Automatic tries them in that order) and the prefix (default
`~/.local/share/fm3-regpu/prefix`). Unpack, run `./fm3-launcher.sh`, then Play tab > Game > "From .iso...". For Steam
(e.g. the Deck's Game Mode): `fm3-launcher.sh` as a non-Steam game with `--play`. Not tested on Linux yet.

**Installer (another PC):** `powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1` builds
`out\installer\dist\FM3-ReGPU-Setup-<version>.exe` (needs Inno Setup 6: `winget install JRSoftware.InnoSetup`):
the current `fm3.exe` with its DLLs and the self-contained launcher, per user, no admin rights, no game files. On
the other PC: install, open the launcher, Play tab > Game > "From .iso…" with the Disc 1 image (checked, then
extracted next to the game, ~6.8 GB; or `FM3Launcher --install-disc1 <Disc 1 .iso>`), Disc 2 as below. Saves and
settings go to `Documents\fm3` there; uninstalling removes the extracted Disc 1 but keeps them.

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
| `patches/` | 1 ReXenon, 5 XenonRecomp and 81 ReGPU patches, applied by `tools/setup.ps1` ([patches/README.md](patches/README.md)) |
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
| `thirdparty/ReXenon`, `thirdparty/XenonRecomp`, `thirdparty/ReGPU` | Submodules (pinned; see NOTES.md). CMake `FM3_RECOMPILER` picks the recompiler's output (`rexenon` default, or `xenonrecomp`) |

## Licences
Our code: MIT, see [LICENSE](LICENSE). ReXenon (MIT, zer0ducksgiven, a fork of XenonRecomp), XenonRecomp (MIT,
hedge-dev and contributors) and ReGPU (MIT) are submodules. Most of `src/` and the XenonRecomp patches in `patches/` come from the MIT-licensed FM4 port
(Aiden-Piercee, building on zer0ducksgiven's FM2 port): see [NOTICE.md](NOTICE.md). FFmpeg is linked
dynamically and stays under its own LGPL licence.
