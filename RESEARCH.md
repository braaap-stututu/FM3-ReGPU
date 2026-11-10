# Research

Investigations that are not bugs yet: options, feasibility studies and leads with no code behind them. Open bugs
live in [OPEN-ISSUES.md](OPEN-ISSUES.md), vinyl/livery work in [VINYLISSUES.md](VINYLISSUES.md).

**Session 21–22 studies (2026-11-10, cloud, no code; first local steps in each):**
| Study | One-line result |
|---|---|
| [Installer](#installer-for-a-public-release-how-the-sibling-ports-do-it-session-21-2026-11-10) | FM2/FM4: Inno Setup per user, own-disc hash check, bundled Python, GitHub patch-zip updater. FH1R: builds the game on the player's PC, SignPath signing. FM3: Inno + the launcher's C# XISO/STFS, self-contained launcher; **decide: ship the exe or build locally, and signing** |
| [Half-rate shading (VRS)](#half-rate-shading-variable-rate-shading-for-weak-gpus-and-igpus-session-21-2026-11-10) | Feasible without shader changes (tier 1 per draw, live A/B); scene draws only, never liveries/pictures/HUD/alpha-tested; no VRS on Vega APUs, Polaris, RDNA1, Pascal, old Intel; render scale + FSR stays the bigger iGPU lever; measure the pixel-shader share first |
| [Telemetry to SimHub](#telemetry-out-to-simhub-forza-data-out-udp-session-21-2026-11-10) | Send FM7's 311-byte Forza "Data Out" Dash packet over UDP; SimHub's Forza profile reads it; needs FM3's car-physics object (the telemetry pages' getters are the shortcut) |
| [Benchmark tools](#benchmark-tools-the-players-car-driven-by-the-games-ai-and-a-frozen-grid-session-21-2026-11-10) | Frozen world while rendering: dt 0 in `frame_time.cpp` (small, our code). AI on the player car: FH1R's native-AI takeover (opponent factory + releasing the human override). AI throttle/brake/clutch: one float controls block per car in FH1 (+3904), very likely in FM3 too; same RE as telemetry |
| [FH1R latest + Sonic Unleashed](#fh1-reimagineds-latest-performance-work-and-sonic-unleashed-recompiled-session-22-2026-11-10) (session 22) | FH1R: CP on two threads (+13%/≥6 cores, −11% on 4), only changed register writes recorded, descriptor reuse −7%/draw, **GPU time by work kind: RT transfers were 37% of the GPU frame**; their ReGPU plugin uses `REGPU_ANISO_FROM_FETCH` (never tried on FM3), tile merge 0 gain, PGO +6%. Upstream ReGPU: 15 commits past our pin (vertex reach, view submit, one-copy queries). Unleashed: hardware MSAA resolve, pipelines built at asset load (`non_volatile_as_local` later measured as a loss by Pinyon Shift) |
| [Pinyon Shift](#pinyon-shift-fh1-recomp-performance-leads-session-22-2026-11-10) (session 22) | GPU: fetch state as shader variants (our `TFetch` branches on gamma/exp/swizzle per fetch; ~5% there), per-shader price list first, per-hash 0×Inf rule with a NaN scrub (car paint is the risky one), depth-alias copies −1.1 ms, specialised resolves. CPU: inline register save/restore, fewer MXCSR toggles, `SetPWLGamma` every frame (30% of FH1's render thread); **`non_volatile_as_local` 20–27% slower**. Stutter: wait for pipelines at submit, long-frame sampler, slow SQLite views after a race |

## Installer for a public release: how the sibling ports do it (session 21, 2026-11-10)
Read from the public repos (`zer0ducksgiven/Forza-Motorsport-2-ReGPU`, `Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU`, the
user's `Fuzzy-Rittersport-4-ReGPU-testing`, `FH1-ReImagined/ForzaHorizon1-ReImagined`; shallow clones, read only).

### FM2 (and FM4, which copied it: same `installer/fm2.iss`, minus FM2's later space check and error log)
- **Inno Setup 6** script `installer/fm2.iss`, built by `installer/build-installer.ps1 -Version x.y.z` →
  `installer/dist/FM2-Setup-<version>.exe`. **Per user, no admin rights** (`PrivilegesRequired=lowest`,
  default folder `{localappdata}\FM2`), lzma2 solid, modern wizard, start-menu and optional desktop shortcut,
  "Open the launcher" at the end. A fixed `AppId` GUID so later versions upgrade the same install.
- **No game files in the installer.** Wizard pages ask for the player's own disc image (.iso), the title update
  and an optional DLC folder. Each is **checked against the build on the page's Next button** (the disc's
  `default.xex` SHA-256 and the title update's SHA-256 are hard-coded in `setup_game.py`); a wrong disc stops there.
- The disc and title-update work runs in a **bundled Python** (the embeddable 3.12 package in the payload, with
  their `xiso.py`/`stfs.py` + `setup_game.py`: `check-iso`, `check-tu`, `install`, `space`). After the files are
  copied, the installer runs `install`: unpacks the title update to `Documents\fm2\update`, copies DLC to
  `Documents\fm2\dlc`, extracts the disc into `{app}\game`. Output goes to `{app}\out\setup.log`; a failure shows the
  log's last line. FM2 added a **6 GB free-space check** on the folder page after installs failed half-way.
- **.NET runtime:** the launcher is framework-dependent; if the .NET 10 Desktop Runtime is missing, the installer
  downloads it from `aka.ms` on the Ready page and runs it quietly (Windows asks for admin rights for that step only;
  a failed download still installs the game).
- **What ships:** `build\tu\*` (fm2.exe, its DLLs, minus `nvngx_dlss.dll` and `xenos_dis.exe`), the published
  launcher, scripts (`play.cmd`, `benchmark.cmd`, perf tests, `TESTING.md`) and `changelog.txt`. The recompiled
  exe is shipped, built by the maintainer.
- **Updates:** `tools/make_patch.py <version>` builds `FM2-<version>-patch.zip` (fm2.exe, d3dcompiler, the launcher,
  the changelog; names checked on read-back: no control characters, nothing outside the install). The launcher's
  `Updater.cs` reads `api.github.com/repos/<repo>/releases/latest`, downloads the `*-patch.zip` asset and unpacks it
  over the install; running launcher files are **renamed to `*.old`** first (Windows lets a running exe be renamed,
  not overwritten) and deleted at the next start. `--apply <zip>` applies one by hand. The exe also does a
  background update check (`src/update_check.cpp`, WinHTTP, 4 s timeouts, any failure silent). The version comes
  from fm2.exe's version resource (`ProductVersion`), else `version.txt`. A GitHub workflow posts each release to
  Discord. Lesson in `make_patch.py`: a DLL the old launcher holds open (SDL3) can't be replaced: leave unchanged
  DLLs out of the patch.
- **Linux:** `tools/make_linux.py` → `FM2-Linux-<version>.tar.gz`: the Windows exe and DLLs (no DLSS: no NGX under
  vkd3d-proton), `play.sh`/`setup.sh` for Proton, the same Python helpers, the launcher published self-contained
  for `linux-x64` (WebKitGTK).

### FH1-ReImagined (a different model)
- Also Inno Setup, per user (`{localappdata}\Programs\...`), but it ships **no game code at all, not even the
  recompiled exe**: the release holds the launcher (PyInstaller), the project's sources and a patched SDK overlay;
  **the player's machine runs the recompiler and builds the game** (~4.6 min at 4 jobs on 8 cores), resumable,
  rebuilt only when something changed. A legal choice: the shipped bytes contain nothing derived from the game.
- Built in **GitHub Actions** (`release.yml`: tests, Linux/Windows/macOS packages, Flatpak), `SHA256SUMS` over the
  release assets (`release_checksums.py`), the updater checks each download's SHA-256 (GitHub's asset digest).
- **Code signing through SignPath Foundation** (free for open-source projects): the launcher, the Inno uninstaller
  and the installer are signed in CI (`launcher/packaging/signpath/*.xml`, off until the repo variables exist;
  release signing needs a maintainer's approval per request). They also build PyInstaller's bootloader from
  source because antivirus flagged the stock one: unsigned recompiled exes get SmartScreen warnings and false
  positives.
- Installer extras: version resource on the installer (antivirus reports name it), licence page, privacy policy
  page + "Online features" opt-out, VC++ redistributable installed only when missing.

### What FM3 would need (recommendation)
1. **Inno Setup, per user, FM2's layout**, with a fixed AppId. Wizard: Disc 1 .iso (required: check `default.xex`
   SHA-256 `aa15372c…8742`, the hash in CLAUDE.md), Disc 2 .iso (optional), a free-space check (Disc 1 is 6.8 GB:
   ask for ~8 GB). **No bundled Python needed:** our launcher already reads XISO and STFS in C# (`XIso.cs`, `Stfs.cs`,
   `Disc2.cs`, `--install-disc2`); add `--check-disc1 <iso>` and `--install-disc1 <iso> <dir>` and have the installer
   call the launcher (log to `{app}\out\setup.log`, show its last line on failure, as FM2 does).
2. **Runtimes:** fm3.exe links the CRT statically (`CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded`), so no VC++
   redistributable. The launcher is framework-dependent .NET 9 (Photino needs the plain .NET runtime, not the Desktop
   one): either download it like FM2 (admin prompt) or **publish the launcher self-contained** (~70 MB more, no
   runtime step: simpler for players). WebView2 is in Windows 11; Windows 10 may need the Evergreen bootstrapper.
3. **Payload:** `fm3.exe`, `d3dcompiler_47.dll`, the FFmpeg LGPL DLLs (avcodec/avutil/swresample: ship the LGPL
   notice and a link to the exact source build), FidelityFX's signed DLLs, the launcher, `changelog.txt`, licences
   (`LICENSE`, `NOTICE.md`, `NOTICE-wheel.md`, FFmpeg, FidelityFX). Never `reference/`, `dumps/`, ISOs, `out/user`.
   Leave out debug-only scripts or keep `play.cmd` as "start without the launcher".
4. **Updates:** port the FM4 fork's `Updater.cs` (CLAUDE.md lists it as not yet ported) + a `make_patch.py`
   equivalent; give fm3.exe a version resource; a release workflow attaching the installer, the patch zip and
   `SHA256SUMS`.
5. **Decide first (user):** ship the recompiled exe (FM2/FM4) or build on the player's machine (FH1R: legally
   cleanest, but needs the LLVM/MSVC toolchain on the player's PC, and our recompile + build takes much longer
   than FH1R's). And whether to sign (SignPath Foundation application) before the first public release.
6. Before release: the user dir is `Documents\fm3` and the game runs in `{app}\out\run` (logs, captures): fine under
   `{localappdata}`, but the debug server must default off in a release build.

## Half-rate shading (variable-rate shading) for weak GPUs and iGPUs (session 21, 2026-11-10)
**Asked (user):** an option that shades at half rate on weak GPUs. The tool for it is D3D12 **variable-rate shading
(VRS)**: the pixel shader runs once per 2x1, 1x2 or 2x2 block of pixels instead of per pixel, while rasterisation,
depth and coverage stay per pixel (edges of triangles stay sharp; surfaces get softer). Nothing checked in ReGPU's
source here (the submodule isn't checked out in the cloud container); this is the design and its limits.

### What it can and can't save
- It cuts **pixel-shader work only**: 2x1/1x2 up to half, 2x2 up to a quarter, on draws where it is applied. It does
  not cut render-target writes, blending, depth traffic, resolves or texture uploads. ReGPU's render targets are
  RGBA16F + D32S8 (8 bytes a pixel each), so on a bandwidth-starved iGPU much of the frame isn't pixel-shader work.
  Games report ~5–15% overall for 2x2 on the parts of the scene they apply it to; nothing measured on FM3.
- **Our own evidence that the shader part is big:** on the 7900 XTX at 1440p 4x the main scene is 3.08 of 4.72 ms and
  the AI cars' cost is pixel shading of their materials (session 20). Whether that holds on an iGPU at 720p 1x is
  unknown: measure first (`REGPU_GPU_PASSES=1` / `REGPU_GPU_SHADERS=1` on an iGPU, or RGP/PIX).
- **Lower render scale + FSR (already in the launcher) is the stronger lever** for iGPUs: it cuts every per-pixel
  cost, bandwidth included. VRS is a finer tool on top: full resolution for edges and the HUD, cheaper shading inside
  surfaces.

### Which GPUs have it (`CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6).VariableShadingRateTier`)
- **Tier 1** (one rate per draw): Intel from Gen11 (Ice Lake) on; NVIDIA Turing and later; AMD RDNA2 and later.
- **Tier 2** (also a screen-space rate image and a per-primitive rate): NVIDIA Turing and later, AMD RDNA2 and later,
  Intel Arc. Intel Iris Xe (Tiger Lake and later laptops): at least tier 1, check the reported tier.
- **None:** AMD Vega-based APUs (Ryzen 2000–5000G/U, the common older iGPUs), Polaris (RX 400/500), RDNA1 (RX 5000),
  NVIDIA Pascal (GTX 10xx), Intel UHD before Gen11. **Many of the weakest machines can't use it**; the newer iGPUs
  can (Radeon 680M/780M/890M, Steam Deck through vkd3d-proton's `VK_KHR_fragment_shading_rate`, Iris Xe, Arc).
- So the option must hide itself when the tier is 0 (the launcher can't know: fm3.exe logs the tier and ignores the
  setting on such GPUs; or the launcher asks DXGI/D3D12 itself, like the GPU-choice study).

### How it fits ReGPU (game-neutral, behind a `REGPU_*` switch)
- **No shader changes:** per-draw rates (`RSSetShadingRate`) and the rate image (`RSSetShadingRateImage`) need only
  `ID3D12GraphicsCommandList5`, and work with ReGPU's FXC/SM 5.1 shaders (only the per-primitive `SV_ShadingRate`
  output needs SM 6.4). No pipeline (PSO) change, so no new shader or pipeline cache.
- **Recording:** set the rate before each draw it applies to, with state tracking (only on change). The draw thread
  and worker lists record through `RecordedList`: add an op for the rate, as `regpu-turbo-hyper` did for predication.
- **Which draws (the hard part: VRS must never touch what is copied out or read as data):**
  - **Never:** passes whose results the game reads back or reuses as images: livery building (2048²/1024² write-backs;
    VRS would bake soft blocks into the paint), car pictures, the 32x512 strips, the 256x256 shade map, shadow maps
    (depth-only: no pixel shader cost anyway), cube/reflection faces (maybe later: cheap and blurred already), the HUD,
    UI and text, the final composite.
  - **Yes:** the main 3D scene (FM3's 1280-pitch scene surface, both EDRAM tiles), possibly full-screen post passes
    (bloom/blur passes are low-frequency already).
  - ReGPU knows each draw's surface (EDRAM base, pitch, format, MSAA) and pass size; a host callback in
    `src/gpu_host.cpp` (FM3 rule: the scene surface, depth-tested draws) decides, as the other FM3 rules there do.
- **Modes, simplest first:**
  1. **Per draw, tier 1:** every scene draw at 2x1 or 2x2 (an "Off / Light (2x1) / Strong (2x2)" setting). Works on all
     VRS GPUs.
  2. **Rate image, tier 2:** coarse only where it shows least. Options: by depth from the previous frame (distant
     scenery and distant cars at 2x2, near the camera 1x1: PERF-PLAN V2); by screen region (the edges, where a racing
     game's motion is fastest); by the previous frame's local contrast (AMD's FidelityFX Variable Shading idea: flat
     sky, tarmac and shadow at 2x2, detail at 1x1; check whether our FidelityFX SDK 2.1 still ships it). The per-draw
     rate combines with the image (`D3D12_SHADING_RATE_COMBINER_MAX`/`PASSTHROUGH`), so excluded draws force 1x1.
- **MSAA:** weak-GPU settings run 1x MSAA, the simple case. With 4x MSAA a 2x2 coarse pixel covers 16 samples; larger
  rates with MSAA need `AdditionalShadingRatesSupported`. Allow 2x1/2x2 only, and test with MSAA on.

### FM3 risks to check
- **Alpha-tested foliage and fences** (FM3's dithered alpha to mask, OPEN-ISSUES F): `discard` is decided once per
  coarse pixel, so cut-outs get blockier. Exclude alpha-tested draws (ReGPU knows them since `regpu-split-alpha-test`).
- **Car paint and specular highlights** shimmer more when coarse; distant-only (depth image) avoids most of it.
- **Text and markings in the world** (track signs, numbers on cars): softer at 2x2; 2x1 is the safer default.
- **Texture detail:** a coarse pixel picks its mip from coarse derivatives, so textures look one mip softer there
  (expected; that is part of the saving).
- Sun-flare occlusion queries count covered samples: rasterisation is unchanged by VRS, so they should be unaffected
  (check at the Montserrat sun spot).

### Plan
1. **Measure the share** on a weak GPU (or the 7900 XTX at 720p 1x as a stand-in): GPU time of the main scene pass vs
   the rest. If pixel shading is a small share there, stop: render scale + FSR is the answer.
2. **ReGPU patch `regpu-vrs`:** `REGPU_VRS=2x1|2x2` (tier 1, per draw), **live** switch (the rate is set at record time,
   so an A/B on a paused grid is instant), a host callback choosing draws, log line with the GPU's tier.
3. **Numbers and pictures:** fps per mode on the race grid (`tools/fm3_ab.ps1`), `pngdiff.py` against off, user check
   in motion (shimmer). Then FM3's settings: `vrs=off|light|strong` in settings.ini, launcher Graphics option (hidden or
   greyed when the tier is 0), included in the Mild/Low/Extreme speed presets if it pays.
4. **Later:** the tier-2 depth image (distant = coarse), which is also PERF-PLAN V2 for the 7900 XTX.
Effort: tier 1 per draw ~1 session (patch + FM3 rule + tests); the tier-2 image another session or two.

## Telemetry out to SimHub (Forza "Data Out" UDP) (session 21, 2026-11-10)
**Asked (user):** can we send FM3's car physics/telemetry to a UDP port SimHub understands? **Yes, feasible**: SimHub
already reads Forza's own "Data Out" UDP packets (FM7, FH4/5, FM 2023), so if fm3.exe sends packets in that format,
SimHub (dashboards, bass shakers, motion, wheel LEDs) works with its existing Forza game profile. Nothing checked
against FM3's code yet (no `out/ppc` in the cloud container); the format and the plan below are from public sources.

### The format (Turn 10's documentation, as laid out in `geeooff/forza-data-web` `Core/*.cs`)
Little-endian, one datagram per update, no header:
- **Sled, bytes 0–231** (232 bytes): `s32 IsRaceOn` (1 driving, 0 menus/paused), `u32 TimestampMS`, `f32` engine max /
  idle / current RPM, acceleration XYZ and velocity XYZ (car space: X right, Y up, Z forward; m/s², m/s), angular
  velocity XYZ, yaw/pitch/roll, then per wheel (FL, FR, RL, RR): normalized suspension travel (0 stretched, 1
  compressed), tyre slip ratio, wheel rotation speed (rad/s), `s32` on rumble strip, puddle depth, surface rumble,
  slip angle, combined slip, suspension travel (m); then `s32` CarOrdinal, CarClass (0 D … 7 X), PerformanceIndex,
  DrivetrainType (0 FWD, 1 RWD, 2 AWD), NumCylinders.
- **Car Dash, bytes 232–310** (FM7's packet = **311 bytes**): `f32` position XYZ, speed (m/s), power (W), torque (N·m),
  tyre temps ×4, boost, fuel, distance travelled, best / last / current lap, current race time; `u16` lap number,
  `u8` race position, accel, brake, clutch, handbrake, gear; `s8` steer, normalized driving line, AI brake difference.
- Variants: FH4/5 insert 12 bytes after the sled (324 bytes); FM 2023 appends tyre wear ×4 and a track ordinal (331).
  **Send FM7's 311-byte Dash**: the oldest, simplest one, which SimHub's Forza Motorsport 7 profile reads.
- SimHub setup (third-party guides): the game sends to `127.0.0.1:<port>`, SimHub's Forza game settings listen on the
  same port (avoid 20127; FM7 guides use e.g. 10001). Only one listener can own a UDP port: SimHub can forward to a
  second app. FM7/FH4 needed a Windows loopback exemption because they are UWP apps; **fm3.exe is a plain Win32 exe, so
  no loopback exemption is needed.**

### What FM3 must give us, and where it probably is
- **The player's car-physics object.** FH1-ReImagined (FM4 engine, FM3's successor) reads FH1's `ICarDynamics`
  (`forza-recomp/src/platform/car_physics.h`): engine angular velocity +324 (rad/s → RPM × 60/2π), redline +452, idle
  +1228, gear byte +2240, ABS +3020, speed +3128, the processed controls (gas, brake, steer, clutch, handbrake) +3904,
  wheels from +3936 (stride 1024 on disc: rotation speed +12, normalized slip +496, slip angle +520, contact +796,
  suspension). FM3's object is very likely the same kind of thing with other offsets (the same block the AI-control
  study above needs: one piece of RE serves both).
- **The in-game telemetry pages are the shortcut.** FM3's own telemetry screen (d-pad in a race) shows RPM, gear,
  speed, G-forces, tyre temperatures, suspension travel, slip. Its HUD code reads those values through getters: find
  them (`xref.py strings` for the page's labels, `refs` to their users) and we have the field meanings and offsets
  without guessing. FH1R did the same with FH1's HUD getters.
- Fields that are easy: RPM, max/idle RPM, gear, speed, controls (accel/brake/clutch/handbrake/steer scaled to bytes),
  position, wheel speeds; velocity and acceleration can be derived from position over time if not stored, yaw/pitch/
  roll from the car's orientation matrix. Harder: tyre temps, slip values, suspension (from the telemetry-page
  getters), lap times / lap number / race position (the race's timing object), CarOrdinal (send FM3's `Data_Car` id:
  SimHub won't know FM3's car names, it only labels the car), PerformanceIndex/class (FM3 has PI and classes: from the
  car's DB row), fuel/boost/power/torque (may need more digging; 0 is acceptable to SimHub).
- `IsRaceOn`: 1 only while driving (not paused, not in menus or replays); FH1R's "driving gate" (pause, rewind,
  loading, menu) is the model.

### Plan (our own code, after the RE)
1. **RE (one local session, the user in a race):** scan memory for RPM / speed while holding revs with the virtual pad
   (session-18 recipe; compare with the telemetry page), `peek` around the hit, `FM3_WATCHW`/`FM3_WATCHRW` to find the
   physics code and the object's vtable; then the telemetry page's getters for the rest. A float scan in our debug
   tools is needed (the debug server's `find` searches text).
2. **`src/hooks/telemetry.cpp`:** a hook after FM3's physics step (or once per main-loop frame) fills the 311-byte
   packet from the player's car and sends it with a non-blocking Winsock UDP socket (`sendto`, never waits). Rate:
   FM7 sends 60 Hz; send at most 60–120 Hz even at a 360 fps cap. Off by default.
3. **Settings:** `settings.ini telemetry_udp=127.0.0.1:<port>` (+ `FM3_TELEMETRY_UDP`), launcher Settings toggle and
   port field. Loopback by default; another host allowed for a second PC running SimHub.
4. **Test without SimHub:** a small Python receiver in `tools/` that decodes the packet and prints fields (also catches
   wrong units); then SimHub's Forza Motorsport 7 profile with its dashboard.
5. Licences: the format is Turn 10's public documentation; write our own code (FH1R is GPL-3, ideas only;
   `forza-data-web` used only as a format reference).

## Benchmark tools: the player's car driven by the game's AI, and a frozen grid (session 21, 2026-11-10)
**Asked (user):** (1) let FM3's own AI ("drivatar") drive the player's car, for a benchmark with no input that is the
same every run; (2) stop the other cars on the grid from moving, for performance and visual tests without the pause
menu. This cloud session has no `out/ppc` or game image, so nothing below is checked against FM3's code yet: these are
the approaches, the evidence from other ports and our own code, and the first local steps.

### What the other ports do
- **FH1-ReImagined (FM4 engine, TU4): the native race AI drives the player car** (`forza-recomp/src/platform/
  benchmark.cpp`, `FORZA_BENCHMARK_AI=1`, docs/benchmark.md). Two hooks: (a) when the race creates the player's
  race container, they create it through **the opponent factory** instead (`sub_82A5D748`), so the player car also
  gets a native race driver (AI state machine + behaviour object), keeping the container's category 0 (player)
  because changing it broke the car's script bindings; (b) each tick they find the player vehicle in the race's
  car list (container +0x18 → AI, +0x28 behaviour vtable check; vehicle +0x3B80 controller) and, while the vehicle's
  control mode (+0x3B88) is 1 (human override), call **`sub_8250B050(vehicle, 1)`, which releases the human override**:
  the game's normal update then runs the AI and physics for that car. They measure 15 s settling + 60 s from guest
  VdSwap intervals (average, 1% low, P99, `frames.csv`, `result.json`), on a copied profile so the real save is
  untouched. Their own caveat: native AI and collisions vary, so it is a fixed event, not a deterministic replay.
- **FH1R also has a line-following autopilot** (`src/debug/autopilot.cpp`, `FORZA_AUTOPILOT=<line file>`): host code
  steers the virtual stick at a point ahead on a recorded line (pure pursuit) and holds the line's speed with the
  triggers, from the car's position (telemetry). Game-independent, but needs the car's position and a line per track.
- **FM2: a scripted benchmark** (`benchmark.cmd`): a pad-script menu tour (`FM2_TOUR=...,RACE:34,QUIT`) into a race,
  holds the throttle for 25 s, writes average fps, lows, CPU/GPU to `out\benchmark.txt`, with its own Benchmark profile.
  Also `FM2_RECORD_INPUT` / `FM2_REPLAY_INPUT` (pad input recorded and played back) and `quickrace.py` (FM2's
  `launch=true` dev command line straight into a race, with `numaicars`, `aiskill`; FM3's `launch` stalls in retail).

### (1) The player's car driven by FM3's AI: options, best first
1. **Find FM3's "hand the player car to the AI" call (FH1R's method).** Forza games let the AI drive the player's car
   after the finish line (the cool-down while results show): if FM3 does too (the user can check: finish a race and
   let go of the pad), that code path is the takeover, already built into the game. Find it locally: a write
   watch (`FM3_WATCHW`) on the player car's control-mode field, or by finding the finish handler, then call it at the
   start of the race. Does the player car have an AI driver object? In FM3 the opponents get theirs in
   `sub_822A1150` (our `ai_drivers.cpp`: driver row from `AIPlayers`); if the player has none, create one the same
   way (FH1R needed the opponent factory for that). **Effort:** 1–2 sessions of reverse engineering. **Result:** the real
   race AI on the player car, with every AI car racing too: the full race load.
2. **Developer command line options.** Turn 10's 270 options (`xref.py options`) may include one for an AI player car
   or an autopilot (FM2's parser had `aiskill`, `numaicars`). First local step: `python -I tools/analysis/xref.py
   options ai`, `... options auto`, `... options drive`, `... strings Drivatar AutoDrive Autopilot AIControl Hire`,
   then test candidates with `FM3_CMDLINE` (many are cut from retail, as `launch` and the overlays were).
3. **Host autopilot (FH1R's line follower).** Needs the player car's position and heading (a pointer chain, found
   with `find`/`peek` and watchpoints) and a line per track: record one lap of the user's driving (position + speed
   every frame) on the benchmark track, then follow it. FM3's own driving line (the assist) could be the line
   source if its data is readable. Independent of the AI's internals; ~1–2 sessions; less "real" (no racecraft).
4. **Recorded pad input** (FM2's record/replay, through our existing `FM3_TOUR` pad-script path in
   `imports_xam.cpp`). Simple, but not repeatable over a lap: the frame time varies, AI cars react, the car drifts
   off the recording within seconds. Fine for "hold the throttle down the first straight", which is what FM2's
   benchmark does.
5. **The post-race replay.** A replay plays the race back exactly, with every car, so it is the most repeatable
   moving scene the game has. Check whether FM3 can save a replay and load it later (the replay stream goes to
   `cache:\replay_stream`, FM3-QUIRKS Q8); if only the last race's replay is kept, a benchmark could drive one
   race and then measure its replay.
**Consistency:** the AI varies run to run (FH1R saw it). For A/B numbers, interleave runs and use the median, or
measure the replay (5) instead. A fixed seed for the game's `rand()` (used to pick AI drivers, `sub_8229F848`) may
make the AI more repeatable: worth a try once (1) works.
**Benchmark shell (any option):** start with our `quick_race` route (`FM3_TOUR`) or a fixed career event, a copied
profile (as FH1R: never the user's save), settle 15 s, measure 60 s from guest swap times, write a result file
(average fps, 1% low, P99), launcher button later.

### (2) Freeze the grid without pausing: options, best first
1. **Stop game time in our frame-time hook (all cars, and the world, stand still; rendering continues).**
   `src/hooks/frame_time.cpp` already sets the main loop's frame time (`f31` after `sub_821F6AF8`, at frame rates other
   than 60). A live switch (e.g. `FM3_FREEZE_TIME=1`, `Fm3 "env FM3_FREEZE_TIME 1"`) would hand the game dt = 0 (or a
   tiny value): physics, AI, the countdown, animations and particles stop, but the render thread keeps drawing
   full frames: no pause menu, no blur, the HUD as in a race, constant draws. Evidence it is safe: the comment in
   that file says the game's own millisecond dt is sometimes 0 above 60 fps, so the game already copes with dt 0
   (check that a frozen car's sounds and the camera stay sane; use 1e-6 s if 0 misbehaves). **Effort: small (an
   hour).** Also gives a free slow-motion switch (dt scale) for visual debugging. It freezes the player too, so it
   suits fps and picture A/Bs (same frame every time) more than a driving benchmark.
2. **Hold the race at the countdown.** Before "GO", every car sits on the grid with engines running, crowd and
   flags animated. Find the countdown (its timer, or the race-state change to "racing": a watchpoint on the race
   state while the countdown runs, strings like `Countdown`/`RaceStart` with `xref.py`) and keep it from finishing.
   A lifelike scene (moving crowd, engine sound), but not frame-identical. ~1 session.
3. **Only the AI cars stop, the player drives on.** Zero the AI drivers' throttle and hold their brakes (their control
   outputs, found from the AI driver object of option (1)), or give the AI cars zero speed each frame. Needs the
   same RE as (1). Useful for "drive past parked cars" visual tests.
4. Dev options (`xref.py options` for `freeze`, `noai`, `pause`, `numai`): check first, cheapest if one survives in retail.
**Recommended order:** (2.1) first (small, our own code, immediately useful for every perf/visual A/B); then the
dev-option search (1.2/2.4, one local run); then (1.1) for the real AI benchmark, which also gives (2.3).

### Controlling the AI cars' throttle, brake or clutch (session 21, user asked)
**Not checked in FM3 yet** (no `out/ppc`, game image or ReGPU source in the cloud container; our own `src/` has no FM3
car or physics address yet: the wheel code only feeds the pad). **Likely possible**, from FH1-ReImagined (FM4 engine,
FM3's successor):
- **Every car's processed controls are five floats in its car-dynamics object**: gas, brake, steer (−1..1), clutch,
  handbrake at **+3904** (`forza-recomp/src/platform/car_physics.h`, `ICarDynamics`, vtables `82003EEC`/`8223759C` on
  FH1's disc build; engine speed +324, gear byte +2240, speed +3128, wheels from +3936). FH1R's telemetry reads the
  player's; "remote-car exclusion" in their tests means the other cars have the same object.
- **Who writes them:** each vehicle has a controller (vehicle +0x3B80) and a control mode (+0x3B88: 1 = human
  override, 0 = AI). The human path fills the floats from the pad, the AI path from its driver; the physics substep
  (`sub_82D4DE30`, run by its substep loop) reads them. So **one set of fields serves human and AI cars alike**, which is
  the lever: overwrite an AI car's gas/brake/clutch after its AI wrote them and before physics reads them.
FM3's car physics is the direct ancestor of FM4's, so the same shape (a float controls block per car) is very likely;
the offsets and addresses will differ and must be found again.
**How to find it in FM3 (one local session; the game running, the user on a race grid or in free roam):**
1. **The player's controls block:** hold the throttle at known values with the virtual pad (`Fm3 "trigger R 0.25"`,
   then `0.45`, `0.55`; the session-18 engine-sound recipe) and scan guest memory for the big-endian float each time,
   keeping the addresses that follow (processed throttle may be curved: also scan for "changed in the same direction").
   Clutch the same way (`Fm3 "hold LB"` is the clutch in FM3's layout: 0 → 1). Our debug server's `find` searches text;
   a float/byte scan is a small addition (e.g. an `FM3_SCAN_F32=<value>` live switch in our code that logs matching
   addresses in the guest heaps, with a "keep only these" second pass).
2. **Neighbours:** `Fm3 "peek <block> 64"` should show gas, brake, steer, clutch, handbrake side by side (check with the
   pad: steer the stick, pull LT, press the handbrake).
3. **Writer and reader:** `FM3_WATCHW=<gas address>` + `FM3_WATCHRW=1` gives the guest call stacks of the pad path that
   writes it and the physics that reads it. An AI car's block (same offset in its own object: find the objects from
   the reader, which runs once per car) shows the AI's writer.
4. **The hook** (a `PPC_FUNC` override like `src/hooks/ai_drivers.cpp`): wrap the AI writer (or the physics step) and,
   for every car that is not the player, set gas 0 / brake 1 (cars hold still on the grid), or clutch 1 (engines rev,
   cars stay: a lively frozen grid), or scale gas (slower AI). Live switch, e.g. `FM3_AI_CONTROLS=hold|clutch|scale:0.5`.
   Tell the player's car apart by its control mode (FH1R: +0x3B88) or by the pointer found in step 1.
**Caveats:** the AI may fight a held brake (its driver logic can count a stuck car and reset or respawn it after some
seconds: watch for that); the countdown may hold the cars anyway until "GO"; FM3's AI may ignore the clutch with an
automatic gearbox (the physics probably still applies the float: check). Whatever we set, write it after the AI runs
each substep, or the AI simply overwrites it.

## FH1-ReImagined's latest performance work, and Sonic Unleashed Recompiled (session 22, 2026-11-10)
Read only, cloud session, no code. FH1R = `FH1-ReImagined/ForzaHorizon1-ReImagined` main `8afc41e` (2026-11-09), its
ReGPU fork `FH1-ReImagined/ReGPU` branch `fh1` (`ff39bea`), upstream `zer0ducksgiven/ReGPU` main `3fc8576`;
Unleashed = `hedge-dev/UnleashedRecomp` `cf829a9`. FH1R is GPL-3 and Unleashed GPL-3: ideas only, our own code.
Earlier FH1R findings (up to `c1ad9cc`, #607 step 2) are in the sections below and in PERF-PLAN; this covers what is new.

### FH1R since our last read (2026-11-05 → 11-09)
Their speed work is still almost all in their own Vulkan/Xenia backend (ReIGlue patches), so these are ideas to
rebuild in ReGPU, not code to port. Measured on a 7800X3D + RX 9060 XT (CPU-bound, uncapped) and a GTX 1650 Ti laptop.
1. **Command processor on two threads, on by default with ≥6 physical cores** (`0fdb852`, ReIGlue 0420, `docs/perf.md`
   "Decoding and recording on two threads"): a decoder parses PM4 against a shadow register file and records the
   register writes and the draws; a recorder applies them and runs the draws. Benchmark +13% (8 cores) / +5.8% (6 cores),
   free roam +15% (6); **4 cores without SMT: −11%** (gated by physical core count, `src/platform/cpu_cores.h`).
   ReGPU's draw thread (`REGPU_DRAW_THREAD`, FM3's default) is the same split. **Two details we don't have:**
   - **Only changed register writes are recorded** (`gpu_split_elide_unchanged_registers`, on): ~30% of the
     360k–530k register writes a frame are changes. Applies to what ReGPU's CP hands the draw thread (check what it
     copies per draw). CPU-bound only.
   - **Direct3D's coherency waits (`WAIT_REG_MEM` on `COHER_STATUS_HOST`, ~100 a frame) are answered from the shadow**
     without waiting for the recorder; memory waits poll each finished batch (≤100 µs) instead of sleeping to a vblank
     (their first build halved fps by sleeping). Check how ReGPU's CP answers these with the draw thread on.
2. **Descriptor reuse** (`0fdb852`, ReIGlue 0442–0445, "Descriptor reuse in the command processor"): constants as
   dynamic buffers with per-draw offsets (−4% per draw, the biggest part), unchanged samplers/texture requests skipped
   (−2%), texture sets memoised by exact contents per frame (≈0 alone). Together −7.4% CP time per draw, 133 → 147
   fps. A compare-with-previous-draw-only version drew wrong textures and was removed: memoise by full contents.
   Already PERF-PLAN L8; this gives the order (constants first).
3. **The swap no longer waits for the frame's replay** (`6e891d4`, now default; DLSS/VR/macOS keep the wait). Their
   +13% above includes it. PERF-PLAN L12.
4. **GPU time by kind of work** (ReIGlue 0460, `--fh1_gpu_profile`, "Where the GPU frame goes on a GTX 1650 Ti"):
   timestamps where the kind of work changes, reported as guest draws / render-target transfers / texture loads /
   resolves / uploads. **The surprise: render-target transfers (the same EDRAM bytes reinterpreted between MSAA modes
   and formats: shadow maps at 4x read as 1x, scene depth) were 5.3 of 17.7 ms, 37% of the GPU frame at render scale 2.**
   Their fix (stencil bit draws made conditional, NVIDIA only; direct texture upload at scale 1): 19.0 → 23.7 fps at
   scale 2. **For FM3:** our `REGPU_GPU_PASSES` times passes between render-target switches and resolves; check that
   ReGPU's own non-draw GPU work (EDRAM surface copies/reinterprets, MSAA resolve blits `psColor`, depth blits, upload
   copies, 1440p 4x) is in its own bucket, and measure that share on the race grid before L4/L6/V3. FM3 also draws
   at 4x MSAA with 2 EDRAM tiles: any per-tile copy scales with render scale.
5. **Main loop waits while a render pass is held >1 s** (`6b2e9d2`, #725): with a slow GPU (cold shader caches on
   an Intel UHD, WARP) the main loop free-ran ~1,300 frames, and FH1's HUD command-buffer ring (12 slots) wrapped:
   null read crash or no frame. Correctness on slow machines, not speed. FM3 has never run with seconds-long frames;
   keep in mind for iGPU first runs (OPEN-ISSUES-style crash with a cold cache).
6. **The ReGPU plugin in FH1R** (`416ad22`, `e4d18f8`; `forza-recomp/regpu-plugin/src/regpu_graphics_system.cpp`
   ~776–845). Switches it sets, with their measurements:
   - **`REGPU_ANISO_FROM_FETCH`**: ReGPU filters every linear-wrap texture at 16x anisotropic; this uses the
     fetch constant's own anisotropy (FH1: none on most ground, 2x–8x on some, what the console and Xenia give;
     16x made distant ground sharper than stock). **In our pin, never tried on FM3** (we set the launcher's
     anisotropy with `regpu::SetAnisotropy`). On a GPU-bound 1440p 4x frame 16x everywhere costs texture bandwidth:
     a cheap start-time A/B (PERF-PLAN I8). The picture moves toward the console's, softer at a distance.
   - `REGPU_NO_SETTLED_SKIP` (FH1's paint-job bake was skipped while its pipeline built). Only matters with
     `REGPU_ASYNC_PIPELINES`, which FM3 doesn't set; GameSettled also gates the draw workers (not FM3's default
     path). Note for VINYLISSUES if async pipelines ever get turned on.
   - `REGPU_NO_TILE_MERGE` on: with the stencil fix, merging FH1's three tiles **saved nothing** (FX 31.5 vs 32.8
     fps). Lowers expectations for PERF-PLAN L10.
   - `REGPU_NO_FETCH_BOUNDS` (skid marks read past the fetch constant's size and stretched to the origin): a
     correctness switch to remember if FM3's tyre marks ever stretch.
   - **PGO build of the plugin** (CMake `FORZA_REGPU_PGO`, clang `-fprofile-instr-use`, a 60 s free-roam profile
     shipped in the repo): +6% (99.3 → 105.5 fps). PERF-PLAN L23.
   - Their numbers: ReGPU on the 1650 Ti laptop 36 vs Vulkan 52 fps in the benchmark, GPU only 63–82% busy at
     35–41 W with ReGPU: "ReGPU's limit there is its CPU side". At the menu the main loop took 18.8 ms on ReGPU vs
     7.3 on Vulkan. ReGPU's CP side is what weak machines will hit first, also for FM3.

### Upstream ReGPU after our pin (57143cb → 3fc8576, 15 commits; our pin is an ancestor of main)
- `5c493d1` **vertex reach**: each draw copies only the vertices it reads (a spark draw copied its whole 2.4 MB
  particle ring; FX p99 80 → 28 ms on a wall hit, `REGPU_NO_VERTEX_REACH`), and the recorder **submits at every new
  RTV/DSV** instead of only at 64 KB (+3.8% parked, `REGPU_NO_VIEW_SUBMIT`). FM3 sparks and tyre smoke: a stutter
  lead; the vertex arena (L1) may already cover the big buffers.
- `99403c0` occlusion queries recorded with the draw workers, **the frame's counts resolved in one copy** (PERF-PLAN
  L7; our `regpu-worker-queries` / `regpu-zpd-tile-spans` patches overlap).
- `16b389b` + `8a2ae2d` fences land after the host GPU (exploded geometry when told early), interrupts don't wait for
  them (London grid 86 → 147 fps). FM3 runs early fences with our write-back fix: read before rebasing.
- `73b05f9` + `7a2c6b6` texture budget 1 GB with eviction (PERF-PLAN L17), `e25824f` DXT cube maps never take
  resolves as faces (FM2 showroom bug), `6e840c7` `regpu::SetMsaa` / `SetFullscreen` live (launcher/in-game options).
- **Rebasing our 21 patches** onto main is the way to get these; a session of its own (patch conflicts with
  zpd/worker queries and fences likely).

### Sonic Unleashed Recompiled (hedge-dev): what is different
Unleashed doesn't emulate the GPU: it **replaces the game's Direct3D calls** with its own renderer
(`UnleashedRecomp/gpu/video.cpp`, plume over D3D12/Vulkan; shaders converted offline by XenosRecomp). Not a model
for ReGPU (a PM4 emulator), but four ideas carry over:
1. **Hardware MSAA resolve** (`video.cpp` ~3378): `ResolveSubresource` when the source is multisampled and the
   formats allow (depth only on D3D12 with programmable sample positions; off on Intel, where it misbehaved), a
   shader resolve otherwise. ReGPU always resolves 4x MSAA with a shader that averages 4 `Load`s (`renderer.cpp`
   ~1652 `psColor`), even when scale is 1 and there is no clamp/gamma/swap. A hardware resolve to a 1x RGBA16F
   intermediate in the plain case: lossless (same box average), GPU-bound, likely small; worth it only if the GPU
   profile (item 4 above) shows resolves matter. PERF-PLAN L24.
2. **Pipelines compiled when an asset loads, behind its loading screen** (`EnqueuePipelineTask(DatabaseData)`,
   a custom "compiling pipelines" flag holds the game's `IsMadeAll` until the asset's pipelines are built): no hitch
   the first time a model is drawn. FM3 version: hold the race load (or a car load in the showroom) until the
   pipelines of the cars and track just loaded are built. Needs FM3's load-done flag; goes with L16 (static shader
   scan + seed). Stutter only.
3. **Low-end defaults on detection** (`ApplyLowEndDefaults`, ~1815): non-discrete GPU, under 2 GB VRAM, or D3D12
   UMA → lower settings at first run. Same as PERF-PLAN V4.
4. **Recompiler register options** (`UnleashedRecompLib/config/SWA.toml`): `skip_lr`, `skip_msr`,
   `non_argument_as_local`, `non_volatile_as_local` all **true**; FM3 has them **false** (`config/fm3.toml`: hooks
   and diagnostics read GPRs/LR from the context). Unleashed hooks through XenonRecomp's **mid-asm hooks**, which get
   the registers they name as arguments, so the rest can live in host registers. For FM3: turn on
   `non_volatile_as_local` (r14–r31, the most used) after checking which hooks and the thread sampler/caller
   walk (`FM3_SAMPLE_CALLERS`) read r14–r31 or LR from the context, and moving those to mid-asm hooks. CPU only
   (guest code: render thread t7, main loop); the user's PC is GPU-bound. **Dropped: Pinyon Shift measured
   `non_volatile_as_local` 20–27% slower (section below); PERF-PLAN L25 dropped.**
Also seen and already ours: GPU upload heap (ReBAR) for buffers (our pin uses `D3D12_HEAP_TYPE_GPU_UPLOAD`), dirty
state tracking per draw, alpha test as a shader specialisation (our `regpu-split-alpha-test`).

### What to do with this (ranked for FM3)
1. **GPU work by kind on the race grid** (extend `REGPU_GPU_PASSES` so copies, resolves, MSAA blits and uploads get
   their own buckets; the FH1R lesson is that non-draw GPU work can be a third of the frame). This decides between
   L4/L5/L6/L24/V3. Small ReGPU patch, start-time switch.
2. **`REGPU_ANISO_FROM_FETCH` A/B** (I8): one start-time switch, GPU-bound; check distant road and kerbs with pngdiff.
3. **Rebase on upstream ReGPU** (vertex reach, view submit, one-copy queries, texture budget, live MSAA/fullscreen).
4. CPU side for other machines: elide unchanged register writes on the CP → draw-thread handoff, coherency waits from
   the shadow, descriptor reuse (constants first), PGO (not `non_volatile_as_local`: a loss in Pinyon Shift).
5. Stutter: pipelines built at asset load (Unleashed), with L16.

## Pinyon Shift (FH1 recomp): performance leads (session 22, 2026-11-10)
Read only. `arcanite24/pinyon-shift` (`620e7c4`, release 0.8.2, 2026-11-07; BSD-3, so code may be adapted with its
notice), on ShiftGlue (`arcanite24/shiftglue-sdk`, its own ReXGlue/Xenia Vulkan fork, not ReGPU). FH1R took its split
command processor from here. Sources are its `docs/` (PERFORMANCE_BACKLOG, NEAR_NATIVE_RESEARCH, NEXT_LEVEL_RESEARCH,
RECOMP_OPTIMIZATION_SURVEY, LOW_SPEC_BACKLOG, AOT_PIPELINE_BACKLOG, ANDROID_ODIN_BACKLOG, DESKTOP_RENDERER_BACKLOG,
DEPTH_ALIAS_RESEARCH, MODERN_RENDERING_RESEARCH) with the commits named. **On desktop their GPU was never the limit**
(RTX 4080: SMs 20% busy, DESKTOP_RENDERER DR-0.1), so their GPU-side numbers come from GPU-bound Android/Deck/UHD 620
runs, which is the case that matches our 7900 XTX at 1440p 4x.

### GPU side (for the user's PC), checked against our pin
1. **Fetch state as shader variants** (ShiftGlue `efcbb82`, texture signedness as a specialisation constant; Odin 1x
   45.8 → 43.8 ms, ~5%, byte-identical; PB-1.8(a) lists the same kind of per-fetch waste). **Applies to ReGPU:** our
   `TFetch` (`hlsl.cpp` ~258) reads `tinfo` at run time on every fetch and branches on gamma-signed components, the
   exponent adjust and the swizzle; `TFetchRaw` switches on dimension and clamp/mirror modes. Variants keyed on the
   bound fetch constants' bits (as `regpu-split-alpha-test` keys on the alpha test) would turn those into constants.
   Car materials average ~4 fetches. Lossless. Start-time (shader cache key). Medium. PERF-PLAN L30.
2. **Per-shader price list before optimising** (Pinyon: flat pixel shaders, per-shader timestamps, knock-out by hash;
   each shader also has a "flat bound" of 0.27–0.66 ms that no edit removes: fetch and output bandwidth). Ours:
   `REGPU_GPU_SHADERS` + `gpudiff.py`, plus `Fm3 "ps <hash> float4(0,0,0,1)"` per AI-car material on the paused grid
   gives each one's ceiling. Do it before I3/L30/L5.
3. **The 0×Inf = 0 rule per shader, with one NaN scrub per output** (`b780677`): −0.02 to −0.09 ms per hot shader,
   byte-identical; globally "fast pixel math" was −10% on an Adreno 740 and noise on desktop. **Risk they hit:** pure
   IEEE everywhere smeared motion blur through a NaN (#403, `2b10c4a`), and PB-1.8 names HDR **paint and glass** as the
   0×Inf-sensitive shaders: that is our car material. Our I3 (`REGPU_PLAIN_MUL_PS`, global) should become per-hash
   with a final `isnan → 0` on each output, and be checked by pixel diff on the car paint shaders first.
4. **Depth alias copies and stencil transfers.** Pinyon: sharing one depth image between a 1x view and its 4x alias
   (draws that don't care which they use render into the 1x one) cut alias transfers 5 → 2 a frame, −1.09 ms on the
   4080 at 3x/4x (DEPTH_ALIAS_RESEARCH R01); depth+stencil in one pass with stencil export instead of 8 bit passes:
   35 → 28 ms on a Snapdragon (`3ee6536`). FH1R saw the same class of cost (30–37% of its GPU frame). For FM3: count
   ReGPU's EDRAM ownership copies per frame (the two tiles, the sun/occlusion passes): PERF-PLAN L26 measures it.
5. **Specialised resolve pipelines** (`38d55bc`/`3d69b06`: one pipeline per format/sample count instead of a generic
   shader with switches; Turnip resolves 4.83 → 3.59 ms, 4080 at 2x −0.4 ms, byte-identical). Ours: one `psColor`
   with run-time `scale/clamp/gamma/swapRB` branches averaging 4 `Load`s. Goes with L24 (hardware resolve).
6. **Reflection cube redrawn less often** (`21dc1b1`, FH1's `EnvMapFrequencyScale` 0.25: Odin 25.4 → 23.2 ms, "looks the
   same"; 0 left reflections dark) and reloading only changed cube faces (0.20 → 0.04 ms, lossless). FM3 has the car
   reflection cubes (session 9 showroom work): find the game's update rate (OPEN-ISSUES L tools, `xref.py options`).
   Visible in principle. PERF-PLAN V8.
7. **Barrier census:** on Turnip ~14.6 ms a frame was in no labelled span (drains between ~300 barrier batches);
   resolves writing textures directly took that to ~200. ReGPU already counts barriers (`REGPU_TRACE_BARRIERS`):
   log batches per frame on the grid next to L26.
**Checked, not applicable:** our pin has no per-sample shading (`SV_SampleIndex`/`sample` interpolation absent;
`SV_Depth` only for shaders that export depth, the depth-restore copies), no geometry shaders (quads are expanded by
index, which they measured as the fast way), no `SampleGrad` (implicit LOD already).
**Measured losses there (don't repeat):** skipping no-op draws (FH1 120 a frame: noise), HDR targets as B10G11R11
("shader-bound, not bandwidth": 0; bears on V3), RelaxedPrecision/spirv-opt/no robustness, shadows every other frame
(trail visibly), 4x → 2x + FXAA (fails their 40 dB picture floor), render scale below 1x on a fast GPU (0%).

### CPU side (other machines; FM3 is GPU-bound on the user's PC)
1. **Per-thread profile, then small game hooks** (their cheapest big wins): FH1 called D3D `SetPWLGamma` every frame
   with an unchanged ramp, ~30% of its render thread (`ffe6407`, skipped when unchanged); an empty array-free loop was a
   fifth of a streaming worker (`8d9030b`, mid-asm hook); their own hooks' `VirtualQuery` per pointer 6% → 1.1% with a
   page cache (`a722b98`). FM3 uses the same Xbox D3D library: check whether t7 calls `SetPWLGamma`/`SetGammaRamp`
   every frame (session 20 profile: ~33% of t7 is game work) and audit `src/hooks` for per-call `VirtualQuery`.
2. **Register save/restore inline** (NEAR_NATIVE NN-1.2): `__savegprlr_N`/`__restgprlr_N` were ~31% of all guest
   calls; copying between `ctx` and locals instead (22,060 of 22,968 functions; helper kept where the slots can be
   read) gave sim −4 to −6% instructions, render −7% per draw, `.text` +2.2%. A XenonRecomp `recompiler.cpp` patch
   (we carry 5). Medium.
3. **Fewer flush-mode (MXCSR) toggles** (ShiftGlue `99b7966`: carry the known mode across labels, −29% toggles, audio
   −6%; NN-1.1: threads that never change FPSCR skip the checks, ~−3% CPU). XenonRecomp emits
   `enable/disableFlushMode` per FP/VMX class change: same change. Small–medium.
4. **`non_volatile_as_local` measured as a loss** (NL-1.4: every correct variant 20–27% slower, `.text` 55.6 → 92.3 MB;
   the first crashed because functions read a caller's registers through `ctx`). This **overrides the Unleashed
   lead above** (PERF-PLAN L25 dropped). Also lost: CR locals synced at every call, plain (non-volatile) stack
   accesses (108–110 vs 117–119 fps), ThinLTO (neutral), devirtualisation (0.3–0.7%), MMCSS "Games" + 1 ms timer
   (worse), PGO on their GPU plugin (0; on the game −0.7% after a retest; FH1R's ReGPU PGO measured +6%).
5. **Waits:** the fence-predicate hook (spin 20 µs, then block on a "GPU wrote" signal, capped at 1 ms so the game's
   own timeout logic runs) cut process CPU 186 → 159 s with **frame time unchanged**; spin length made no difference.
   Windows `Sleep`/`SleepEx` below 1 ms rounds to a yield: use `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (`fc39661`).
   Sleeping waits won at ≤60 fps and **lost 2.4–4.5 fps at 120 fps** to a yield (LOW_SPEC LS-2.2): gate L11 by frame
   rate (our cap is 360).
6. **Frame pacing** with the sim at every other vblank (our `mainthread30fps` scheme): late swaps flipped at once when
   two guest vblanks passed (`64983a2`, p95 13.07 → 10.48 ms); vblank on a self-calibrating high-resolution timer
   (`90be82c`, lateness ~600 → 43 µs); hold the vblank while the main loop is two steps ahead (`ee41d07`, frames with
   exactly one step 87 → 98.6% at 120 fps). Check `src/hooks/frame_time.cpp`: log steps per swap. PERF-PLAN L31.
7. **Guest-native replacements with a verify mode** (`7c7d469`/`f8566cf`: strong `sub_X` overrides the weak one after
   a code-hash check; verify runs the original every Nth call and compares). CRT `pow` 2.2x, LZX 1.8–2.2x: load time
   only. Fits XenonRecomp's weak symbols; only if a profile shows a hot leaf.

### Stutter and low-spec
1. **Wait for missing pipelines at submit, not per draw** (`52b6374`: a cold drive's worst frame 305–416 → 145–166 ms,
   frames over 100 ms 6–7 → 2; warm unchanged): a frame's new pipelines build in parallel. Check whether ReGPU's draw
   thread blocks per draw (`GetPipeline` wait). PERF-PLAN L32.
2. **Hitch sampler that fires only on long frames** (`b6fd36f`: samples the title and named host threads every 1 ms once
   a frame passes N ms; named all three causes of a 28-minute session). Make `FM3_SAMPLE_THREAD` frame-time triggered.
   Small; do it first for any stutter work. PERF-PLAN L33.
3. **The game's SQLite as a hitch** (`622461e`: one view after each race took 158–186 ms; four indexes and the view
   rewritten with plain joins, same rows: 49–55 ms). FM3 runs on `gamedb.slt` too (session 15, crash C1 work): profile
   post-race and menu frames with item 2.
4. **Pipeline list as "pairs × states seen together"**, built on workers behind the title screen, with the driver cache
   loaded before the first creations (`cdde20b`); starting play before it's done was rejected (first-race stutter
   back). Even with every pipeline stored, world entry and race load still had spikes (NL-3.6). Extends our manifest.
5. **Runtime locks and per-use allocations** (Odin): file lookups under the global lock (up to 39 ms), a buffer made
   and freed per readback, textures in 64 MB blocks as they appear (5–20 ms each), a pipeline-cache save holding the
   command thread 30–40 ms every 30 s (`4d59c26`), a readback waiting for the whole queue (`a747943`). Audit ReGPU's
   cache saves and `imports_io` (mods overlay) the same way.
6. **Low-spec defaults and a laptop warning:** iGPU, under 6 GB VRAM or under 6 threads → MSAA off, 60 cap (LS-1.5).
   A GeForce 930MX rendering for an Intel-driven display stalled 550–700 ms every 6–8 frames, and the UHD 620 alone ran
   better (12.5 vs 6.8 fps): "always the dGPU" (L15) can be wrong for weak dGPUs. Losses: crowd, grass, traffic and
   post-effect cuts saved nothing measurable.
7. **Memory:** render surfaces sized to the rows actually used: at 3x 2,388 → 635 MB (NL-3.5). Texture budget rarely
   bound. At 1440p 4x our render targets probably dominate VRAM: measure by category before L17.

### Method worth copying (their benchmark)
An in-run A/B that flips the setting every 180–300 frames on a scripted route and scores disjoint A-B-A blocks with a
t-interval (adjacent pairs understated the error ~1.45x); frames banded by draw count; p95/p99/worst/over-25/50/100 ms
reported, slow frames never filtered; warm-cache runs count "draws that waited for a pipeline" instead of fps; every
run saved as JSON with commit, machine and effective settings. Our `tools/fm3_ab.ps1` does interleaved rounds: adding
the block t-interval and the tail metrics is cheap.

## ReGPU speed work in other projects (session 19, 2026-11-10)
Survey of every branch of the reference clones (framework/ReGPU, ReGPU-fh1, FM2, PGR4, FM4, FH1R), read only. Our pin
(57143cb, a squash) already has rexglue-driver and most of engine-optimisation (draw workers + early fences, pipeline seed,
VS precompile, lean fetch v1, SIMD hashing, sampled upload reuse, lean ALU, ...). None of the FM2 "Turbo/Hyper" code is
public (session 19 implemented our own: patch regpu-turbo-hyper). Candidates, best first for FM3:
1. **FM2 vertex arena** (PORTED in session 19: patch regpu-vertex-arena) (`release-0.9.10`: a6edeb7, 0c9a747, 620a77c, 816898e, d430385, f893212; `origin/main:src/gpu/renderer.cpp`
   `struct VertexArena` ~2466): large vertex buffers kept in a 512 MB VRAM arena, recopied only after a guest write (write-watch:
   read-only pages, first write faults). FM3 logs "zero-copy vertex fetch": the GPU reads vertices over PCIe every draw. FM2:
   1.5 ms a frame on a 4070 at 1440p. Needs WriteWatch in our `src/memory.cpp` and an `NtReadFile` tweak; FM2 hit a tyre-mark
   bug with it (d430385). Less gain on iGPUs.
2. **Occlusion results resolved once a frame** (PGR4 vendored ReGPU, `REGPU_RESOLVE_EACH_QUERY` = old): ~3% on PGR4's grid.
   Touches our zpd-tile-spans and worker-queries patches.
3. **All samplers as a 4-entry table** (FM2 ce5bf5a): every fetch path 2 sample instructions instead of ~8; 4070 1440p
   213.8 -> 225.2 fps. Root signature change.
4. **Texture eviction budget** (PGR4 `EvictTextures`, `REGPU_TEXTURE_BUDGET_MB`, FM2 `FM2_TEXTURE_CACHE_MB`): for low-VRAM presets
   and long sessions.
5. **SMAA 1x** (FM2 9b6b512; ReGPU form in FM4's vendored copy, `smaa_passes.hlsl`): cheap AA for low-end presets.
6. **Laptop GPU pick** (`REGPU_ADAPTER`, ReGPU-fh1 4ead774) or exporting `NvOptimusEnablement` /
   `AmdPowerXpressRequestHighPerformance` from fm3.exe.
7. **Ring wait sleeps instead of spinning** (FM2 480e94a, `FM2_SPIN_RING_WAIT`): render-thread CPU 100% -> 36%, same fps; for
   APUs. FM3's equivalent is t7's `sub_824B5048` -> `sub_824AAA68` wait (needs FM3 addresses).
8. `REGPU_OPTIMIZE_LEVEL` (engine-optimisation afb4495, untested upstream), `REGPU_FRAMES_AHEAD` (FM4, latency only).
Don't port: `SplitMainListAsync(false)` (rexglue-driver 0bc61d1; our pin reverted it: the AMD hang), FM4's RGBA32F surfaces,
FH1R's perf branches (ReIGlue/Xenia, not ReGPU; ideas only: a 540p scene under a 720p HUD for an iGPU preset).

## 16-bit (half-precision) shaders for lower-end GPUs (session 18, 2026-11-09)

**Question:** could an optional shader translation that does vertex shaders (and others) in 16-bit instead of
32-bit floats help lower-end hardware?

**Short answer:** possible as an opt-in for **pixel shaders only**, and only for values the translator can prove
don't need 32 bits. Vertex shaders in 16-bit would break the game. The likely gain is small next to lowering the
resolution with FSR, which the build already has. Read from ReGPU's translator at our pinned commit
(`thirdparty/ReGPU/src/gpu/hlsl.cpp`, `renderer.cpp`); nothing built or measured.

### What ReGPU does today
- Each Xenos shader is translated to HLSL and compiled with **FXC** (`d3dcompiler_47`) as **shader model 5.1**
  (`renderer.cpp` ~3367: `ps_5_1` / `vs_5_1`). Every guest register becomes a `float4 rN` (`hlsl.cpp` ~1215 for
  vertex shaders, ~1375 for pixel shaders).
- With FXC the only 16-bit option is **`min16float`**: a hint the driver may honour or ignore.
- Real 16-bit types (`float16_t`, `-enable-16bit-types`) need **DXC and shader model 6.2**. Moving ReGPU to DXC
  means DXIL output, a new shader cache and new compile times for every shader: a project of its own.

### Why vertex shaders can't go 16-bit
- **The vertex index travels in a float register:** `r0.x = float(vindex)` (`hlsl.cpp` ~1218), passed on to
  `VFetch(..., float index, ...)`. A 16-bit float holds whole numbers exactly only up to 2048; car models have tens
  of thousands of vertices, so fetches above 2048 would read the wrong vertices (exploded meshes).
- **Positions:** world/view positions on a track run to hundreds or thousands of metres; 16-bit keeps about
  3 significant digits, so geometry would jitter.
- **Constant indexing and loop counters** (`a0`, `aL`) are computed from floats in the translated code.
- Nearly everything in a vertex shader feeds the position, so even a careful version saves almost nothing there.

### Pixel shaders: possible, with exclusions
16-bit is usually fine for colour, lighting and blending math. Not for:
- texture coordinates on large textures (2048x2048 liveries need more than 11 bits of position within the texture);
- shadow-map depth comparisons (shadow acne);
- depth output (`SV_Depth`), kill and predicate comparisons, anything that feeds an index.

So the translator needs a small dataflow pass: mark every register that (transitively) feeds texture coordinates,
depth, `a0`/`aL`, predicates or kills as 32-bit, and declare the rest `min16float4`.

### What it would gain
- **NVIDIA** reports no minimum-precision support on desktop GPUs and runs `min16float` at 32-bit: a GTX
  1050/1650 or MX laptop chip gains nothing (and loses nothing).
- **AMD** (Vega, RDNA, incl. APUs such as the 780M) and **Intel** iGPUs (Gen 8 and later) can use it. Check at run
  time with `D3D12_FEATURE_D3D12_OPTIONS` → `MinPrecisionSupport`.
- **Only passes limited by shader math or register pressure gain.** Rough estimate from what other games report: a
  few percent to the low teens on such passes, nothing on passes limited by memory bandwidth or fill rate.
- **Proton / Steam Deck:** not known whether vkd3d-proton and the driver there use the hint.

### Better levers for low-end hardware (already in the build)
- Lower internal resolution with **FSR upscaling**: pixel cost drops roughly with the pixel count.
- **MSAA 1x**.
- The CPU side: on fast GPUs FM3 was bound by ReGPU's command processor (OPEN-ISSUES P); a slow 4-core CPU will
  likely hit that before shader math.

### If we build it
1. **Measure first:** on a low-end AMD or Intel GPU, profile a race (Radeon GPU Profiler or PIX) and check whether
   pixel-shader math is actually the limit.
2. **Opt-in switch in ReGPU** (game-neutral, per ReGPU's CLAUDE.md), e.g. `REGPU_HALF_PRECISION=ps`: pixel shaders
   only, with the dataflow pass above, active only when the GPU reports support.
3. **Separate shader cache:** the cache key (`ShaderCachePath`, `renderer.cpp` ~3313) must include the switch, or
   cached 32-bit and 16-bit shaders mix.
4. **Per-shader opt-out by hash**, and numeric screenshot comparisons as in session 8 (`tools/analysis/`):
   liveries, shadows, sun glare, reflections.
5. **Launcher option** ("Low-precision shaders") only if step 1 shows a gain.

## Music plays briefly, then not again for the session (session 18, 2026-11-09)
**Seen (user):** music plays for a very short while, if at all, and is not heard again for the rest of the session.
hl360 plays music normally, so the cause is in our build.

**Solved (session 18, 2026-11-10; user-confirmed in menus and races):** lead 2 was right, plus a second cause: the codec pool made the streamer's start loop give up, and the seek guard flushed the music codec between its one-packet buffers. Details: NOTES.md session log "music fixed". Lead 1 (APC) was not it. The rest of this section is the original study.

**Ruled out (code read):** the music-player stub (`XMPGetPlaybackController` in `src/kernel/imports_xam.cpp`
answers "title controls playback, not taken over": FM4's fix for menu music cutting in and out); the FM2 easter-egg
music mute (`DealerMutesMusic()` returns false in `src/hooks/common/diagnostics.cpp`); category volume
(`XAudioGetVoiceCategoryVolume` reports 1.0).

**Lead 1 (strongest): no APC support anywhere in our kernel.** hl360 (ReXGlue, from Xenia) runs completion
callbacks. Ours skips them and logs once:
- `NtReadFile with an APC routine is not supported` (`src/kernel/imports_io.cpp` ~373): if the music streamer
  reads each chunk with a completion callback, it never hears back after the first chunks.
- `NtSetTimerEx with an APC routine (...) is not supported` (`src/kernel/imports_ke.cpp` ~797): a periodic timer
  callback that never fires would stall it the same way.

**Check:** `Select-String -Path out\run\fm3.log, out\run\fm3.prev.log, out\logs\*.log -Pattern "with an APC routine"`.
A hit names the call; the fix is to queue the callback to the calling thread and run it at that thread's next
alertable wait (`KeDelayExecutionThread`, `KeWaitFor*`, `NtWaitFor*`, `NtSignalAndWaitForSingleObjectEx` with
`alertable` set), as Xenia does, with a log line per delivery.

**Lead 2: our XMA decoder drops the stream.** FM4's drop-ahead and seek-guard fixes (we carry both) restart the
decoder when the title clears a stream's input bits or a packet doesn't seem to follow; FM2 (which has neither)
found that music streams in packet pairs with a seek between them (`92420c5`). Live A/B while music is silent:
`Fm3 "switch FM3_NO_XMA_DROP_AHEAD 1"`, then `Fm3 "switch FM3_NO_XMA_SEEK_GUARD 1"`; watch the `xma: 5 s:` lines.
Also `set FM3_TRACE_READS=music` in `play.cmd` to see whether music-file reads continue or stop (assumes loose
`.xma` files under `Media\Audio\Music`, as `imports_io.cpp` expects; not checked for FM3).

## Local multiplayer / split screen (session 18, 2026-11-09)
Neither sibling port has it. **FM2** merges every controller into player 1 (`fcdb0f8`, `f332752`: a second pad
was a signed-out player 2, career locked) and states "FM2 has no split screen". **FM4** signs in one profile on the
controller that last pressed Start or A (`d90ae09`); our FM3 code is ported from it.

**Our state:** each XInput pad is its own guest user (`ReadPad`, `XInputIndex` in `src/kernel/imports_xam.cpp`),
so player 2's input reaches the game, but only one user is ever signed in (`IsSignedIn`). Never tested.

**First step:** start split screen with two pads and `FM3_TRACE_XAM=1`, to see whether FM3 accepts a signed-out
player 2 or needs a second signed-in profile (not known). Then the rendering side: two viewports per frame, a path
no ReGPU port has tested.

## Frame rate with AI cars: ~200 fps in races vs ~300 in time trial (session 18, 2026-11-09)
**Seen (user):** races with AI cars ~200 fps, time trial without AI ~300 fps (RX 7900 XTX, 1440p 4x MSAA, cap 360).
That is 5.0 vs 3.3 ms a frame: AI cars add ~1.7 ms. Sources: FH1R (`FH1-ReImagined/ForzaHorizon1-ReImagined`,
`docs/perf.md`, GPL-3: ideas only), PGR4-ReGPU, upstream ReGPU, the FM2/FM4 ports. Read only; nothing measured on FM3.

**What we already know (OPEN-ISSUES P):** FM3's frame rate near cars follows the draw count: ~1,900 draws alone,
3,200–3,600 with AI cars close (session 13); the game's render thread waits on ReGPU's command processor; at 1440p
4x MSAA the GPU was ~90% busy after the draw thread (session 14). 1.7 ms over ~1,500 extra draws is ~1.1 µs a draw.

**What the other ports found:**
- **FH1R: cars cost draws and nothing else** ("Steam Deck frame collapses", `cbb26d4`). Free roam 2,865–3,144
  draws; 8-car race pack 5,802 draws (+70–80%); each draw costs the same 1.7–1.8 µs whatever the scene; no
  per-car textures, pipelines, render targets or evictions. "There is no per-car overhead of its own to remove."
- **FH1R: the EDRAM tiles are the biggest cost.** FH1 draws its 4x MSAA scene in 3 tiles and the command
  processor replays the scene once per tile: tile passes were 56–60% of its busy time. Their one-pass scene
  (`scene_msaa=off`, a game hook) cut command-processor time 21%, GPU 13%, benchmark 119.5 → 137.4 fps (+15%).
- **PGR4: ReGPU's tile merge** (`d0204cd`): 3,270 of 8,100 draws a frame were the second tile's copies; dropping
  them took races ~63 → ~75 fps (+19%).
- Other cars also caused hitches in FH1R (new pipelines waited for at the frame end, `664d33a`; first readbacks of
  each car's 64x64 render targets, `bbddfee`); FH1R's AI/traffic cost on the CPU side is "the command processor
  (more draws) and the audio mixer (more cars)".

**The FM3 lead: our scene draws are recorded twice, AI cars included.** FM3 draws its 720p scene in two EDRAM
tiles (rows 0–511, 512–719). ReGPU has the tile merge (`renderer.cpp` `TileMerge`, on by default,
`REGPU_NO_TILE_MERGE=1` off), **but not with the draw thread** (`gpu.cpp` ~983: "Not with the draw thread yet:
merged there, the second tile's area came out unrendered"), and the draw thread is FM3's default since session 14.
PGR4's vendored ReGPU has the same restriction. So every scene draw of every AI car goes through the command
processor and draw thread twice. Never measured on FM3.

**Steps (ranked):**
1. **Find the bound** in a paused race with AI cars: 1440p 4x vs 1280x720 1x (`out/user/settings.ini`, back up
   first), `Fm3 "status"` fps and draws. If 720p is much faster, the GPU is the limit at 1440p, not draws.
2. **Does the tile merge engage without the draw thread?** `set FM3_NO_DRAW_THREAD=1` (temporary `play.cmd`
   line), grep the log for `tile merge: surface` (logged when a surface's tiles are learnt), compare race fps and
   draws with the default. Then live `Fm3 "switch REGPU_NO_TILE_MERGE 1"` for its share.
3. **Make the tile merge work with the draw thread** (a game-neutral ReGPU patch; PGR4 and upstream would
   gain too: report it). Likely the biggest single lever near AI cars (PGR4 +19%, FH1R's equivalent +15%).
4. **Per-draw cost cuts** from FH1R, as ReGPU patches (ideas, not code): reuse the previous draw's render-target
   setup when unchanged (−7.3–7.5% command-processor time per 1,000 draws, 158 → 165 fps); check an atomic flag
   before exchanging it (a locked `xchg` per draw was 3.6–3.9% of the command processor, `c1ad9cc`); descriptor
   and binding reuse (−7.4% per draw, 133.4 → 147.3 fps); skipping unchanged constant writes measured **worse**.
5. **Unmeasured switches already in our pin:** `REGPU_SUBMIT_DRAWS=512` (PGR4 FX 35.7 → 43.9 fps; FM2,
   GPU-bound, lost 4–17%; skipped on workers, unknown with the draw thread), `REGPU_CONSTANTS_IN_RING` /
   `Config::lowPerformance` (constants are FM3's biggest command-processor bucket; ~20% on an FX-8350, slower on
   fast CPUs), `REGPU_UPLOAD_STABLE_REUSE` (FH1 +1–5%; risk: a buffer one frame stale).
6. **Fewer AI-car draws** as a low-end option: FH1 gives only the two nearest other cars LOD0 (`f95b52c`). Find
   FM3's car LOD selection (OPEN-ISSUES L) and offer a lower-detail setting for AI cars. Costs detail, not accuracy.

Caveat: time trial differs from a race in more than AI cars (ghost car, field of cars at the start); compare the
same track and camera, paused, as in session 14.

## Performance on integrated GPUs and APUs (session 18, 2026-11-09)
Low-end machines are limited on both sides: a GPU with shared memory bandwidth, and a 4-core laptop CPU sharing
its power budget with that GPU. FH1R's per-draw command-processor cost on players' PCs: 1.5–1.6 µs (recent
desktops), 2.2–2.9 (Zen 3 / Comet Lake), 3.6–3.8 (4-core Zen 1/2), ~6 (Ryzen 7 7730U laptop).

**GPU side (ranked by expected gain):**
1. **Render scale + upscaler** (in ReGPU already: `Config::upscaler` FSR1/FSR3/DLSS, quality 1.5/1.7/2/3x per
   side, HUD at full resolution; FM3 wires it, launcher says "not yet tested on FM3"). FH1R's 540p scene: +44%
   GPU-bound (35 → 50 fps). Test FM3's menus (PGR4 dropped FSR1: menu panels drawn twice at two scales, `dd2f4dd`);
   FM4 forces MSAA off under FSR3/4 (`a09fc86`). Any upscaler turns the draw workers off (not the draw thread).
2. **MSAA 1x** (launcher). FH1R: no gain when CPU-bound, real gain when GPU-bound.
3. **iGPU defaults at first launch** (FM2 `0623b1f`: D3D12 UMA → 77% render scale + FSR1, sharpening 25; an
   Iris Xe report: 42 fps at 720p). Port the detection into our launcher.
4. **Texture budget:** PGR4's vendored ReGPU evicts unused textures past 1 GB (`REGPU_TEXTURE_BUDGET_MB`; without
   it PGR4 reached 280,000 textures and ran out of heap). Our pin has no limit; on shared-memory iGPUs this matters
   more. Unmeasured on FM3: log the texture count over a long session first.
5. **Small GPU-side items:** `REGPU_LEAN_FETCH` (FH1 GTX 970 20.2 → 21.7 fps, PGR4 +1 fps); 16-bit pixel shaders
   (section above, low priority).
6. **Submission size on UMA GPUs:** FH1R ends a submission every 1,000 draws on shared-memory GPUs (device lost
   6 of 46 starts without, 0 of 10 with: "a hint, not proof").

**CPU side:**
1. **Frame cap on iGPU machines:** an uncapped main loop takes the power budget the iGPU shares (FH1R #263:
   164 loops a second on 2.9 cores). Default the cap to 60 (or the display rate) when a UMA GPU is detected.
2. **Thread gating:** FM3 sets 3 draw workers and the draw thread with no core check (`src/gpu_host.cpp` ~165–186).
   PGR4 enables workers only with ≥6 hardware threads and measured the draw thread at −7 fps on an FX; FH1R's
   decoder/recorder split is on only with ≥6 cores (−11.4% on 4 cores in free roam). Measure on 4 cores; gate.
3. **Sleeping GPU waits:** the render thread spins in D3D's ring/fence wait (`sub_824B5048`). FM2 `480e94a`:
   render thread 100 → 36% of a core; FH1R: 86 → 45% and **+10% fps on a 4-core laptop** (no change on 8 cores).
   Needs ReGPU to `WakeByAddressAll` after its guest writes (never done in our pin) plus an FM3 hook.
4. **Present without waiting for the frame's replay** (FH1R `0414`): +15.3% uncapped on 4 cores, 0% when GPU-bound.
   Check whether ReGPU's present has an equivalent wait.
5. **Per-draw cuts** (previous section, step 4) help weak CPUs most.
6. **Shader/pipeline warm-up:** FM3 uses neither ReGPU's pipeline seed (`Host::pipelineSeed`) nor
   `REGPU_OPTIMIZE_SHADERS`; first-run hitches are worse on weak CPUs (PGR4 seeds 4,681 of 5,873 pipelines).
7. **Code generation:** x86-64-v3 for the recompiled code (FH1R: CPU −5%, fps unchanged); low priority.

**Memory (8 GB laptops):** FH1R found its XMA decoder pre-allocating 183 MB of codec contexts it mostly never used
(race RSS 1,204 → 1,037 MB after the fix); our decoder keeps a codec pool too (FM2 `c830cbc`: up to ~120 MB): check.

**Found in our code:** `src/settings.h` says its `Upscaler` numbers match ReGPU's, but our pinned ReGPU has no FSR 4
(`regpu.h`: Off, Fsr1, Fsr3, Dlss), so `upscaler=fsr4` in `settings.ini` passes an out-of-range value 4
(`gpu_host.cpp` ~55). The launcher doesn't offer FSR 4; only a hand edit hits it. Map it to Off or FSR3.

## Choosing the GPU in the launcher (session 18, 2026-11-09)
**Goal:** pick which GPU (dedicated card or integrated GPU/APU) the game renders on, from the launcher.

**Today:** ReGPU creates its device with `D3D12CreateDevice(nullptr, ...)` (`thirdparty/ReGPU/src/render/presenter.cpp`
~129): the default adapter, adapter 0 in DXGI's list. Neither FM3 nor ReGPU enumerates adapters or exports the
hybrid-laptop hints, and the launcher has no GPU code. On a laptop with Optimus or AMD switchable graphics the game
can end up on the integrated GPU. Windows' own per-app choice (Settings → Display → Graphics, stored under
`HKCU\Software\Microsoft\DirectX\UserGpuPreferences` as `GpuPreference=1;` power saving / `2;` high performance)
changes which adapter is the default.

**What the other ports did:**
- **FH1R** (`docs/rendering.md`, commit `3cf09e1`): a GTX 1050 + UHD 630 laptop rendered on the Intel GPU and lost
  the device at the intro. Their ReGPU plugin now takes the first adapter in DXGI's **high-performance order**,
  `REGPU_ADAPTER=<n>` picks another, and the log names the adapter in use. Their launcher lists the GPUs
  (`launcher/forza_launcher/gpu.py`: `Win32_VideoController` through PowerShell, `lspci`/sysfs on Linux) and passes
  the card chosen in Settings (`--gpu_vendor`: nvidia/amd/intel or a PCI id; fallback: most dedicated memory).
  They note a plugin DLL can't export `NvOptimusEnablement` for the exe; **we own `fm3.exe`, so we can.**
- **FM2** (`launcher/FM2Launcher/SystemCheck.cs`): the launcher calls `D3D12CreateDevice` itself through P/Invoke and
  asks `D3D12_FEATURE_ARCHITECTURE` whether the default GPU is UMA (integrated), to choose first-run iGPU defaults.
  The same approach lists every adapter (DXGI `EnumAdapters1` + `GetDesc1`) without a helper exe.

**Design (four parts):**
1. **ReGPU (game-neutral patch):** before `D3D12CreateDevice`, enumerate with `IDXGIFactory6::EnumAdapterByGpuPreference(
   DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)` (falls back to `EnumAdapters1`), skip software adapters
   (`DXGI_ADAPTER_FLAG_SOFTWARE`, WARP), and choose by `REGPU_ADAPTER`: empty = first in high-performance order;
   `<n>` = index in that order; `vendor:device` (hex PCI ids) or a name substring = that card. Fall back to the first
   on no match, and log every adapter (name, vendor/device id, dedicated and shared memory, UMA) and the chosen one.
   Store the pipeline library per adapter (`pipelines-<vendor>-<device>.bin`): today one `pipelines.bin` is
   rejected by the other GPU's driver and rebuilt, so switching back and forth rebuilds every time. The DXBC shader
   cache (FXC output) is GPU-independent and can stay shared.
2. **fm3.exe:** export `NvOptimusEnablement = 1` and `AmdPowerXpressRequestHighPerformance = 1` (two
   `extern "C" __declspec(dllexport)` globals), so "Automatic" means the dedicated GPU on hybrid laptops even when
   the driver profile would pick the integrated one. An explicit `REGPU_ADAPTER` still overrides.
3. **Launcher:** a Graphics setting "GPU": *Automatic (fastest)* plus one entry per adapter, e.g.
   "AMD Radeon RX 7900 XTX (24 GB)", "AMD Radeon Graphics (integrated, shared memory)". Listed in C# with DXGI
   P/Invoke as FM2's `SystemCheck.cs` does (factory → `EnumAdapters1` → `GetDesc1`; UMA via
   `D3D12_FEATURE_ARCHITECTURE` on a device made on that adapter). Save it in `settings.ini` as
   `gpu=<vendor>:<device>` plus the name; **not the LUID, which changes at every boot**. Pass it as `REGPU_ADAPTER`
   (as `Install.cs` already passes `REGPU_NO_ANISO`). Grey out DLSS unless the chosen GPU is NVIDIA; choosing an
   integrated GPU can offer the iGPU defaults (RESEARCH "Performance on integrated GPUs and APUs").
4. **The log line** `gpu: ReGPU at WxH, msaa N` gains the adapter name, so A/B runs show which GPU ran.

**Caveats:**
- **Monitor on one GPU, game on the other:** on a desktop with the monitor on the card, rendering on the iGPU
  makes Windows copy each frame across to the display GPU (and the reverse). It works (flip-model swap chains are
  cross-adapter capable) but costs latency and bandwidth: show a note in the launcher. On hybrid laptops this is the
  normal path.
- **A GPU that disappears** (eGPU unplugged, driver reset) falls back to Automatic with a log line, not a crash.
- Windows' per-app setting stays as a fallback the user can also use; the launcher doesn't need to write that
  registry key.

**Effort:** small and contained: ~60 lines in ReGPU (adapter choice, log, per-adapter library name) as patch 19,
two exports in FM3, and a launcher setting with ~80 lines of DXGI P/Invoke. Testable on the user's PC
(7900 XTX + the Ryzen iGPU if the CPU has one enabled).

## Less RAM, VRAM and memory traffic for iGPUs/APUs (session 18, 2026-11-09)
**Why it matters:** an iGPU has no VRAM of its own. "VRAM" is a carve-out of system RAM (often 512 MB–2 GB) plus
shared system memory, and every texture read, render-target write and copy competes with the CPU for the same
dual-channel DDR4/DDR5 bandwidth (~50–100 GB/s, against ~960 GB/s on the user's 7900 XTX). So on an iGPU, **bytes
moved per frame** matter more than shader math. Read from ReGPU at our pin (`thirdparty/ReGPU/src/gpu/renderer.cpp`,
`texture.cpp`) plus the other ports; nothing measured on an iGPU.

### Where ReGPU spends memory and bandwidth today
- **Render targets at twice the console's size per pixel.** Every EDRAM colour surface is `R16G16B16A16_FLOAT`
  (`kColorFormat`, `renderer.cpp` ~319: 8 bytes a pixel, chosen so the 7e3 HDR format fits), and every depth surface
  `D32_FLOAT_S8X24_UINT` (8 bytes). The console's are 4 bytes each (8888 / 2_10_10_10 / 7e3 colour, D24S8 depth).
  Multiplied by host resolution² and host MSAA samples: at 1440p 4x MSAA one colour + depth pair is
  2560×1440×4×16 B ≈ 236 MB, and every write, blend and resolve moves those bytes. At 720p 1x it is ~15 MB.
- **Texture uploads go through four memory passes.** The CPU reads guest memory, untiles and writes the blocks into
  the frame's upload ring (`CopyTextureBlocks`), then the GPU reads the ring and writes a texture in a default heap
  (`CopyTextureRegion`, ~5240–5290). On a dedicated GPU the last write lands in VRAM; on an iGPU all four passes hit
  the same system RAM. DXT1/3/5, DXT5A and DXN stay block-compressed (BC1–BC5), which is good. **Small uncompressed
  formats are expanded to 4 bytes a texel on the CPU** (`DecodeTexel`: k_8, k_8_8, k_5_6_5, k_1_5_5_5, k_4_4_4_4 →
  RGBA8: 2–4x their size).
- **Upload rings: 3 × 128 MB** of upload heap (system memory), one per frame in flight (`kFrames = 3`), or
  **3 × 384 MB = 1.15 GB** when guest memory can't be mapped for the GPU (the copy path; ~2010). They grow on
  demand to 2 GB (`kRingMax`). `REGPU_RING_MB` / `Config::uploadRingMB` set the start size.
- **Guest memory mapped for the GPU** (zero-copy, `OpenExistingHeapFromFileMapping` / `...FromAddress`, ~1753–1791):
  vertex and index data are read in place. **On an iGPU this is the ideal path** (no copies). On a dedicated GPU the
  GPU reads them over PCIe each frame; FM2 still measured it faster than copying (211 vs 150 fps).
- **Change detection** samples guest textures with `ContentHash` (a 256-byte stride, not every byte), staggered over
  frames (`REGPU_TEXTURE_CHECK_STAGGER`, an FM3 default): already light.
- **No texture budget:** the texture cache (`textures`, `cubes`, `volumes`, `mipChains` maps) never evicts unused
  textures; PGR4's vendored ReGPU added a 1 GB budget after reaching 280,000 textures.
- **ReGPU never asks the OS for its memory budget** (`IDXGIAdapter3::QueryVideoMemoryInfo`), so nobody sees an iGPU
  running past its carve-out into shared memory (slower again, and the cause of FH1R's black textures and driver
  timeouts on small carve-outs, `bccb7fd`, `11496a5`).

### What the other ports did
- **FH1R** (`docs/perf.md` "Memory ... for 8 GB PCs", #640): XMA decoder pre-allocated 183 MB of codec contexts it
  mostly never used (race RSS 1,204 → 1,037 MB after the fix); no NVIDIA driver pipeline file (private bytes
  3.29 → 2.72 GB); render targets sized to the rows actually drawn (est. 250 MB) and a 192 MB texture cache
  (both unmeasured); on small carve-outs new textures go to system memory when the carve-out is nearly full
  (`bccb7fd`), and submissions end every 1,000 draws on UMA GPUs against driver timeouts (`11496a5`).
- **PGR4:** texture eviction past a budget (`REGPU_TEXTURE_BUDGET_MB`, default 1 GB, `REGPU_NO_TEXTURE_EVICT`).
- **FM2:** the vertex arena keeps large vertex buffers on the GPU when guest memory can't be mapped (GPU
  3.34–3.55 → 2.59–2.97 ms, `a6edeb7`), and detects changes by write-watching pages instead of hashing
  (`0c9a747`); the launcher's first-run iGPU defaults (77% scale + FSR1).

### Options, ranked (gain for iGPUs vs effort)
1. **Measure first (small, ReGPU):** log `QueryVideoMemoryInfo` (local and non-local: budget and usage) every
   ~10 s and at loads, the upload ring's peak (`g_ringPeak`) and the texture count. One run on an iGPU then shows
   whether FM3 overruns the carve-out and how big the ring really needs to be.
2. **Settings (no code, biggest effect):** render scale + FSR and MSAA 1x cut render-target bytes with the pixel
   and sample count (1440p 4x → 720p 1x is 16x fewer). These are the iGPU defaults to ship (section
   "Performance on integrated GPUs and APUs").
3. **Smaller upload rings on iGPUs (small):** the launcher sets `REGPU_RING_MB` (e.g. 32–64) when the GPU is UMA,
   once step 1 shows the real peak; the ring still grows if a frame needs more. Saves ~200–300 MB of RAM (more on
   the copy path).
4. **Texture budget with eviction (medium, port from PGR4):** keeps the carve-out from overflowing over long
   sessions. On UMA, set the budget from `QueryVideoMemoryInfo` instead of a fixed 1 GB.
5. **Native small formats (small–medium):** upload k_8 as `R8_UNORM`, k_8_8 as `R8G8_UNORM`, 565/1555/4444 as
   `B5G6R5`/`B5G5R5A1`/`B4G4R4A4_UNORM` with the component mapping done in the SRV (`Shader4ComponentMapping`)
   instead of expanding to RGBA8 on the CPU. 2–4x smaller and faster to upload for those textures. First count
   how many FM3 uses (a capture's texture list).
6. **Untile on the GPU (medium–large):** a compute shader reads the tiled texture straight from the mapped guest heap
   and writes the texture: two memory passes instead of four, and no CPU time per upload. Helps loads and streaming
   (hitches when new cars or track sections appear) more than steady fps. Xenia does it this way.
7. **Write textures in place on UMA (medium):** on `CacheCoherentUMA` GPUs (`D3D12_FEATURE_ARCHITECTURE`), place
   textures in a CPU-writable custom heap and write them directly (`WriteToSubresource`), skipping the ring copy.
   Driver support varies; keep the ring path as fallback.
8. **Half-size render targets where the console format allows (large, risky):** choose the host format per surface
   from the guest's (8888 → `R8G8B8A8_UNORM`, 2_10_10_10 → `R10G10B10A2_UNORM`; 7e3 → `R11G11B10_FLOAT` when
   alpha is unused, else 16F; D24S8 → `D24_UNORM_S8_UINT` on NVIDIA/Intel). Halves render-target bandwidth, but
   ReGPU uses one format everywhere because EDRAM is aliased (the same bytes read as another format by a later
   pass, depth read as 8888, resolves): every alias needs a conversion. Only after a GPU profile shows render
   targets as the bound.
9. **RAM (8 GB laptops):** check our XMA codec pool against FH1R's 183 MB finding (FM2's pool grew to ~120 MB,
   `c830cbc`); keep the pipeline library compacted (ReGPU does already); size render targets to the rows drawn
   (FH1R's idea).

Not worth it: replacing the zero-copy mapping (already ideal on UMA), finer change detection (already sampled
and staggered), 16-bit shader math for bandwidth (section above: it saves registers, not memory traffic).

## Ultrawide (21:9, 32:9) (session 18, 2026-11-09)
**Today:** the launcher already offers Aspect ratio *Auto / 16:9 / 21:9 (experimental) / 32:9 (experimental)*
(`Install.cs` `Aspects`), written as `aspect=` in `settings.ini`. `Settings::RenderSize` (`src/settings.cpp` ~140)
fits that aspect inside the resolution (bars for the rest) and never goes narrower than 16:9. **Never tested on
FM3** (no note in NOTES/OPEN-ISSUES).

**What ReGPU does at wide outputs (our pin, `renderer.cpp` ~60–90, ~286–302, ~9650):**
- Only passes at the game's full 1280-pixel pitch (`WidePass()`: the main view and its full-screen post) scale
  horizontally to the full output width; every other pass (reflection faces, shadow maps, reduced-size post)
  scales uniformly, so square targets stay square. FM3's scene tiles are 1280 wide, so the race view qualifies.
- **Hor+ in the vertex shader:** perspective geometry in a wide pass is narrowed (`vwide`) so the picture is not
  stretched; the view gains width at the sides. `REGPU_WIDE_FOV` (0–100, default 100) trades side view for a
  vertical zoom (0 = keep the 16:9 horizontal view and crop top/bottom).
- **2D is squeezed back into the middle 16:9** (`vwide[2]`; `REGPU_NO_HUD_SQUEEZE=1` stretches it with the view):
  the HUD and menus keep their shape.
- `regpu::SetGameWidescreen(true)`: a port that widens the game's own camera says so, and ReGPU then stops
  narrowing perspective geometry itself (only the `REGPU_WIDE_FOV` zoom remains).

**The missing piece in FM3: the game's frustum culling stays 16:9.** ReGPU widens the picture, but the game still
culls everything outside its 16:9 view, so objects at the sides are missing or pop in late. FM4's port lists this
as its ultrawide known issue (`TESTING.md`). **FM2 fixed it** (`6f2980b`, `src/hooks/camera_aspect.cpp`, ~50
lines): it found the camera's aspect setter (`stfs f1,268(r3)`, called every frame of a race with 16/9) with a
float scan (`FM2_SCAN_FLOAT`, the same tool FM3 already has as `FM3_SCAN_FLOAT` in `src/hooks/common/mem_scan.cpp`),
and the hook passes the wider aspect instead. Result: the game's projection and frustum culling cover the wider
view (3440x1440: 1,933 draws a frame with the hook against 1,852 without, same frame rate). Follow-ups:
- `555f54d`: the setter runs every frame in races and never in menus, so a recent call means "race on screen":
  menus stay 16:9 with black bars, races go wide.
- `f2e98ed`: also require last frame's depth-tested scenery, so loading/saving screens stay in menu mode.
- `f1f4b62`: the race's car diagram (a HUD element) kept whole inside the HUD's 16:9 area.
- Remaining in FM2: some side objects still pop in through other game-side visibility (not the main frustum).

**FH1R** (ReXGlue, ideas only): native ultrawide with proportional HUD placement (HUD anchored to the screen edges,
`67a9b7b`, `docs/graphics-menu.md`), off-screen reflection and bloom passes kept out of the wide scaling, a
correction for the cockpit instruments (`6b4c29e`), 1080-line ultrawide windows rendered at 1440 lines and
downsampled (`8432b90`), and a note that shadow cascades fitted to the 16:9 frustum cover less of a wider view
(`docs/graphics-detail.md`).

**Plan for FM3:**
1. **Test what works today, without an ultrawide monitor:** set Aspect 21:9 at 2560x1440 (the game draws
   2560x1097 with bars) and 32:9. Check a race (sides: missing objects, pop-in), the HUD, menus, the pre-race
   camera, cockpit view (instruments), rear-view mirror, sun flare position, shadows at the sides, photo mode and
   replays. Screenshots with the timer for each.
2. **Find FM3's camera aspect setter:** in a paused race, `set FM3_SCAN_FLOAT=3FE38E39,<frame>` (16/9 as a float)
   logs every guest address holding it; take the one inside the camera object, then find the function that
   stores it each frame (a breakpoint-free way: the scan at two frames, then the xex's `stfs` to that offset).
   Our `diagnostics.cpp` already reserves `g_cameraAspectWidened` for this hook.
3. **Hook it as FM2 did:** pass the wider aspect only in races (a recent setter call), call
   `regpu::SetGameWidescreen(true)` while widened, keep `REGPU_WIDE_FOV` as the side-view/zoom control, and an
   `FM3_NO_CAMERA_ASPECT=1` undo switch. Measure draws with and without (the sides now drawn).
4. **Menus:** pillarbox (FM2's choice) vs stretched backgrounds with a centred 16:9 layer; decide after step 1.
5. **Later:** HUD anchored to the edges (FH1R-style; needs per-element positions), shadow-cascade reach at the
   sides, cockpit instruments if step 1 shows them distorted, and a launcher FOV slider (`REGPU_WIDE_FOV`).

**Effort:** step 1 is a play test; steps 2–3 are about a session (one hook plus the scan); menus and HUD work after.

## Shaders: precompilation, decompilation, discovery tools and building (session 18, 2026-11-09)

### How ReGPU builds shaders today (our pin)
1. **Find the microcode:** from the command stream when a draw binds it; ahead of use by scanning guest memory for
   the shader objects a level load creates (`src/gpu/shader_scan.cpp`: the IM_LOAD_IMMEDIATE packets, header
   `0xC0nn2B00`, embedded in each object); and optionally from a list of every shader in the game's files
   (`Host::staticShaders`, compiled on first boot behind a boot screen).
2. **Translate** Xenos microcode to HLSL (`hlsl.cpp`; vertex shaders are keyed with their vertex-fetch fields
   masked, since the title patches fetch fields into copies at load).
3. **Compile** with FXC (`d3dcompiler_47`) to DXBC shader model 5.1, first with `D3DCOMPILE_SKIP_OPTIMIZATION`
   (fast; `renderer.cpp` ~3516) so the draw isn't held long, cached per shader in `Config::userDir`.
   `REGPU_OPTIMIZE_SHADERS` (+ `REGPU_OPTIMIZE_THREADS`) rebuilds them with `OPTIMIZATION_LEVEL1` in the
   background (~3311–3380). **FM3 has never measured it.** GPU drivers optimise DXBC again when they turn it into
   GPU code, so the gain is unknown (larger on weaker driver compilers, possibly Intel's).
4. **Pipelines** (shader pair + render state) through `CreateGraphicsPipelineState`, stored in the pipeline
   library (`pipelines.bin`, compacted, keyed by driver) and listed in `pipelines.manifest` (hashes and render
   state only, no game data). **Lookahead** (`gpu.cpp`, `REGPU_NO_LOOKAHEAD=1` off) walks the commands queued
   behind a draw that needs a new pipeline and builds the whole set together. FH1R adopted the idea and measured
   worst frames 518 → 134 ms on a cold cache (`docs/perf.md` "Pipelines built ahead of their draws").
5. Draws wait for their shaders by default (`REGPU_ASYNC_SHADERS`, `REGPU_ASYNC_PIPELINES` opt-in: skip instead).
   FM3-QUIRKS Q-shader notes hl360 needed `async_shader_compilation = false` for the same reason (skipped draws
   show as missing objects).
6. `Host::pipelineSeed`: a shipped pipeline list built once on first boot (those needing a run-time shader are
   built once it exists).

**FM3 uses neither `staticShaders` nor `pipelineSeed`** (`src/gpu_host.cpp`; session 7: "Not copied: boot screen,
static shader scan (`.fxobj`), pipeline seed"). So every shader and pipeline is first met in play: the
"first-time shader compile" stalls in the session-7 race load (up to 1.37 s) and spikes in the first race.

### What the other ports ship
- **FM4** (`src/static_shaders.cpp`, 247 lines): its shaders are in `.fxobj` effect files, loose under
  `Media/Shaders` and inside each track's `bin.zip` (LZX, zip method 21), 4-byte aligned without a packet header,
  plus a few dozen in the executable. A candidate counts as a shader when its control flow decodes cleanly (valid
  opcodes to an end flag, first exec address = control-flow length, exec ranges tiling the instruction slots).
  The list is saved so later boots skip the search. **FM3 already has the pieces it needs** (`src/zip.cpp`,
  `src/lzx.cpp`); FM3 is the same Turn 10 engine generation, so its data very likely uses `.fxobj` too (check).
- **PGR4** (`src/gpu/static_shaders.cpp`, `pipeline_seed.h`, `boot_screen.cpp`): static shaders plus a pipeline
  seed generated from **players' uploaded manifests** (`tools/pipeline_seed.py`: 6,529 pipelines from 658
  manifests, 220 vertex-shader variants); first-boot seed coverage 4,681 of 5,873 pipelines once vertex-shader
  variants were precompiled too (`58e81a2`).
- **FM2** (`da37c3c`): the 33 vertex shaders every player compiled during play come precompiled.
- **FH1R** (`docs/rendering.md`): first start compiles all 1,688 game shaders behind a boot screen, then the
  1,183 seed pipelines; later starts load them in ~20 s. On a 4-core i5-10300H: 488 s for shaders + 70 s for
  pipelines (~10 minutes), so show progress and make it a one-time cost.

**Don't ship compiled shaders.** DXBC/DXIL translated from the game's microcode is derived game data. The ports
compile on the player's machine and ship only manifests (hashes and render state).

### Discovery and decompilation tools
| Tool | What it does for us | Where |
|---|---|---|
| `tools/scanshaders.py` (ReGPU) | Finds shader objects in the game data offline (loose files and zips; IM_LOAD packets), writes JSON. Python's `zipfile` can't read LZX (method 21) zips: for those, FM4's C++ scanner. | `thirdparty/ReGPU/tools/` |
| `tools/xenos_dis.cpp` (ReGPU) | Xenos microcode disassembler | same |
| Debug server (`tools/fm3ctl.ps1`) | `capture` (each draw's vs/ps hashes, textures), `hlsl <hash> <file>` (the translated HLSL), live `ps`/`vs` edits | ours |
| `pipelines.manifest` | Every pipeline a session built (hashes + state): the raw material for a seed | `out/user` (ReGPU user dir) |
| **XenosRecomp** (hedge-dev, MIT) | Converts Xbox 360 shader binaries to HLSL offline, using the XDK shader container's **reflection data** (named constant registers), compiled with DXC to DXIL/SPIR-V. Built for Unleashed Recompiled's translation-layer renderer, "do not expect it to work out of the box". For us: **readable HLSL with named constants** to understand FM3's effects (if `.fxobj` holds those containers); not a runtime path for ReGPU (no EDRAM model, game-specific vertex fetch). | `github.com/hedge-dev/XenosRecomp` |
| Xenia (`--dump_shaders`, `xenia-gpu-shader-compiler`) | Dumps each shader's microcode, disassembly and its own translation; the standalone compiler turns a microcode file into disassembly/DXBC/SPIR-V. A second opinion when our translation looks wrong. | Xenia source (BSD) |
| RenderDoc 1.47 (installed) | D3D12 frame capture: pipeline state, DXBC disassembly, shader debugging of our translated shaders | local |
| PIX on Windows | Per-draw GPU timing and occupancy: which translated shaders cost the most | Microsoft |
| Radeon GPU Analyzer / Profiler | Compiles DXBC/HLSL offline to RDNA3 ISA with VGPR count and occupancy; RGP times a frame on the user's 7900 XTX | AMD GPUOpen |
| Intel GPA, NVIDIA Nsight Graphics | The same for Intel iGPUs and GeForce cards | vendors |
| `fxc /dumpbin`, `dxc -dumpbin` | Disassemble a cached DXBC/DXIL file | Windows SDK |

### Options for building shaders, ranked
1. **Port FM4's static shader scan** (`Host::staticShaders`) with a boot screen: first boot compiles every
   shader in the game files; no first-play compile stalls. First check FM3's data for `.fxobj` (or run
   `scanshaders.py` on the extracted game; LZX zips need the C++ path). Effort: about a session (FM4's 247 lines
   plus FM3 paths; the zip/LZX readers are already ported).
2. **Pipeline seed** (`Host::pipelineSeed`): collect `pipelines.manifest` from the user's play sessions (and any
   testers'), merge with a small script as PGR4's `tools/pipeline_seed.py` does, embed as a header. Precompile the
   vertex-shader variants the manifests name (PGR4 `58e81a2`). Effort: small once step 1 exists.
3. **Measure `REGPU_OPTIMIZE_SHADERS`** (GPU time on the 7900 XTX and, later, an iGPU): a free A/B.
4. **Profile translated shaders** with RGA/RGP: the heaviest pixel shaders by VGPRs and time point at translator
   improvements (and tell whether the 16-bit idea above would pay at all).
5. **DXC / shader model 6** instead of FXC (large): often faster compiles on big shaders, DXIL, 16-bit types and
   wave ops, but a new cache format and shipping `dxcompiler.dll` + `dxil.dll`. Not in any ReGPU branch yet
   (upstream `engine-optimisation` only adds `REGPU_OPTIMIZE_LEVEL` for FXC, untested).
6. **Emit DXBC/DXIL directly** from microcode, as Xenia's D3D12 backend does (no compiler, near-instant
   shaders): the fastest possible first boot, but a rewrite of the translator. Not recommended for this project.
