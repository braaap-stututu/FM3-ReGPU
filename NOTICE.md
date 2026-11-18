# Third-party code in this repository

## FM4 ReGPU port (Aiden-Piercee) and FM2 ReGPU port (zer0ducksgiven), MIT

Most of `src/` (runtime: loader, memory, kernel/XAM imports, threads, audio/XMA, fibers, zip
overlay, crash/hang diagnostics, settings, log) started as a copy of
[Fuzzy-Rittersport-4-ReGPU](https://github.com/Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU) commit
`69bdae44d22a9e0f52733ff55b9f1d0dd5e55b2b` (`src/`), which itself builds on
[Forza-Motorsport-2-ReGPU](https://github.com/zer0ducksgiven/Forza-Motorsport-2-ReGPU); its licence (below) is the one FM2 ships, kept by FM4. It was adapted for FM3: FM3 guest addresses (`src/hooks/addresses.h`),
a single recompiled image, upstream ReGPU's API, `FM3_*` switches, and FM2/FM4-only parts removed.
`src/static_shaders.cpp` (session 24) is adapted from that project's `src/static_shaders.cpp` (FM3 file stamp, `FM3_NO_STATIC_SHADERS`).
The XenonRecomp patches in `patches/` come from the same project (see `patches/README.md`).
The launcher in `launcher/FM3Launcher` is adapted from that project's `launcher/FM4Launcher` as of the same
commit (via the fork `braaap-stututu/Fuzzy-Rittersport-4-ReGPU-testing`): renamed for FM3, its settings and paths
pointed at `fm3.exe`, and the import, DLC, photo, report and update parts left out.

```
MIT License

Copyright (c) 2026 zer0ducksgiven

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Submodules and local dependencies

- `thirdparty/XenonRecomp`: MIT, hedge-dev and contributors.
- `thirdparty/ReGPU`: MIT.
- `thirdparty/ffmpeg` (not in the repository): FFmpeg LGPL build, linked dynamically, under its own licence.
- `thirdparty/fidelityfx` (not in the repository): AMD FidelityFX SDK, MIT.
- `thirdparty/meshoptimizer` (submodule, v1.3): meshoptimizer, Copyright (c) 2016-2026 Arseny Kapoulkine, MIT (`LICENSE.md`); compiled into ReGPU for the Hyper presets' mesh decimation and vertex-cache order (session 43).
- `thirdparty/xess` (not in the repository): Intel XeSS SDK 3.0.2 (XeSS 2.0.2 runtime), Copyright (C) 2025 Intel Corporation, Intel Simplified Software License (`LICENSE.txt`, shipped beside fm3.exe as `libxess-LICENSE.txt` and in `licenses/`); `libxess.dll` is redistributed unmodified and loaded at run time (session 43).
- `thirdparty/openxr` (session 37, not in the repository; VR): the OpenXR SDK headers and `openxr_loader.dll` from
  KhronosGroup/OpenXR-SDK-Source release 1.1.65 (`openxr_loader_windows-1.1.65.zip`, x64), Copyright The Khronos Group
  Inc. and contributors; Apache License 2.0 (`thirdparty/openxr/LICENSE`). The loader DLL ships beside `fm3.exe`.
- `thirdparty/aa` (session 36, the anti-aliasing filters, compiled into ReGPU's shaders; pinned commits in
  `thirdparty/aa/SOURCES.txt`):
  - `smaa/`: SMAA (`SMAA.hlsl`, `AreaTex.h`, `SearchTex.h`), Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria,
    Belen Masia, Fernando Navarro, Diego Gutierrez; MIT licence (`smaa/LICENSE.txt`). From iryoku/smaa.
  - `cmaa2/`: CMAA 2 (`CMAA2.hlsl`), Copyright (c) Intel Corporation; Apache License 2.0 (`cmaa2/license.txt`).
    From GameTechDev/CMAA2.
  - `fxaa/`: FXAA 3.11 (`Fxaa3_11.h`), Copyright (c) 2014 NVIDIA Corporation; BSD-style licence in the file's header.
    The copy in Intel's CMAA 2 sample (its luma-texture addition unused here).
