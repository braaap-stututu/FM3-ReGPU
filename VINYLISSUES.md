# VINYLISSUES.md — car liveries, vinyls and the saves they touch

Everything about FM3's vinyls/liveries on our build: how the game builds a livery, every issue found (B, B2, H and
the poster bug), what fixed each one, what is still open, and how to test. State at the end of **session 11
(2026-11-08)**. The chronology is in NOTES.md's session logs (sessions 9–11); this file is the reference.

## ▶ Where to continue
**Session 16: livery broken on some race loads — fixed.** Each load builds the livery in a new image a few KB above the previous load's; ReGPU's enclosing search took the lowest matching base (the old image), and the write-back safety check then dropped those pieces. ReGPU patch `regpu-livery-enclosing-recent`: only images used in the last 120 frames match (`REGPU_ENCLOSING_LOWEST=1` = old); the safety check now compares per texel inside its own rectangle and logs where memory changed (`REGPU_WRITEBACK_REUSE_SPAN=1` = old). Several reloads clean (you). If a livery piece goes missing again: grep `memory changed since the resolve` and `fits N images` in the log.

1. **Confirm the final build in normal play** (not yet done): the last part of the H fix (written-back mark reset)
   was built after the last test run. Play with the `Baseline` profile, paint/vinyl several cars, buy one, and
   check that **My Cars keeps every car**. If a car vanishes, read the log first (see "If a car vanishes again").
2. **Small uncovered spots** (B leftovers), all at the edges of the big vinyl regions:
   - Scion xD, right side: a strip **beside the rear wheel arch** and a spot **at the rear door window**.
   - Fiat Abarth 500: similar spots, plus **a chunk of the rear wing** (the wing is its own vinyl tab).
   - Toyota Celica Supra '84: a **small area on the rear bumper, both sides**.
   First step: look at the same spots on **hl360** (`boot hl360.cmd`, its own Baseline save has the same cars only if
   you paint them there; or swap a save in, see "hl360 comparison"). If hl360 covers them, it's ours. Leads below.
3. **Honda CRX: vinyls invisible in the paint booth** (seen once, in the build that still had the poster bug).
   Probably the poster bug (a region drawn into the wrong image); re-check with the current build first.

## How FM3 builds a car's livery (what ReGPU has to get right)

- The car's paint+vinyl texture is an **8888 2048×2048 atlas** (some cars may be smaller: the Fiat's sizes are not
  checked yet). It is built on the GPU from **four 2048×512 strips** (EDRAM pitch 2080 → a ReGPU *big pass* at
  scale 0.5). Each strip is resolved to its own address 4 MB apart, declaring "rows left to the end" as its height
  (2048, 1536, 1024, 512): ReGPU sees the first strip as a 2048×2048 image and draws the others into it
  (`ResolveIntoEnclosing`, patch `regpu-resolve-into-enclosing`).
- Each **vinyl region** (one per editor tab: front bumper 704×264, rear bumper 672×360, top/hood 1288×600, left side
  1416×544, right side 1416×568, sizes from the Scion) is composed in its own pass and resolved **into the atlas at
  its tile position** (pitch 2048, a whole number of 32×32 tiles from the atlas base). The big ones (sides, hood) are
  big passes drawn scaled down (0.723, 0.765); the bumpers are ordinary passes at scale 1.
- A region's compose reads a **side view**: for the big regions a **4×MSAA pass resolved in 224-row tiles**
  (e.g. 1400×534 at pitch 1408 → tiles of 224, 224, 88 rows; 1280×592 → 256, 256, 80), plus 704×267 1× side views.
  Edits in the editor recompose from the 1× side views; the 4× tiled passes run **when the car loads**.
- A second strip pass (colour base `0C0`) redraws the four strips from the atlas + the car's base DXT5 texture.
- Then a **1024×1024 race livery** is built the same way (base + regions). The game's **CPU compresses** both
  (2048 → close-ups: photo mode, race countdown; 1024 → racing) **to DXT from memory**, so the GPU result must be
  written back to guest memory. This happens at boot/garage load and after painting, **not** at race load.
- **Car pictures** (My Cars): a 512×256 picture and a 256×128 thumbnail are resolved and copied by the CPU into the
  car's saved images (`Thumbnail_<n>.xdc`), also written back.
- Save files per car: `ForzaProfile/Liveries/<n>.liv`, `Thumbnails/Thumbnail_<n>.xdc`; the garage itself is in the
  encrypted `PlayerDatabase` (`cmss` container, fixed 6,291,464 bytes, ~2.2 KB of real data at the start; one car
  ≈ 192 bytes of it).

## Issues and fixes

| Issue | Symptom | Cause | Fix | A/B switch |
|---|---|---|---|---|
| **B** (S9–11) ✅ | Vinyls invisible, then cut along a straight line on sides and hood (editor, then races too) | (S9) regions kept as their own images, not drawn into the atlas. (S11) the big side-view passes' **tiles 2–3 never reached their image**: big-pass images are kept at the pass width (pitch·64/65 = 1378) while the tiles name the copy pitch (1408); the last tile's rect is 8-row rounded past the image (88 vs 86) and was refused | ReGPU patches `regpu-resolve-into-enclosing`, `regpu-enclosing-big-pass` | `REGPU_NO_RESOLVE_INTO_ENCLOSING`, `REGPU_NO_ENCLOSING_PASS_WIDTH`, `REGPU_NO_ENCLOSING_ROW_CLIP` (the last two act at the next car load) |
| **B2** (S9–11) ✅ | Race: car covered in noise; photo mode/countdown: other cars' decals + noise; badges missing | Livery built on the GPU, compressed by the CPU from memory that ReGPU never wrote | `src/gpu_host.cpp` **livery rule**: 8888, top 0, 2048×2048 or 1024×1024 → write back **Deferred**; ReGPU patch `regpu-enclosing-writeback`: strips/regions drawn into a written-back image queue a Deferred write-back of their rectangle | `FM3_NO_LIVERY_WRITEBACK`, `FM3_LIVERY_WRITEBACK_NOW` (Now: ~55 ms stall per 2048 image), `REGPU_NO_ENCLOSING_WRITEBACK` |
| **C** (S9) ✅ | My Cars pictures noise/missing | Picture resolves read by the CPU | `src/gpu_host.cpp` picture rule (512×256, 256×128 → Now). Old pictures fix themselves when the car is repainted | `FM3_NO_PICTURE_WRITEBACK` |
| **Poster bug** (S11, mine) ✅ | A career event poster showed a car's purple front-bumper vinyl; probably also the invisible CRX vinyls | My first B fix applied the pass-width match to ordinary passes too: a 2048-pitch bumper region matched any 1280-wide image below it | Pass width only when the current pass is a big pass (patch `regpu-writeback-safety`) | (part of `REGPU_NO_ENCLOSING_PASS_WIDTH`) |
| **H** (S11) ✅, final build unconfirmed | A car **vanished from My Cars** after painting another car; the damaged save then **crashed on buying a car** (`indirect call to 00000000` from `sub_827C5FD8`, vtable slot 80 null), in hl360 too | Garage record lost **in game memory** (the save that followed wrote 192 bytes less car data). Every losing run had the poster bug; likely the region drawn into the wrong image was written back into game memory because that image's **written-back mark was stale** (never reset after the address was reused) | Patch `regpu-writeback-safety`: pass-width fix; **written-back mark cleared** by a resolve that isn't written back; **deferred write-backs skip memory the title changed since the resolve** (dense hash, own writes excluded; same as FM4's ReGPU `ClaimAndWrite`). `src/indirect.cpp` logs the caller stack on a null indirect call | `REGPU_KEEP_WRITEBACK_MARK`, `REGPU_NO_WRITEBACK_REUSE_CHECK` |

H evidence in short: the purchase crash came from the damaged save (hl360 crashed on a copy). On a clean copy of
hl360's save our build lost the Scion after buying+painting a Supra and painting the Fiat; **hl360 kept it** on the
same save. Runs with the poster fix kept it (write-backs all off, livery off, everything on with the same sequence);
the reuse check never fired. **Not proven step by step** (the old poster behaviour has no switch to replay it).

## Leads for the B leftovers

- **Edge columns:** the big 4× side-view pass is copied **1378 wide** (`rect 0,0-1378,…`) but the region compose
  fetches it as **1400×534** (`t0=8888 1400x534@…`): the last 22 columns are never written by the copy. Where do they
  land on the car, and what does the console have there? (Its memory: whatever was there before.)
- **UVs outside the side view:** the region quads sample −0.0086…1.0029 horizontally and −0.052…1.011 vertically
  (`vf95 floats:` in a capture): clamped/borders at the region edges.
- **Rear wing (Fiat):** its own tab = its own region; check its pass (size, big or not, tiles) in a capture at load.
- Live A/B of the enclosing switches needs a **car reload** (the 4× tiled passes run at load; a nudge only
  recomposes from the 1× side view).

## Tests, tools and saves

- **Profile:** `play.cmd` starts profile **`Baseline`** (`out/user/profiles/Baseline`): a copy of hl360's Baseline
  save with its XUID `E0000BA5E11E0001` (file `xuid`; saves are signed for their owner; gamertag = `FM3_PROFILE`).
  The old `Player` profile's save is **damaged** (buying crashes, also in hl360); kept for reference only.
- **Backups** (`out/profile-backups/`, all whole profile folders): `rewardcar` (Baseline right after winning the
  Fiat: the clean restart point for H), `multicar` (the run that lost the Scion), `ab-all-writebacks-off`,
  `ab-livery-off`, `Baseline-test-before-reward-1535`, `hl360-Baseline-E0000BA5E11E0001` (hl360's own save, original),
  `hl360-test-after-rewardcar-run`, `Player-*` (the damaged profile at several points).
  **Only replace a profile while the game is closed:** the game saves on exit and overwrites it (happened twice).
- **hl360 comparison on one of our saves:** hl360's Baseline profile has the same XUID, so a Baseline backup can be
  swapped in: check hl360 is closed, `diff -rq` its `user/E0000BA5E11E0001` against `hl360-Baseline-E0000BA5E11E0001`,
  replace `4D53084D/00000001/{ForzaProfile,LivCatalog}` (keep its `Headers`), test with `boot hl360.cmd`, then restore
  the original and `diff -rq` again. A profile with another XUID can be run with
  `fm3.exe --profile_xuid=<xuid> --profile_name=<name>` from its folder (CLI cvars outrank the toml); it needs
  `Headers/00000001/<container>.header` files (328 bytes: two dwords, then the display name UTF-16BE).
- **Save I/O trace:** `FM3_TRACE_WRITES=ForzaProfile` logs every write to the profile files with the writer's stack;
  the `PlayerDatabase` write sizes show a lost car (data shrinks by ~192 bytes).
- **Measuring vinyl coverage:** `python -I tools/analysis/greentop.py <shot>` (editor side view of the right side,
  1280×720 shot: median top row of the green over x 450–750, green pixel count). Scion values: 262 cut, 215 after
  the first B fix, 124 with both (green px 38,655 → 80,235).
- **Driving the editor yourself:** `Fm3 "press A"` twice opens a layer's move mode; `Fm3 "stick L 0.5 0 120"` nudges it;
  `press LB/RB` changes tabs. After each edit the car flashes the highlight colour (blue) for a moment: measure a few
  seconds later.
- **Capturing a rebuild:** `Fm3 "stick L 0.5 0 60"` then `Fm3 "capture"` right after, repeat until the traced frame
  has ~1300+ draws (an idle editor frame has ~1230). Read `resolve: control … rect …`, `also drawn into` and the
  draws before each region resolve; region images land in `out/run/out/res` (check the file time). The boot build
  is in the log at ≈22 s (2048) and ≈29 s (1024) with `REGPU_TRACE_RESOLVES=1` + `REGPU_TRACE_RESOLVES_MIN=1024`
  set before start.
- **If a car vanishes again:** set `FM3_TRACE_WRITES=ForzaProfile`, reproduce from `rewardcar`, then read the log for
  `writeback: … the memory changed since the resolve` lines and the `PlayerDatabase` write sizes per save.

## Lessons from session 11 (don't repeat)

- A traced frame is not a normal frame: session 10's "traced edits come out complete" was an artefact of a removed
  cache experiment still compiled into that exe. Slowing frames to traced speed (`REGPU_PACKET_SPIN_US`, removed)
  proved B was not timing.
- The hook's `ResolveCopy` height is the **declared** height (rows left to the end), not the rect: a strip of
  2048×512 shows up as 2048×2048, 2048×1536, …
- An A/B run is only clean if nothing else changed: the "write-backs off" runs also carried the poster fix.
- The FM4 port's history (GitHub `Aiden-Piercee/Fuzzy-Rittersport-4-ReGPU`, 304 commits; the local clone is one
  squashed commit) is worth querying: it had already met the "late write-back onto reused memory" problem
  (`ClaimAndWrite`, `REGPU_WRITEBACK_OVERWRITE`) and turns fence-held write-backs off for speed
  (`REGPU_NO_WRITEBACK_FENCES`, `REGPU_EARLY_FENCES` in its launchers).
