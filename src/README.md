# src/ — runtime (planned layout)

Filled in Phase 3. Modelled on FM2-ReGPU's `src/`, with the GPU replaced by ReGPU linked the way
FM4-ReGPU does it. Every guest address here is for FM3's `default.xex` v0.0.0.8: never reuse an
FM2/FM4 `sub_XXXXXXXX`.

| Path | Contents | Template |
|---|---|---|
| `main.cpp` | crash handler → memory → load `default.xex` → runtime → TLS → guest main thread → watchdog | FM2/FM4 `main.cpp` |
| `loader.*`, `memory.*` | XEX parse (XenonUtils), section copy, `PPCFuncMappings`; 4 GB guest space, physical/virtual heaps | FM4 |
| `ppc_hooks.h` | force-included into `out/ppc` (`PPC_MSR_CHANGED`, helpers) | FM4 |
| `indirect.*` | `PPCLookupIndirect`, fatal on a miss | FM4 |
| `gpu_host.cpp` | ReGPU `Config`/`Host` wiring; FM3 defaults (vblank 60 Hz, Q24) | FM4, adapted (**written**) |
| `kernel/` | `imports_{ke,mm,io,rtl,crypt,xam}.cpp`, objects, threads, VFS, `stubs.cpp` (generated) | FM2/FM4 |
| `hooks/` | `PPC_FUNC` overrides: fibers (Q7), `cache:` utility drive (Q8), setjmp/longjmp support | FM4 `fibers.cpp` + FM3 `addresses.h` |
| `audio/` | render driver → XAudio2, XMA via FFmpeg (Q26) | FM2/FM4 |
| `input/` | XInput / keyboard | FM2 |
| `settings.*`, `log.*`, `user_dir.*`, `crash.cpp`, `hang.cpp` | support | FM4 |
