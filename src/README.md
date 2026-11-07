# src/: runtime

Ported from the FM4 ReGPU port's runtime (MIT; see `NOTICE.md`), adapted for FM3. Every guest
address here is for FM3's `default.xex` v0.0.0.8 and lives in `hooks/addresses.h`; never reuse an
FM2/FM4 `sub_XXXXXXXX`. Debug switches are environment variables named `FM3_*` (and ReGPU's
`REGPU_*`).

| Path | Contents |
|---|---|
| `main.cpp` | crash handler → memory → load `default.xex` → runtime → TLS → guest main thread → watchdog (`FM3_WATCHDOG=<ms>`) |
| `loader.*`, `memory.*` | XEX parse (XenonUtils), section copy, imports, TLS; 4 GB guest space, physical/virtual heaps |
| `recomp_modules.*`, `indirect.*` | the one-image table; `PPCLookupIndirect` table (fatal on a miss) |
| `ppc_hooks.h` | force-included into `out/ppc` (`PPC_MSR_CHANGED`, MMIO store, half floats, indirect calls) |
| `mmio.cpp` | marked hardware stores (APU → XMA) |
| `gpu_host.cpp` | ReGPU `Config`/`Host` wiring; FM3 defaults (vblank 60 Hz, Q24) |
| `kernel/` | `imports_{ke,mm,io,rtl,crypt,xam}.cpp`, objects, threads, VFS, zip overlay (mods), XAM trace; stubs are generated into the build folder (`tools/genstubs.py`) |
| `hooks/` | `fibers.cpp` (XAPI fibers → host fibers, Q7), `addresses.h`, `common/` diagnostics |
| `audio/` | render driver → XAudio2 (`audio.cpp`), XMA via FFmpeg (`xma.cpp`) |
| `settings.*`, `user_dir.*`, `log.*`, `crash.cpp`, `hang.cpp`, `exit_trace.cpp`, `live_switch.*`, `zip.*`, `lzx.*` | support |

User data: `Documents\fm3` (`FM3_USER_DIR=<dir>` overrides). Log: `fm3.log` in the working
directory (`REGPU_LOG_DIR` overrides).

Left out of the FM4 copy: its boot screen (FM4 art), `.fxobj` shader scan, shipped pipeline list,
updater, FM2 test launcher, FM2/FM4 ui.zip patches (`FM3_LEGACY_UI_PATCHES=1` re-enables them),
benchmark mode and the draw-worker profiler (FM4's vendored ReGPU API).
