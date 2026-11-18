# VINYLISSUES.md — car liveries, vinyls and the saves they touch

Everything about FM3's vinyls/liveries on our build: how the game builds a livery, every issue found (B, B2, H and
the poster bug), what fixed each one, what is still open, and how to test. State at the end of **session 11
(2026-11-08)**. The chronology is in NOTES.md's session logs (sessions 9–11); this file is the reference.

## ▶ Where to continue
**State at the end of session 42:** NOTES "Start here (session 43)" has the current open list (the menu car once lost its vinyl after an editor exit: watch for it; olive rear bumpers: compare with hl360 via `boot hl360 fiat.cmd`; write-back by CPU reads) and the method (I drive the editor; rebuild by leaving and re-entering Apply Decals; `REGPU_DUMP_RESOLVES_AT` / `REGPU_DUMP_EACH_RESOLVE`). Fiat wing fixed (below); Civic bands = layer size (below).
*(Session 41:)* NOTES "Start here (session 42)" has the current open list (Fiat wing; olive rear bumpers on Asian Car Pack cars: compare with hl360; write-back by CPU reads) and the method notes (dump the atlas outside the editor; live A/B switches).

**Session 41: the edge gaps (2 below) traced and fixed.** ReGPU sized every big pass `pitch * 64 / 65` (right only for the 2080-pitch strips): the 1400-wide 4x side views became 1378-wide images that the region composes sampled stretched by 22 columns, and the 1416-wide regions 1329-wide images. ReGPU patch `regpu-big-pass-width` (surface's own width, max 2048; `REGPU_BIG_PASS_6465=1` = old). Scion atlas read from memory (`tools/analysis/dumptex.py AB508000 2048 2048 out.png`: physical mirror address): right side +8,000 texels at the rear end, nothing else changed. User: Scion and Supra right. **CRX (3 below) fixed** by ReGPU patch `regpu-resolve-into-own-base`: its rear-bumper region is copied to the atlas's own base (texel 0,0) and replaced the whole atlas (`REGPU_NO_RESOLVE_INTO_OWN_BASE=1` = old); user: right. **Open:** the Fiat's wing; a Civic's rear bumper olive-yellow (0.8 x vinyl + 0.2 x green under it in the final atlas; compare with hl360; NOTES session 41); item 1. `REGPU_TRACE_RESOLVES=1` now logs each copy's `enclosing:` candidates.

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

## Civic hood/fender bands (session 42: not a bug, the layer's size)

Red strips along the hood edges, fender tops and next to the headlights on the user's 1994 Civic. The hood/roof region is one
1640x744 copy at atlas (224,896), built from a 4x big pass (1616x728) whose scratch image is at the atlas's own base, with
the panel mask at 13D91000 (each panel has its own 1024x1024 linear k_8 mask). Diffing the vinyl image against the mask
(the user's idea) showed the vinyl cut by two straight lines ~10% from the region's top and bottom: the user's layer edge.
Enlarging every layer quad 8x (live `vs 7061152fe7f7fc72 opos.xy = r2.xy * 8 * opos.w;`) covered the bands. The user made
the layer taller: covered. Left: a few red pixels on panel borders (the mask's soft edge + filtering: console behaviour).
The mask also has thin seams between the hood and the fender-top bands (game artwork).

**Seen once, open:** after an edit and leaving the editor the menu car showed only the front bumper's vinyl (NOTES
"Start here (session 43)" item 1 for what was ruled out and the lead).

## Fiat wing (session 42, fixed: ReGPU patch 76 `regpu-depth-reset-color`)

**Cause, found with the new per-copy dumps (`REGPU_DUMP_EACH_RESOLVE=152x424`):** the high-detail vinyl pass (4x, pitch 160, EDRAM
colour base 0) clears columns 120-146 with a colour rectangle but columns 0-120 only with depth-only (EDRAM mode 5) stencil resets,
one of them on a depth surface at base 0, the colour's own EDRAM: on the console that zeroes the colour too. ReGPU keeps depth and
colour in separate surfaces, so the colour kept what the car-shade pass (a 256x256 image at the same base, cleared with a
120x128 rectangle through a 4x view, scaled to 240x256) left there with alpha 1, and the layers, blended front to back by the
destination's alpha (`RB_BLENDCONTROL 010B010B`), added nothing in that 240x256 block: the wing's first 256 rows of the image.
Fix: a texture-less mode-5 rectangle whose depth base equals the colour base also writes the colour with its own output (zero
here). A second change (the session-16 clear scale only when the pass itself shows the 1x layout) was tried and dropped: not
needed. Verified: editor A/B by re-entering Apply Decals (off: half white; on: fully yellow), frozen grid 0 pixels.

### The trace (kept for the method)

Symptom: the wing's left half takes the vinyl, the right half stays bare paint (white on the test car).

- **Where it is lost:** the wing is its own 176x440 region (copied to the atlas at texel 1792,1024). In the final atlas its
  first 272 rows are bare paint `204,204,204` (0.8 x white) and rows 272-439 have the vinyl `204,186,81`; the boundary is
  exact (atlas row 1296). Read with `FM3_WRITEBACK=2048x512` live (strips written back) + `dumptex.py AC569000 2048 2048`.
- **How the region is built** (frame captures while the user moved the layer; the editor rebuilds only on edits):
  1. a vinyl image: clear, a stencil from the panel mask (k_8 1024x1024 linear texture at `1362C000`, draw `5d606c../cb430d..`),
     the layer quads (`7061152f..`, colour in pc0, 32x32 shape texture `10F86000`); copied as 73x212 (1x, pitch 80) or, in
     other rebuilds, a **high-detail 146x424 4x MSAA pass** (pitch 160, depth at EDRAM tile 212);
  2. an 8-bit coverage image of the same size (`04C3xxxx`);
  3. the region draw (`1040b9c5../a9a8e665..`): out = lerp(atlas at the region, vinyl image, panel mask `.w`) (t3 coverage only
     when pc0.x > 0); 4. the strip pass (`0bafd65c..`): final = 0.8 x atlas + base DXT5 `141A4000` x its alpha x (1 - atlas alpha).
- **Ruled out:** the user's layer coverage (vs edit `opos.xy = r2.xy * 8 * opos.w` covered everything: no change); the stencil
  mask draw (ps forced to pass: no change); the panel mask texture (memory has the whole wing shape; `REGPU_FULL_CONTENT_HASH 1`
  re-decoded it: no change; showing `t2.w` as colour gave 1 on every panel); the low-detail vinyl image (capture image
  `04F77000_192x424` is yellow over the whole wing shape).
- **Suspect:** the high-detail 4x vinyl image (capture `059DE000_320x848_v0`: white rows 0-255, yellow 256-319, black below:
  row 256 of it = region row 272) and its texture-less clears through the 4x view (pitch 160, which shares its host surface
  with the roof region's 1x pitch-320 pass). `REGPU_NO_MSAA_COLOR_SCALE 1` changed the white area's shape (not fixed). But a
  later rebuild without any 152x424 copy was still half white, so it may not be the whole story.
- **Next:** add a debug-server command that dumps a resolved image by address (they persist between rebuilds), so each
  input of the region draw can be read at idle without the user editing; then A/B `REGPU_NO_MSAA_COLOR_SCALE`,
  `REGPU_NO_MSAA_SCALE`, `REGPU_NO_DEPTH_VIEW_SCALE` on the high-detail pass. Method notes: `FM3_WRITEBACK` doesn't reach
  copies drawn into their own base image (patch 74 path); a captured rebuild needs the capture loop while the user moves
  the layer; `REGPU_DUMP_TEXTURES` during captures decodes 8888 textures without their swizzle (wrong colours until restart).

## Why these bugs keep coming back (session 41 analysis)

**The model mismatch.** On the console every copy (resolve) just writes bytes into one flat memory; a livery atlas is
whatever the strips and region copies wrote into its address range, and a texture fetch reads that memory. ReGPU keeps
each copy as its own GPU image in a map keyed by the copy's exact base address (`g_r.resolved`), and a fetch finds an
image only at its exact base. Everything FM3 composes out of several copies (the atlas from 4 strips + one region per
vinyl tab; each 4x side view from 160/192/224-row tiles) therefore depends on rules that guess which earlier image a
copy belongs to: `ResolveIntoEnclosing` (row width equal to the pitch or the big-pass width, declared height equal to
the rows left, base strictly lower, most recently used, row clip), `placedInImage`, height keep, the big-pass width
(`pitch*64/65`), and since session 41 the own-base rule. Each car's editor uses its own region sizes and positions, so
each car can hit a combination no rule covers: the Scion's 1400-wide side views (64/65), the CRX's region at the atlas
base, the Civic's 1616/1632 tiles at bases left over from the last build. Fixing car by car is fixing the guesses.

**The siblings have the same model and the same open bugs.** FM2 port: images keyed by exact address, livery support
is pitch/scissor special cases (the 64/65 rule, 2080/1040 layout keys, tall/foreign-MSAA views), no handling of a copy
at an x,y inside another image; still open there: report #50 "layers land in the wrong places", a moved decal
vanishing, race livery corruption. FM4 port (vendored ReGPU): the same FM2-era livery fixes, no enclosing logic, no
livery write-back at all; its GitHub issue #3: "decals don't apply on cars most of the time". Its
`docs/FM4-RENDERER-PORT.md` lists "CPU readback tracking instead of size-keyed writebacks" as a plan only.
FH1 ReImagined (ReXGlue, Xenia's model): every resolve is written into a GPU copy of guest memory and textures are
read from memory by address range, so copies compose by themselves; its livery bugs were of another kind (below).

**A second, separate cause (from FH1's notes): draws skipped while their pipeline builds.** The game builds a livery
once per car load. ReGPU skips a blended draw (vinyl layers blend `010B010B`) whose pipeline is not built yet, and an
opaque one once the game has settled (`SkippableDraw`); a skipped layer stays missing until the car is rebuilt. Warm
caches hide it (0 skipped in session 41's runs); first boots and the Steam Deck can show it as random missing pieces.

**What would end the guessing** (1 and 2 built in session 41 as ReGPU patch `regpu-copies-by-range`: `REGPU_ENCLOSING_GUESS=1` and `REGPU_SKIP_OFFSCREEN=1` give the old behaviour; 3 not built):
1. *Copies by address range.* Keep resolved images with their tiled byte range (pitch rounded to 32, rows rounded to
   32, bytes per texel). A copy is drawn into every live image of the same row width and format whose range holds the
   copy's destination, at the x,y its tiled offset gives (inside, at the base, or a tile of it); a new image is made
   only where none covers it (and an image a new larger copy covers is drawn into that one). This replaces the width,
   declared-height, base, own-base and pass-width rules with one rule that is what the memory does.
2. *Livery passes never skip draws.* Only the main 1280-wide views may skip a draw while its pipeline builds (they are
   redrawn next frame); every off-screen pass waits.
3. *Write-back by what the CPU reads,* not by size (the 2048/1024 livery rule): later, with read tracking on the
   resolve destinations (FM4's plan).
Each behind a `REGPU_*` switch, checked on the Scion, Supra, Fiat, CRX and Civic atlases read from memory
(`dumptex.py`) plus the frozen grid.

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
| **H again?** (S44) open | Buying a car on a copy of the **current Baseline** save crashes the same way (`indirect call to 00000000` from `sub_827C5FD8`); the session 41 backup `s41-before-vinyl` buys the same car fine | Garage damage again, some time in the session 41-42 vinyl/editor work? Not checked: hl360 on a copy, My Cars complete? | — | — |

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
- **Reading the livery atlas itself (session 41):** `python -I tools/analysis/dumptex.py A<address> 2048 2048 out.png [--raw f.bin]` (address from the log's `livery writeback:` line, plus `A0000000`): the CPU-side 8888 atlas, untiled; diff two runs' PNGs per colour (session 41's scripts in `out/vinyl/`).
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
