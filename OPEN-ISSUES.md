# Open issues

State after session 9 (2026-11-08). The vinyl issues B (vinyls cut along a straight line) and B2 (races: a car with vinyls
covered in noise) were fixed in session 11, with issue H (a vanishing car); everything vinyl-related, including the
small leftover gaps, is in [VINYLISSUES.md](VINYLISSUES.md). This file
lists everything else that is known to be open, so it can be picked up in any order.

Each entry gives: what you saw (with the on-screen timer value of your screenshot, which is the `[time]` in the log of
that run, `out/logs/session9-explore.log`), what the log and the code already show, what the FM2/FM4 ports did, and a
first step. "Live" means it can be tried with `tools/fm3ctl.ps1` while the game runs, without a rebuild.

Fixed in session 9 and **not** in this list: A (matte paint, black chrome), C (My Cars picture and thumbnail), the
race-load crash. See NOTES.md for those.

## Rendering

### D. Upper third of the screen slightly washed out (pre-race menu, photo mode)
- **Seen:** pre-race menu (timer 119.3) and photo mode (1219.9). A lighter band over the top of the 3D view, with a
  straight lower edge at about row 262 of 720. Not seen while racing.
- **Known:** nothing in the log yet. Both screens show the 3D scene behind a menu, or a paused, re-rendered scene,
  so the scene is probably drawn or composed differently from racing.
- **Sibling ports:** FM4 had photo mode's 64-bit picture accumulation sharing EDRAM with the 32-bit scene, which bled
  into the photo. It fixed this with `REGPU_COLOR_SURFACE_PER_BPP`, which exists only in FM4's own ReGPU and would
  need porting. FM2 fixed several photo-mode tile problems.
- **First step:** in the pre-race menu (easier to reach than photo mode), pause, then `Fm3 "capture"`. Find the
  pass that writes rows 0 to about 262 (FM3's scene tiles are rows 0–511 and 512–719, so 262 is not a tile edge;
  check for a 64-bit surface sharing an EDRAM base). Live A/B: `switch REGPU_NO_TILE_MERGE 1`,
  `switch REGPU_NO_CLEAR_EXTEND 1`. Measure the band with `tools/analysis/boxes.py` (one box above the edge, one
  below), not by eye.

### E. Flickering shadow on the player's car from other cars
- **Seen:** intro race (266.2). A shadow patch flickers on the right rear of the R8 when other cars are near. It
  shouldn't be there.
- **Known:** session 8 fixed the car's own soft shadow (no square any more). The cars' shadow cascades have not been
  checked against other cars.
- **Sibling ports:** FM4 fixed a close match. Its soft car shadow has three tiles, and the tile holding other cars'
  silhouettes is built from a depth copy that the shader reads as packed 8888 bytes. It needed `REGPU_DEPTH_AS_8888`
  plus `REGPU_CASCADE_*` switches, which exist only in FM4's ReGPU (`Fuzzy-Rittersport-4-ReGPU/src/gpu_host.cpp`
  lines 113–125, and its vendored `renderer.cpp`). FM4 also fixed a "stray shadow" from depth bases shared between a
  1× caster pass and a 4× pass.
- **First step:** in a race with cars close by, pause, `Fm3 "capture"`. Find the car-shadow shader and its textures,
  and check whether one is a depth resolve fetched as k_8_8_8_8. ReGPU already returns a depth image's guest D24S8
  words when it's fetched as 8888 (`REGPU_NO_DEPTH_PACK`), so check that path works for this copy before porting
  FM4's switches.

### F. Dithering / stipple on foliage and fences
- **Seen:** 365.8, on trees and fences; it looks like anti-aliasing.
- **Known:** our build renders at host MSAA 1× (log: `renderer: anti-aliasing 1x`). At 1×, upstream ReGPU emulates
  alpha-to-coverage with a 2×2 dither, as Xenia does. That is the stipple.
- **Sibling ports:** FM4 uses plain D3D alpha-to-coverage (a hard cut at alpha 0.5, no dither). FM2 the same.
- **First step (live, 1 minute):** in a race near trees, `switch REGPU_NO_ALPHA_DITHER 1` and compare screenshots.
  Also try a higher host MSAA (`set FM3_MSAA=4` before starting, or the `msaa` key in the settings file read by
  `src/settings.cpp`; needs a restart): with real MSAA the coverage is smooth. Decide between hard edges (`REGPU_NO_ALPHA_DITHER=1` as an FM3 default in `src/gpu_host.cpp`) and MSAA.

### Smaller rendering notes
- The k_2_10_10_10 decoder (session 9) keeps 8 of 10 bits. If reflections ever show banding, add a native
  `R10G10B10A2_UNORM` upload (the cube path always decodes to 8-bit today).
- The car paint also binds a second reflection cube (`1832E000` in the session-9 showroom capture) that is all
  zero in memory and never resolved into while in the showroom. It may be unused there, or only live in races. The
  showroom looks right without it; worth a look if race reflections ever look wrong.

## Audio

### G. Engine sound cuts out while holding RPM
- **Seen:** 1027.1, holding revs in a solo race. You mentioned it's known across the 360 recomps.
- **Known (strong lead):** our XMA stats (`xma: 5 s:` log lines) show **0 "discontinuity flushes" in menus,
  110–200 per 5 s in a solo race, 500–730 in the 6-car intro race**. Each one comes with codec reopens and 4–11k
  decoded samples dropped at "stream restarts". So our seek guard flushes the decoder about 30 times a second while
  driving and throws away audio already decoded. Both behaviours are ours (session 7) and have live switches.
- **Sibling ports:** we already carry FM4's queue depth, top-up, decoder boost and "consumed means delivered". FM2
  additionally decodes engine loops with a fresh codec per restart, plus a seamless-loop table and crossfades
  (`Forza-Motorsport-2-ReGPU/src/audio/xma.cpp` lines 81–111 and 413–459, `seamless_loops.h`).
- **First step (live):** in a race, hold revs, and listen with `switch FM3_NO_XMA_DROP_AHEAD 1`, then also
  `switch FM3_NO_XMA_SEEK_GUARD 1`. Watch the `xma: 5 s` lines. If cut-outs stop with drop-ahead off, the guard's
  "this packet doesn't follow" test is too strict for engine loops (`src/audio/xma.cpp` around line 825).
  `FM3_TRACE_XMA=1` (set before starting) logs each flush with its buffer and packet numbers.

## Content and system (Phase 4)
- **Disc 2 DLC cars** (FM3-QUIRKS Q11–Q14): Disc 2 content must be installed (no disc swap), the licence mask must
  grant all bits, header-less packages listed twice, `fileName` may be unterminated. Not tested yet. The log shows
  `XamSwapDisc` / `XamSwapCancel` unimplemented (timer 2070): the game asked for a disc swap.
- **Profile rules** (Q15: XUID and gamertag constraints) and **language/country** (Q16): not checked.
- **Unimplemented imports seen:** `XamParseGamerTileKey`, `XamReadTileToTexture` (gamer picture, 1598.9),
  `XamShowMarketplaceUI` (2077). Harmless so far; stub them properly when Phase 4 gets there.
- **Title update files:** every race load logs `vfs: no mapping for "Update:\media\cars\driver\*.gr2"` (steering
  lock and shifter models). We don't mount an `Update:` drive. Check whether a title update exists for this
  version and whether these files matter (driver-view animations?).
- Career and menus beyond what session 9 visited: not explored.

## Performance and parity (Phase 5)
- **Audio queue headroom** (Q26): a check beyond issue G.
- **Post-race finish menu at 30.0 fps:** hl360 locks it at 30 (probably the game's own behaviour); check ours.
- **Frame rate over a full lap**, after the shader cache is warm (the first seconds after a cache rebuild run at 30–36
  fps).
- **ReGPU-side frame dump** in hl360's `index.txt`/`tex_*.bin` format, so `tools/dumpdecode.py diff` can compare
  hl360 and our build directly. That would speed up finding parity bugs like D and E.
- Frame-rate unlock (Phase 6) only after FM3's simulation timing is confirmed (Q24: FM3 paces itself on guest vblank).

## Leftovers
- `sub_82A918F0` blrl trap and the SEH policy (not hit yet).
- `githubfileignorepls` at the project root: delete it? (It's yours; I haven't touched it.)
