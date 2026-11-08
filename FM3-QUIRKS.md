# FM3-QUIRKS.md — Forza Motorsport 3 game-specific findings

Every FM3-specific finding from the two ReXGlue recomps, with the address, the symptom, how each project solved it, and how the same thing is handled in **XenonRecomp + ReGPU** (the stack used by the FM2 / FM4 ReGPU ports).

Sources (cloned under `reference/`):

| Short | Repo | Commit | Claimed state |
|---|---|---|---|
| **HL** | `reference/rexglue recomp reference/hl360-forza3` | `8612130` (2026-10-17, rel 0.1.3), ReXGlue SDK v0.10.0 (+2, `c94f5eb`) + 6 patches | Menus, videos, Free Play, races at 60 fps |
| **TS** | `reference/rexglue recomp reference/forza3-recomp` | `5b14b0d` (2026-10-16, v0.1.0), ReXGlue v0.10.0 tag + 11 patches | Boots, menus, R8 showcase, opening race drivable |
| XR | `reference/framework/XenonRecomp` | `ddd128b` | — |
| RG | `reference/framework/ReGPU` | `57143cb` | — |
| FM2 / FM4 | `reference/sibling-recomps/*` | `c5e71cd` / `69bdae4` | templates |

Full verbatim catalogues (every address list, file:line refs): [reference/catalogs/hl360-forza3_catalog.md](reference/catalogs/hl360-forza3_catalog.md), [reference/catalogs/forza3-recomp_catalog.md](reference/catalogs/forza3-recomp_catalog.md), and the stack mechanisms reference [reference/catalogs/xenonrecomp-regpu_mechanisms.md](reference/catalogs/xenonrecomp-regpu_mechanisms.md).

Legend: **(inf)** = inference, not stated by the sources. "Agree" = both ReXGlue projects reached the same answer independently, which makes it high-confidence.

---

## 0. Target build (both projects agree)

| Field | Value |
|---|---|
| Game | Forza Motorsport 3 **Ultimate Collection (USA)** |
| Title ID / Media ID | `4D53084D` / `6F74359D` |
| `default.xex` | version 0.0.0.8, 13,250,560 bytes, SHA-256 `aa15372c13bc4c899b5ef90a1cfb5634d389b0a3620cc9715f6023d61a9b8742` |
| XEX format | retail XEX2, encrypted, basic compression |
| Disc 1 / Disc 2 MD5 | `A527154D55C7B39849EC68CD46F17281` / `E65FF1CDC189F3EB9B1D0F14F97BAEA4` (7,838,695,424 bytes each) |
| Image base / size | `0x82000000` / `0xD60000` |
| Code base / size | `0x821E0000` / `0x99E128` (ends `0x82B7E128`) |
| Static XAPI | XDK 2.0.8276 |
| Disc 2 | 8 STFS packages `4D53084D0CCF00{13,14,15,30,35,40,45,60}`, type `00000002`; Disc 2's own default.xex (installer) unused |

All addresses below are only valid for this exact xex. The original 2009 release (used by the Project Forza Plus 3 mod's edited xex) is **different** and must not be used. The only FM3 entry in Xenia's `game-patches` ("Disable hash checks") doesn't match this xex.

---

## 1. Codegen / function boundaries

### Q1. setjmp / longjmp — **Agree**
- **Address:** setjmp `0x82534B20`, longjmp `0x825345E0` (CRT, hand-written, no .pdata). Crash site without it: libjpeg `read_markers` = `sub_824E9048`, null deref of `cinfo->unread_marker` at +0x1A4.
- **Symptom:** guest longjmp restores registers but doesn't unwind the host stack → clobbered non-volatiles → crash entering a race (HL) / in the R8 showcase (TS).
- **Call sites (TS, grep of generated code):** setjmp in `sub_824D37E0, 824D47B8, 824D3B28, 824D49B0, 824EB258, 824EB6C8, 828C2B98`; longjmp in `sub_824D0BA0, 824D0D38, 824E6970, 824E69C0, 828C3558, 82A3D650`. 0x824Dxxxx–0x824Exxxx = libjpeg (inf).
- **HL:** `config/codegen.toml` `setjmp_address`/`longjmp_address`.
- **TS:** `forza3_manifest.toml` `[entrypoint]` same keys; found with `tools/analysis/findjmp.py`.
- **XR + ReGPU:** identical keys in XR `[main]`: `setjmp_address = 0x82534B20`, `longjmp_address = 0x825345E0`. XR emits host setjmp/longjmp and saves/restores the whole `PPCContext`. Disassemble both first; FM4 warns that fiber switches match the same byte pattern.

### Q2. Missed tail-call / vtable-only thunks and refcount leaves — **Agree** (same 11 addresses)
| Address | Size | What |
|---|---|---|
| `0x82576940` | 0x4C | interlocked Release (refcount +0x110) |
| `0x8254ABF0` | 0x24 | interlocked AddRef (+0xE0) |
| `0x82A97418` | 0x24 | interlocked AddRef (+0x174) |
| `0x82B123E0` | 0x04 | `b 0x82B12068` |
| `0x828DCE98` | 0x08 | `addi r3,r3,0x8C; b 0x828DCDF8` |
| `0x82B14310` | 0x08 | `addi r3,r3,4; b 0x82B0DAC8` |
| `0x826E0998` | 0x18 | virtual-base this-adjustor → `0x826E0F70` |
| `0x82713848` | 0x18 | thunk → `0x82713668` (small leaf after a .pdata function, reached only by tail branch) |
| `0x82AB2668` | 0x18 | thunk → `0x82AB2468` (same) |
| `0x82A07F68` | 0x4C | small tail-branch-only leaf |
| `0x82A60EF0` | 0x28 | small tail-branch-only leaf |
- **Symptom:** no .pdata, so analysis doesn't find them → unregistered function at runtime / bad splits.
- **HL:** `[functions]` with explicit `size` (sizes above are HL's). **TS:** same addresses, mostly `{}` (SDK infers size).
- **XR + ReGPU:** `functions = [{ address = 0x…, size = 0x… }]` in `[main]`. XR **requires** `size` on every entry, so use HL's sizes. Before adding them, confirm with `FM4/tools/fm4_crosscheck.py` whether XR's own analysis already finds them.

### Q3. Leaf functions with tail-merged exits split at first `blr` — **Agree** (TS has 1 extra)
| Address | End | XR size |
|---|---|---|
| `0x8295AB20` | `0x8295ABA0` | `0x80` (list walk, returns 0 / 0x25) |
| `0x8295ABA0` | `0x8295ABDC` | `0x3C` (**TS only**: list-count leaf right after it) |
| `0x82971FE0` | `0x82972054` | `0x74` (getter) |
| `0x829720D8` | `0x8297214C` | `0x74` (setter) |
- **Symptom:** a shared `li r3,0; blr` exit sits after the first `blr`; the scanner ends the function there, so internal branches are reported unresolved.
- **HL / TS:** `{ end = … }`.
- **XR + ReGPU:** `{ address, size = end - address }`. Include TS's extra `0x8295ABA0`. Check XR's log for `// ERROR` branches and run `FM2/tools/fallthrough.py` to find others.

### Q4. ~193–206 functions reachable only through pointers (vtables, callbacks, thread entries) — **Agree** (lists overlap almost entirely)
- **Addresses:** `0x821E2E18` … `0x82B26598`. HL: 206 entries in `config/fptr_functions.toml`. TS: 148 data-referenced + 45 code-built (lis/addi|ori) = 193. Full lists are in the catalogues. Notable ones: `0x829502D8` (first runtime hit, data ref `0x82128A60`) and **`0x8282B500` = GPU interrupt thread entry** (code ref `0x8282C434`). Excluded false positive: `0x82A8E440` (EH continuation inside a function). Odd non-aligned starts: `0x8297B124`, `0x8298D08C`.
- **Symptom:** `Call to invalid or unregistered function at guest address X`.
- **HL:** `tools/find_fptrs.py` scans `.rdata`/`.data` for words pointing into `.text` that follow a function end (blr / bctr / pad / b), plus lis+addi/ori pairs. `tools/prune_fptrs.py` drops candidates that lie inside unresolved-branch spans in codegen.log.
- **TS:** `tools/analysis/ptrscan.py` does the same and also estimates sizes (first unconditional b/blr/bctr, cap 64 insns); it excludes `loc_` labels such as jump-table bases.
- **XR + ReGPU:** XR has no separate "fptr" key. A pointer-only target is callable only if it is in `PPCFuncMappings`, which means it must be an XR function, so add each as a `functions` entry **with a size** (TS's ptrscan comments give estimates; verify). Use the union of HL and TS. Run `fm4_crosscheck.py` to drop entries XR already finds. Adopt FM4's `PPCLookupIndirect` (`src/ppc_hooks.h` + `src/indirect.cpp`) so a miss gives `Fatal("indirect call to %08X")` instead of a null call. FM2's `GuestCall` does the same for thread entries.
- **Phase 2 result (2026-11-07):** XenonRecomp's gap sweep already makes 19,453 of 19,469 pointer targets (both ports' lists + our own scan) function starts: **no fptr entries were needed**. The other 16 are SEH `__except` resume points inside functions (scope tables at the end of `.rdata`), not entry points. `tools/fm3_check.py` repeats the cross-check after each recompile.

### Q5. vmsum3fp128 / vmsum4fp128 overflow must return QNaN — **TS only** (FH1 patch 0025)
- **Address:** codegen-wide (no FM3 address given).
- **Symptom:** Xenon returns QNaN `0x7FC00000` when the finite dot product overflows; x86 `dpps` gives ±inf. TS does not document a visible FM3 symptom.
- **TS:** SDK patch `0025-vmsum-overflow-is-qnan` (codegen/builders/vector.cpp): keep the `dpps` fast path; if the result is non-finite, recompute in double in Xenia's order.
- **XR + ReGPU:** patch XR's `vmsum3fp128`/`vmsum4fp128` emitters (alongside FM2/FM4's `xenonrecomp-*.patch` set) to do the same, or redefine the helper in the force-included `ppc_hooks.h`. **(inf)** Low priority unless physics NaNs show up.

### Q6. Jump tables / EH data / save-restore helpers — not covered by either ReXGlue project
- Neither ReXGlue project needed explicit jump-table config, because the SDK handles them.
- **XR + ReGPU:** run XenonAnalyse → `switch_table_file_path`. FM3 (2009) sits between FM2 (2007) and FM4 (2011) in compiler age. **(inf)** Expect to need FM4's `xenonrecomp-fm4.patch` (back-trace from every `bctr`) and `xenonrecomp-switch-u32.patch`. Also find `invalid_instructions` (EH handler words, `0x00000000` padding) and the `savegprlr_14`/`restgprlr_14`/fpr/vmx helper addresses with `FM4/tools/fm4_config.py` or `FM2/tools/findhelpers.py`. Apply `tools/patch_mftb.py` (mftb → 49.875 MHz timebase).
- **Phase 2 result (2026-11-07):** confirmed. XenonAnalyse finds 262 tables, 251 of them only through the FM4 back-trace patch. Helpers found by `tools/fm3_config.py` (savegprlr_14 `825301E0` etc., all unique). Three leaves with no .pdata that hold byte-offset tables (`82354668`, `825F16B8`, `829D5508`) needed explicit sizes. The `vmsum` QNaN fix (Q5) is not applied yet.

---

## 2. Threading / fibers

### Q7. XAPI fibers must be replaced by host fibers — **Agree on 4 of 5 addresses**
| Function | HL | TS |
|---|---|---|
| ConvertThreadToFiber | `0x82A7B098` | `0x82A7B098` |
| ConvertFiberToThread | `0x82A7B128` | `0x82A7B128` |
| CreateFiber | `0x82A7B178` | `0x82A7B178` |
| DeleteFiber | `0x82A7B258` | `0x82A7B258` |
| SwitchToFiber | **`0x82A82210`** (body, end `0x82A8261C`) | **`0x82A7B2B8`** (export thunk `b 0x82A82210`) |
| GetCurrentFiber | — | `0x82A7B088`, left as guest code (reads `KTHREAD+0x164`) |
| CreateFiber trampoline | — | `0x82A82200` |
- **Symptom:** SwitchToFiber is hand-written assembly that swaps GPR/FPR/VMX and the stack; recompiled code can't follow a stack swap, so the job system breaks (inf: neither repo shows the crash).
- **Address difference:** the two projects aren't in conflict. `0x82A7B2B8` is a one-instruction thunk to the body at `0x82A82210`; HL hooks the body, TS hooks the thunk.
- **HL:** `[rexcrt]` + boundary `0x82A82210 = { end = 0x82A8261C }`. **TS:** `[entrypoint.rexcrt]`, found with `tools/analysis/fibers.py`.
- **XR + ReGPU:** `PPC_FUNC` overrides with host `CreateFiberEx`, keeping the guest fiber object layout (0xA50 bytes, current fiber at `KTHREAD+0x164`). Port `FM4/src/hooks/fm4/fibers.cpp` and keep FM3 addresses in an `addresses.h`. Add `functions` entry `{ address = 0x82A82210, size = 0x40C }`, the same size as FM4's SwitchToFiber, so FM3 follows the FM4 pattern. Override the body; the thunk at `0x82A7B2B8` then reaches it. Keep FM2's practice of overriding the raw register-swap routine with `Fatal` so any path that skips the hook is caught.

---

## 3. Filesystem / devices

### Q8. `cache:` (utility drive) used but never mounted → crash at race start — **Agree**
- **Address:** null replay-stream deref in **`sub_828678F0`** (TS). The game calls its statically linked `XMountUtilityDrive`, then streams to `cache:\replay_stream`.
- **Symptom:** "crash at race start". The SDK stubs the kernel calls but registers no device behind `cache:`.
- **HL:** `Fm3App::MountCachePartition` (`src/fm3_app.cpp:84-97`) → `HostPathDevice` `\Device\Fm3Cache` at `<user>/cache_partition`, symlink `cache:`.
- **TS:** `src/utility_drive.cpp` → `\CACHE` at `<user>/utility_drive`, symlinks `cache:`, `cache0:`, `cache1:`. Deliberately avoids the SDK shader-cache folder.
- **XR + ReGPU:** FM2's `VfsResolve` already maps `cache:`, `\device\harddisk0\cache0/1` → `UserDir()/cache`. Also port FM2's `hooks/utility_drive.cpp`: override FM3's `XMountUtilityDrive` (find its address; it is linked into the title, not an import), set the mounted flag and return success, and override the "has hard drive" native to return true. Keep the replay folder separate from ReGPU's `shadercache/`.

### Q9. Protected-file signature table at `0x82CF0000` — **HL only**
- **Address:** table at guest `0x82CF0000` (read-only resource section). Big-endian layout: `u32 block_size (0x8000)`, `u32 count`, `count × {u32 offset, u32 size}`, then per file a name relative to `Media` + SHA-1 of every 0x8000 block. 27 files (gamedb.slt, PhysicsSettings.ini, GameTunableSettings.ini, CameraSettings.ini, profile schemas, string tables…).
- **Symptom:** any modified protected file (mods, FOV edits) → disc read error.
- **HL:** `src/file_signatures.cpp`. In `OnPostLoadXexImage` it re-hashes the files from disk and rewrites the table in memory (pages temporarily RW). The cvar `resign_protected_files` defaults to true. The result is identical for unmodified files.
- **XR + ReGPU:** do the same in `loader.cpp` after the section copy (FM2 does something similar: it renames hash-table entries for modded `camerasettings.ini` / `gamedb.slt`). This is only needed for mods; with the stock game it is a no-op.

### Q10. Windows MAX_PATH on the user-data root — **HL**
- **Symptom:** the deepest Disc 2 path is 162 chars below the user root; with no long-path support the files can't be opened.
- **HL:** launcher limits the user root to ≤96 chars (≤53 with Project Forza Plus 3).
- **XR + ReGPU:** same constraint unless the VFS uses `\\?\` prefixes. **(inf)** Use `\\?\` in the port's host file layer and avoid the limit altogether.

---

## 4. Kernel / XAM / content / DLC

### Q11. Disc 2 content must be installed (no tray swap) — **Agree**
- **Path:** `Disc 2/Content/0000000000000000/4D53084D/00000002/<8 packages>` → `<user>/0000000000000000/4D53084D/00000002/<pkg>/` + `Headers/00000002/<pkg>.header` (XCONTENT data + licence mask from the STFS header).
- **Symptom:** the game's own install flow needs a disc swap; installed packages without licence headers aren't usable.
- **HL:** `fm3tool install-dlc` (tool-mode runtime → `ContentManager::InstallContent`), run by the launcher.
- **TS:** `src/disc2_install.cpp` auto-installs in `OnPreLaunchModule`. The `.header` is written last, so a partial install retries.
- **XR + ReGPU:** FM2 unpacks type-2 packages with `tools/stfs.py install` into `DlcDir()/<pkg>`, mounted read-only as `\Device\Dlc\<pkg>`; `XamContentCreateEnumerator`/`XamEnumerate` list them. Reuse that. The enumerator must list only real packages (see Q13).

### Q12. Content licence mask must grant all bits — **HL** (only matters for DLC car visibility)
- **Function:** `xeXamContentCreate` / `XamContentGetLicenseMask`.
- **Symptom:** FM3 runs `Media/LicenseMasks/<bit>.sql` for each granted bit to mark a DLC pack's cars installed. With a mask of 0, the mod's 14 packs (114 cars) stay hidden. The UC packs don't ship such scripts, so stock UC isn't affected.
- **HL:** SDK patch 0005 ORs cvar `license_mask` (= `0xFFFFFFFF`) into each package's licence.
- **XR + ReGPU:** FM2's `XamContentCreateEx` already writes `0xFFFFFFFF` to the licence out-param (r8); FM2 hid 10 of 11 cars without it. Carry it over, and make `XamContentGetLicenseMask` return all bits too.

### Q13. Content listing duplicated header-less packages — **HL** (SDK bug that FM3 hits)
- **Symptom:** `XSUCCEEDED(X_ERROR_FILE_NOT_FOUND)` counted as success, so a package without a header got an unfilled or duplicate entry and FM3 mounted the previous package repeatedly.
- **HL:** patch 0006 compares against `X_ERROR_SUCCESS`.
- **XR + ReGPU:** in the port's `XamEnumerate`, skip packages whose header or metadata can't be read, and never emit zeroed entries.

### Q14. `XCONTENT_DATA.fileName` may be unterminated (42 chars) — **HL**
- **HL:** patch 0004 stops at the first byte outside 0x20–0x7E.
- **XR + ReGPU:** in `XamContentCreateEx`/enumeration, read at most 42 bytes and stop at the first non-printable byte.

### Q15. Profile XUID and gamertag constraints — **HL** (TS uses a fixed XUID)
- **Symptom:** XUID bits 54–55 make the account look restricted to the game; gamertags are at most 15 chars.
- **HL:** patch 0001 adds profile setters; `xuid & ~0x00C0000000000000`; offline XUIDs `0xE000000000000000 | rand & 0x00003FFFFFFFFFFF`.
- **TS:** patch 0101 adds a `user_name` cvar (≤15 chars); XUID fixed at `0xB13EBABEBABEBABE`.
- **XR + ReGPU:** FM2's `Xuid()` already requires `0xE…` offline XUIDs. Add the bit-54/55 clear and 15-char name truncation in `XamUserGetXUID` / `XamUserGetName`.

### Q16. Game language depends on console *country* too — **HL**
- **Address:** string-table selection `sub_828AFE00`.
- **Symptom:** Polish/Czech/Hungarian load only when the country is PL/CZ/HU; Spanish + Spain gives Castilian. Language is read only at boot (TS).
- **HL:** launcher writes language/country pairs: EN 1/103, ES 5/31, FR 4/34, DE 3/24, IT 6/50, PL 11/82, CZ 1/23, HU 1/42, RU 12/88, JA 2/53, KO 7/56, ZH 8/101.
- **XR + ReGPU:** make `XGetLanguage` and the country query (`XamGetLocale`/`XGetLocale`-style import, plus `ExGetXConfigSetting` country) return the paired values from settings.

### Q17. Achievement toast breaks frame pacing — **HL**
- **Symptom:** while the SDK's achievement ImGui dialog is registered, the presenter repaints at monitor rate → misleading FPS counter and judder.
- **HL:** `CreateAchievementNotificationDialog()` returns `nullptr`.
- **XR + ReGPU:** not applicable; ReGPU presents once per `VdSwap` and FM2's `XUserWriteAchievements` returns success and stores nothing. Keep any future achievement UI out of the present path.

---

## 5. GPU / render

### Q18. Render-to-texture readback needed (roads, car thumbnails, shadow maps) — **Agree it's needed; disagree on mode**
- **Symptom:** without CPU readback of resolves, the road surface and vehicle thumbnails are wrong. TS adds: CPU writes near the shadow maps make the emulator re-upload pages from stale guest memory → page-sized block/grid/dash patterns on the road, flashing while driving.
- **HL:** `readback_resolve = "fast"` (async, previous frame; 60 fps). `"full"` is synchronous and caps FM3 at **30 fps** (measured). Hidden from the launcher and force-written.
- **TS:** `readback_resolve = "some"`. The float24 depth settings were **rejected** because they broke 5 shadow pipelines. Detected with `tools/analysis/stripes.py`.
- **XR + ReGPU:** ReGPU keeps resolves on the GPU unless `Host::resolveWriteback` asks for one. Write an FM3 rule like FM4's road rule (k_8_8 detail image ≤512² and 32×512 strips, `Mode::Deferred`, row range) plus FM2's thumbnail rule (256×128). Find FM3's actual resolve sizes and formats with `REGPU_TRACE_RESOLVES` / F12 capture. Set `REGPU_NO_WRITEBACK_FENCES=1` (with fences FM4 dropped to 43 fps). The stale-page problem is a Xenia texture-cache artefact **(inf)**: ReGPU's texture invalidation differs, so verify with a road-flicker test rather than assume.

### Q19. Car shadow: dark square block under / rotating with the car — **Agree on root cause family**
- **Symptom:** "square shadows". TS's root cause: FM3 renders a top-down **variance shadow map** (`k_16_16`, **4× MSAA**), resolves it with exp bias +10 as **SignedInteger** (`RB_COPY_DEST_INFO.copy_dest_number`), then samples it as **signed integer** data with exp_adjust −15 and tests `>= 0.99` to build the ground-shadow mask. The SDK ignored the destination number format in resolves (saturated to 0xFFFF = −1) and the integer number format in fetches (32767 → 1.0).
- **TS:** SDK patch `0100-resolve-and-fetch-integer-number-formats`: (1) fetch: multiply the normalized result by `2^w−1` / `2^(w−1)−1` when fetch dword 3 bit 0 is set (8- and 16-bit uniform formats); (2) the fast resolve is only used when the destination number format matches; (3) the full-resolve compute shaders are rebuilt from upstream Xenia + canary number-format packing and the −32 `k_16_16` clamp. Found by **diffing frame dumps against an instrumented xenia-canary**.
- **HL:** patch 0003 backports xenia-canary `d119505` (same integer `num_format` fetch rescale, via a `texture_integer_scale_bits` system constant), plus `execute_unclipped_draw_vs_on_cpu = true` (EDRAM ownership-transfer heuristic, "suspected"), plus an optional launcher toggle that hides shadows by setting `TrackSettings.xml` `<ShadowColor>` r/g/b to 1.0. HL lacks TS's resolve-side number-format fix.
- **XR + ReGPU:** this belongs in ReGPU's resolve and texture path. Check that (a) the resolve copy honours `copy_dest_number` (signed / integer packing of k_16_16 from 4× MSAA, exp bias) and (b) the shader translator scales fetches when `num_format` is integer. If ReGPU lacks either, fix it in ReGPU (game-neutral, so upstreamable), or in the port as a stopgap via `rewriteShader` for the specific ground-shader hash + `onDraw` constants. FM4's newer ReGPU shadow switches (`REGPU_CASCADE_SLOTS`, `REGPU_DEPTH_AS_8888`, `REGPU_CASCADE_B_LIT`) are its own car-shadow work. `execute_unclipped_draw_vs_on_cpu` has no ReGPU equivalent.

### Q20. Xenia-derived GPU correctness fixes (in one or both SDK patch sets)
Each is game-neutral Xenos behaviour that FM3 exercises (HL notes FM3 issues ~170 empty resolves/s). For each, the action is to **verify ReGPU behaves correctly**, and fix ReGPU if not.

| Fix | HL | TS | Symptom / rule | ReGPU check **(inf)** |
|---|---|---|---|---|
| Integer num_format fetch rescale | 0003 (d119505) | 0100 | see Q19 | translator fetch path |
| Sampler border alpha forced to 1 when `force_bc_w_to_max` | 0003 (c0e1129d7) | — | wrong border alpha | ReGPU border is shader-side, black/white only: add the alpha=1 case |
| Point-sample formats without linear filter support (D3D12) | 0003 (4e2964f85) | — | invalid sampling / errors | D3D12 format-support query in the texture path |
| Don't mark a texture loaded if its memory was invalidated during load | 0003 (9781a75a2) | 0008 | stale textures (road/shadow blocks) | ReGPU texture-check race (`REGPU_TEXTURE_CHECK_STAGGER` in FM4) |
| Resolve readback downscale at >1× internal res | 0003 (c02060f22) | — | garbled roads/thumbnails at 2×+ | ReGPU writeback at `REGPU_RENDER_SCALE` > 1 |
| Empty / inverted resolve region = no-op | 0003 (2d7ca4fb3) | 0023 | log spam (~170/s) | make sure ReGPU skips quietly |
| k_8_8_8_8 ↔ _GAMMA RT transfer direction | — | 0006 (+0015 Vulkan) | wrong gamma after RT reuse | ReGPU EDRAM/RT aliasing |
| Scalar op src3 component is **Z** when the paired vector op has 3 operands | — | 0009 | wrong ALU results | ReGPU ucode→HLSL translator |
| 64bpp clear: CLEAR_LO = low 32 bits, CLEAR = high | — | 0011 | wrong clear colour on 64bpp RTs | ReGPU clear path |
| Draws with `PA_SC_VIZ_QUERY.kill_pix_post_hi_z` write nothing | — | 0013 | occlusion-query draws visible | ReGPU draw filter |
| Don't fake VIZ query status as always visible | — | 0020 | occlusion culling wrong | ReGPU VIZ query emulation |

### Q21. Vulkan backend doesn't render FM3 correctly — **Agree** (both D3D12 only)
- **XR + ReGPU:** ReGPU is D3D12-only, so this doesn't apply.

### Q22. Synchronous shader compilation; invalid fetch constants allowed — **HL**
- **HL:** `async_shader_compilation = false` (async skips draws → placeholders / missing frames); `gpu_allow_invalid_fetch_constants = true`. Uses a persistent shader cache + pipeline precompile.
- **XR + ReGPU:** ReGPU's draws wait for shaders by default (`REGPU_ASYNC_*` are opt-in). A missing fetch constant reads as zero and the draw proceeds. Precompile via `Host::staticShaders()` + `pipelineSeed()`: port FM2/FM4 `static_shaders.cpp` and rescan FM3's mesh archives and xex.

### Q23. GPU plugin not selected by default — **TS** (SDK quirk, not FM3)
- Not applicable to ReGPU.

---

## 6. Frame pacing

### Q24. FM3 paces itself on **guest vblank** — **HL** (TS concurs in a settings tooltip)
- **Symptom:** the SDK's `vsync` cvar is the guest vblank timer. Off means a vblank every 1 ms, and FM3 runs too fast. The SDK's old `Sleep(1)` vblank loop fired 0–2 ms late, so half the frames went over 17.5 ms at a 16.67 ms average (judder).
- **HL:** `vsync = true` (forced), plus an undocumented part of patch 0003: a high-resolution waitable-timer vblank worker (sleep to about 1 ms before the deadline, then spin; resync if ≥4 intervals behind).
- **XR + ReGPU:** **high-priority risk.** ReGPU's `VblankLoop` fires at **2× the frame cap (120–720 Hz)** because FM2 keeps game time by the clock. If FM3 counts vblanks, it will run 2× fast or worse. Start with `REGPU_VBLANK_HZ=60` (or `driver::SetRefreshRate(60)`), measure game speed, and only then investigate whether FM3's sim/physics follow the clock (needed before any framerate unlock). ReGPU already uses precise sleep (`PreciseSleepUntil`).

- **Session 13:** FM3's main loop (`sub_821F5DE8`) runs once per vblank but takes dt from the ms tick, as FH1/FM4 do: the vblank now runs at the frame cap and `src/hooks/frame_time.cpp` gives precise dt off 60 (NOTES.md "Session 13"). "2x speed" still unmeasured.

### Q25. Present-path hook point — **HL**
- **Address:** `sub_824AF280` (XDK present path that every frame ends in); it calls `VdSwap` at `0x824AF4F4`.
- **HL:** wraps it for frame stats (`frame_stats` cvar).
- **XR + ReGPU:** `PPC_FUNC(sub_824AF280)` wrapper if needed; ReGPU already measures at `VdSwap` (`REGPU_PERF=1`). This is also a natural hook for a frame counter that drives deterministic frame dumps.

---

## 7. Audio

### Q26. Audio queue headroom — **HL** (TS: no audio fixes)
- **Symptom:** FM3 submits **256-sample blocks** (5.33 ms @ 48 kHz). The SDK's 8-block queue (~43 ms) underruns during shader compiles / readbacks, so SDL inserts silence.
- **HL:** `audio_maxqframes = 16` (~85 ms).
- **XR + ReGPU:** port `src/audio/audio.cpp` (render driver → XAudio2) and tune `kTargetQueued`/`kMaxQueued`: FM2 uses 4/8, FM4 6/10 + top-up. Start at FM4 values and raise if `FM2_TRACE_AUDIO=1` shows drops. **To check:** does FM3 drive XMA through the kernel `XMA*` exports (FM2-style) or direct APU MMIO at `0x7FEA0000` (FM4-style, needs `PPC_MM_STORE_U32` redirect)?

---

## 8. Input
- No FM3-specific runtime input fix in either project; keyboard-to-pad mapping only. TS gates input while its menu or ImGui is focused.
- **XR + ReGPU:** reuse FM2's `XamInputGetState` + SDL3 (+ wheel FFB) and `FM2_RECORD_INPUT`/`FM2_REPLAY_INPUT`.

---

## 9. Mods (Project Forza Plus 3) — **HL**
- 18 pre-extracted content packages replace the 8 UC ones; its `default.xex` is the 2009 build and is not used. It needs Q9 (re-signing), Q12 (licence mask) and Q13 (header check). Career mode is selected by swapping `Media/DB/gamedb.slt` (Normal / Hard / Sandbox / Vanilla, ± Harder AI). FOV is set in `Media/Camera/CameraSettings.ini` (`<Cam>\FOV\FOV0..2` speed bands). Shadows can be hidden via `TrackSettings.xml` ShadowColor.
- **XR + ReGPU:** FM2's mod overlay (`VfsModFile`, `zip_overlay.cpp`) is the mechanism. Out of scope until the base game runs.

---

## 10. Tooling worth reusing
| Need | ReXGlue tool | XR + ReGPU counterpart |
|---|---|---|
| Find pointer-only functions | HL `tools/find_fptrs.py` + `prune_fptrs.py`; TS `tools/analysis/ptrscan.py` | `FM4/tools/fm4_crosscheck.py` against the union list |
| Find fibers / setjmp | TS `tools/analysis/fibers.py`, `findjmp.py` | `FM4/tools/findfibers.py` |
| Dump decrypted image | HL `fm3tool image` (also the Xenia patch hash) | `FM2/tools/xexdump`, `FM2_DUMP_IMAGE` |
| ISO / STFS | HL `fm3tool iso/stfs/install-dlc` | `FM2/tools/xiso.py`, `tools/stfs.py` |
| Frame diff | TS `--texture_dump_dir`, `port_dump_to_canary.py`, `texdecode.py`, `stripes.py`; PIX capture | `REGPU_DUMP_AT`, `REGPU_DUMP_RESOLVES`, `REGPU_REGION_HASH`, `regpuctl.py shot/capture/rdcapture`, `framecmp.py`, input record/replay; `REGPU_GUEST_ABI=rexglue` to run ReGPU under a ReXGlue host for A/B |
| Scripted drive to first race | TS `tools/drive.ps1` (`Enter-FirstRace`: title → profile → Casual → R8 showcase → opening race) | record once with `FM2_RECORD_INPUT`, replay on both builds |
