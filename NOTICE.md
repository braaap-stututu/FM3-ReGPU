# Third-party code in this repository

## FM4 ReGPU port (Aiden-Piercee) and FM2 ReGPU port (zer0ducksgiven), MIT

Most of `src/` (runtime: loader, memory, kernel/XAM imports, threads, audio/XMA, fibers, zip
overlay, crash/hang diagnostics, settings, log) started as a copy of
[Fuzzy-Rittersport-4-ReGPU](https://github.com/Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU) commit
`69bdae44d22a9e0f52733ff55b9f1d0dd5e55b2b` (`src/`), which itself builds on
[Forza-Motorsport-2-ReGPU](https://github.com/zer0ducksgiven/Forza-Motorsport-2-ReGPU); its licence (below) is the one FM2 ships, kept by FM4. It was adapted for FM3: FM3 guest addresses (`src/hooks/addresses.h`),
a single recompiled image, upstream ReGPU's API, `FM3_*` switches, and FM2/FM4-only parts removed.
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
