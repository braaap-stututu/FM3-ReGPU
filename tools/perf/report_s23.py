"""Session 23 picture report: every comparison of the hands-off performance tests as one local HTML page.

  python -I tools/perf/report_s23.py pairs out/perf/s23      # writes pairs.txt (for make_pairs.ps1)
  powershell -File tools/perf/make_pairs.ps1 -Dir out/perf/s23 -Pairs pairs.txt
  python -I tools/perf/report_s23.py html out/perf/s23       # writes report.html (open it in a browser)

A = reference (switch off / defaults), B = the test. The red picture is computed from the original PNGs: B's pixels in
grey, red where A and B differ by more than 8 in a channel. Numbers are from PERF-RESULTS.md / results.csv.
"""
import html
import os
import sys

# (section, key, title, A file, B file, A label, B label, numbers, verdict, kind, note)
# kind: good (gain / keep), bad (loss / broken), same (no effect), info
S_GRID = "Is the test grid the same on every boot?"
S_P1 = "Phase 1: live switches, one game session (1440p 4x MSAA)"
S_P2 = "Phase 2: start-time switches, separate boots (1440p 4x MSAA)"
S_L11 = "L11: ring wait (made default)"
S_P4 = "Phase 4: CPU-bound (1280x720, no MSAA)"
S_P5 = "Phase 5: promising pieces alone and stacked (round 1 shown)"
S_P6 = "Phase 6: which preset piece broke the race picture"
S_P7 = "Phase 7: presets after the fix"
S_FIN = "Final build"
CROSS = "Separate boots: the crowd, pit crew and flags always differ between boots (own animation clock); judge the cars, track, sky and HUD."
SAME = "Same game session, world frozen: any red pixel is the switch's doing."

ITEMS = [
    (S_GRID, "grid-b1b2", "Boot 1 vs boot 2", "boot1.png", "boot2.png", "boot 1", "boot 2",
     "Same 7 AI drivers (ids 1, 26, 15, 16, 5, 30, 31), same order, same grid spots; frozen at 9.001 / 9.003 s", "Line-up identical", "good",
     "Red = the animated crowd, pit crew and flags, plus single pixels on AI car edges."),
    (S_GRID, "grid-b2b3", "Boot 2 vs boot 3", "boot2.png", "boot3.png", "boot 2", "boot 3",
     "Same line-up; 219.5 vs 217.4 fps", "Line-up identical", "good", "One AI tail light lit in one boot only."),
    (S_GRID, "grid-same", "Same boot, 2 s apart", "boot1.png", "boot1-b.png", "boot 1", "boot 1, 2 s later",
     "0 pixels differ", "Byte-identical", "good", "Within one session the frozen grid never changes."),
    (S_GRID, "grid-fixed", "Fixed frame steps (FM3_FIXED_DT=1), two boots", "fixeddt1.png", "fixeddt2.png", "boot 1", "boot 2",
     "Freeze at exactly 9.000 s both times; 213.6 / 220.9 fps", "Crowd still differs", "info", "Fixed steps don't make boots identical."),

    (S_P1, "AA", "Null A/B (an unused variable flipped)", "AA-off.png", "AA-on.png", "off", "on",
     "214.3 -> 213.8 fps (-0.2%), on faster in 0/4 rounds", "Noise floor", "info", SAME),
    (S_P1, "L1", "L1 vertex arena (on = arena turned OFF)", "L1-off.png", "L1-on.png", "arena on (default)", "arena off",
     "204.2 -> 184.7 fps: the arena is worth +10.5%, 4/4 rounds", "Keep (default)", "good", SAME),
    (S_P1, "L2", "L2 alpha-test-free pixel shaders", "L2-off.png", "L2-on.png", "off", "on (default)",
     "204.6 -> 213.0 fps (+4.1%), 4/4 rounds", "Keep (default)", "good", SAME),
    (S_P1, "I4a", "I4a index lists hashed once a frame", "I4a-off.png", "I4a-on.png", "off", "on",
     "213.0 -> 212.7 fps (-0.1%)", "No effect", "same", SAME),
    (S_P1, "I4b", "I4b 2_10_10_10 copies kept in 10 bits", "I4b-off.png", "I4b-on.png", "off", "on",
     "213.1 -> 218.0 fps (+2.3%), 4/4 rounds; worst difference 2/255", "Gain, rounding only", "good", SAME),
    (S_P1, "I5", "I5 stable upload reuse", "I5-off.png", "I5-on.png", "off", "on",
     "213.2 -> 213.2 fps (0%)", "No effect", "same", SAME),
    (S_P1, "A2M", "Alpha to mask as a plain test", "A2M-off.png", "A2M-on.png", "off", "on",
     "213.1 -> 213.0 fps (0%)", "No effect", "same", SAME),
    (S_P1, "I8live", "I8 anisotropy from the fetch (live part)", "I8live-off.png", "I8live-on.png", "off", "on",
     "213.1 -> 213.1 fps; 0.13% of pixels change (texture filtering)", "No gain live", "same", SAME),
    (S_P1, "I1", "I1 hidden-part cull (1 px)", "I1-off.png", "I1-on.png", "off", "on",
     "213.2 -> 210.4 fps (-1.3%), 0/4 rounds", "Loss", "bad", SAME),
    (S_P1, "I1burst", "I1 flicker check: first vs sixth shot in a row, cull on", "I1-burst0.png", "I1-burst5.png", "shot 1", "shot 6",
     "6 shots, all identical", "No flicker", "good", SAME),

    (S_P2, "L3", "L3 optimised shaders (REGPU_OPTIMIZE_SHADERS=1)", "P2-BASE-defaults-0.png", "L3-optimize-boot2.png", "defaults", "optimised, boot 2",
     "boots 219.6 / 207.3 fps vs defaults ~220.5: -6% once the optimised shaders are in", "Loss", "bad", CROSS),
    (S_P2, "I2-lean1", "I2 lean texture fetch, level 1", "P2-BASE-defaults-0.png", "I2-lean1-boot2.png", "defaults", "lean fetch 1",
     "boots 227.0 / 225.7 fps (+2.6%)", "Gain", "good", CROSS),
    (S_P2, "I2-lean2", "I2 lean texture fetch, level 2", "P2-BASE-defaults-2.png", "I2-lean2-boot1.png", "defaults", "lean fetch 2",
     "boots 229.9 / 218.6 fps (boot 2 unsteady); phase 5: +3.6%", "Gain", "good", CROSS),
    (S_P2, "I2-leanv", "I2 lean vertex fetch", "P2-BASE-defaults-2.png", "I2-leanv-boot2.png", "defaults", "lean vertex fetch",
     "boots 221.7 / 220.7 fps (+0.3%)", "No effect", "same", CROSS),
    (S_P2, "I3", "I3 pixel shaders multiply as IEEE (plain multiply)", "P2-BASE-defaults-4.png", "I3-plainmul-boot2.png", "defaults", "plain multiply",
     "boots 226.5 / 226.5 fps (+2.7%)", "Gain (needs a drive)", "good", CROSS),
    (S_P2, "I8", "I8 anisotropy from the fetch", "P2-BASE-defaults-4.png", "I8-aniso-boot2.png", "defaults", "anisotropy from fetch",
     "boots 224.3 / 224.3 fps (+1.7%); 12% of pixels change", "Small gain, visible", "info", "Softer distant textures (filtering as the game asks; closer to the console)."),
    (S_P2, "L9", "L9 submit every 512 draws", "P2-BASE-defaults-6.png", "L9-submit512-boot2.png", "defaults", "submit 512",
     "boots 207.7 / 208.0 fps (-5.7%)", "Loss here", "bad", CROSS),
    (S_P2, "L13", "L13 draw-thread pieces", "P2-BASE-defaults-6.png", "L13-pieces-boot2.png", "defaults", "pieces",
     "boots 214.5 / 216.1 fps (-2.3%)", "Loss", "bad", CROSS),
    (S_P2, "DToff", "Draw thread off (reference)", "P2-BASE-defaults-8.png", "DT-off-boot2.png", "defaults", "draw thread off",
     "boots 182.2 / 181.7 fps (-17.5%)", "Draw thread earns its keep", "info", CROSS),
    (S_P2, "V1turbo", "Preset Turbo", "P2-BASE-defaults-8.png", "V1-turbo-boot1.png", "defaults", "Turbo",
     "boots 229.8 / 227.6 fps (+3.7%)", "Gain, picture intact", "good", CROSS),
    (S_P2, "V1mild", "Preset Mild (before the fix)", "P2-BASE-defaults-10.png", "V1-mild-boot2.png", "defaults", "Mild",
     "boots 228.2 / 229.7 fps; 41% of pixels changed", "BROKEN", "bad", "White rectangles: the helper-pass pieces (fixed, see phases 6-7)."),
    (S_P2, "V1potato", "Preset Potato (before the fix)", "P2-BASE-defaults-10.png", "V1-potato-boot2.png", "defaults", "Potato",
     "boots 239.8 / 240.1 fps; 70% changed", "BROKEN", "bad", "Washed out above the tile seam and a white rectangle."),
    (S_P2, "V1low", "Preset Low (before the fix)", "P2-BASE-defaults-12.png", "V1-low-boot2.png", "defaults", "Low",
     "boots 240.8 / 237.9 fps; 49% changed", "BROKEN", "bad", ""),
    (S_P2, "V1extreme", "Preset Extreme (before the fix)", "P2-BASE-defaults-12.png", "V1-extreme-boot2.png", "defaults", "Extreme",
     "boots 238.4 / 240.1 fps; 41% changed", "BROKEN", "bad", ""),

    (S_L11, "L11", "Ring wait: spin (old) vs sleep 100 us (new default)", "L11-mode0.png", "L11-mode2.png", "spin", "sleep",
     "220.5 vs 220.1 fps; game CPU 3.39 -> 2.73 cores (-19%); frame times the same (p99 5.36-5.50 vs 5.36-5.41 ms)", "Made default", "good", SAME),

    (S_P4, "P4AA", "Null A/B at 720p", "P4-AA-off.png", "P4-AA-on.png", "off", "on", "232.1 -> 231.8 fps (-0.1%)", "Noise floor", "info", SAME),
    (S_P4, "P4L1", "Vertex arena at 720p (on = arena OFF)", "P4-L1-off.png", "P4-L1-on.png", "arena on", "arena off",
     "232.5 -> 228.1 fps: arena worth +1.9%", "Keep", "good", SAME),
    (S_P4, "P4I4a", "Index hash once at 720p", "P4-I4a-off.png", "P4-I4a-on.png", "off", "on", "231.3 -> 231.8 fps (+0.2%)", "No effect", "same", SAME),
    (S_P4, "P4I4b", "10-bit copies at 720p", "P4-I4b-off.png", "P4-I4b-on.png", "off", "on", "230.8 -> 227.3 fps (-1.5%, noisy rounds)", "No gain here", "same", SAME),
    (S_P4, "P4I5", "Stable upload reuse at 720p", "P4-I5-off.png", "P4-I5-on.png", "off", "on", "230.8 -> 233.6 fps (+1.2%), 4/4 rounds", "Small gain when CPU-bound", "good", SAME),
    (S_P4, "P4I1", "Hidden-part cull at 720p", "P4-I1-off.png", "P4-I1-on.png", "off", "on", "232.0 -> 229.7 fps (-1.0%)", "Loss", "bad", SAME),
    (S_P4, "P4L9", "Submit every 512 draws at 720p", "P4-BASE2-boot1.png", "P4-L9-boot2.png", "defaults", "submit 512",
     "243.9 / 242.8 vs 231.2 / 232.7 fps (+5.3%)", "Gain when CPU-bound", "good", CROSS),
    (S_P4, "P4L13", "Draw-thread pieces at 720p", "P4-BASE2-boot1.png", "P4-L13-boot2.png", "defaults", "pieces",
     "216.9 / 215.8 fps (-6.5%)", "Loss", "bad", CROSS),
    (S_P4, "P4DToff", "Draw thread off at 720p", "P4-BASE2-boot1.png", "P4-DToff-boot2.png", "defaults", "draw thread off",
     "180.1 / 181.0 fps (-22%)", "Draw thread earns its keep", "info", CROSS),

    (S_P5, "P5lean1", "Lean fetch 1", "P5-defaults-r1.png", "P5-lean1-r1.png", "defaults", "lean 1",
     "227.1 / 226.5 / 217.4 vs 222.1 / 221.2 / 215.6 fps: +1.8%", "Gain", "good", CROSS),
    (S_P5, "P5lean2", "Lean fetch 2", "P5-defaults-r1.png", "P5-lean2-r1.png", "defaults", "lean 2", "228.3 / 229.5 / 224.6 fps: +3.6%", "Gain", "good", CROSS),
    (S_P5, "P5plain", "Plain multiply", "P5-defaults-r1.png", "P5-plainmul-r1.png", "defaults", "plain multiply", "227.0 / 221.9 / 221.6 fps: +1.8%", "Gain", "good", CROSS),
    (S_P5, "P5stack2", "Lean 2 + plain multiply + 10-bit copies", "P5-defaults-r1.png", "P5-lean2_plainmul_copies-r1.png", "defaults", "stack (lean 2)",
     "230.5 / 226.4 / 228.7 fps: +4.1%", "Gain", "good", CROSS),
    (S_P5, "P5stack1", "Lean 1 + plain multiply + 10-bit copies (best)", "P5-defaults-r1.png", "P5-lean1_plainmul_copies-r1.png", "defaults", "stack (lean 1)",
     "230.1 / 230.9 / 231.8 fps: +5.1%, the steadiest", "Best set (needs a drive)", "good", CROSS),

    (S_P6, "P6h050", "Helper passes at half scale alone", "P6-defaults.png", "P6-helper050.png", "defaults", "REGPU_HELPER_SCALE=0.5",
     "227.8 fps; 47% of pixels changed", "BROKEN: cause 1", "bad", "Washed-out rectangle over the upper picture."),
    (S_P6, "P6h025", "Helper passes at quarter scale alone", "P6-defaults.png", "P6-helper025.png", "defaults", "REGPU_HELPER_SCALE=0.25",
     "228.8 fps; 70% changed", "BROKEN: cause 1", "bad", ""),
    (S_P6, "P6frz", "Helper passes frozen every 3rd frame alone", "P6-defaults.png", "P6-freeze3.png", "defaults", "REGPU_FREEZE_HELPERS=3",
     "227.9 fps; 61% changed", "BROKEN: cause 2", "bad", "Everything above the EDRAM tile seam washed out."),
    (S_P6, "P6mild", "Mild without helper scaling", "P6-defaults.png", "P6-mild-nohelper.png", "defaults", "Mild, no helper scale",
     "230.0 fps; 24% changed (67% scale + FSR 1)", "Fine", "good", "Softer by design."),
    (S_P6, "P6pot1", "Potato without helper scaling (freeze still on)", "P6-defaults.png", "P6-potato-nohelper.png", "defaults", "Potato, freeze on",
     "235.6 fps; 63% changed", "BROKEN (the freeze)", "bad", ""),
    (S_P6, "P6pot2", "Potato without both pieces", "P6-defaults.png", "P6-potato-nohelper-nofreeze.png", "defaults", "Potato, both off",
     "229.2 fps; 5% changed (2x anisotropy)", "Fine", "good", ""),

    (S_P7, "P7turbo", "Turbo after the fix", "P7-defaults.png", "P7-turbo.png", "defaults", "Turbo", "230.5 vs 222.8 fps (+3.5%)", "Fine, gain", "good", CROSS),
    (S_P7, "P7mild", "Mild after the fix", "P7-defaults.png", "P7-mild.png", "defaults", "Mild", "216.5 fps (unsteady); 24% changed by design", "Fine", "good", "Softer: 67% scale + FSR 1."),
    (S_P7, "P7potato", "Potato after the fix", "P7-defaults.png", "P7-potato.png", "defaults", "Potato", "215.9 fps (unsteady); 5% changed", "Fine", "good", ""),
    (S_P7, "P7low", "Low after the fix", "P7-defaults.png", "P7-low.png", "defaults", "Low", "229.7 fps; 27% changed (59% scale)", "Fine", "good", ""),
    (S_P7, "P7extreme", "Extreme after the fix", "P7-defaults.png", "P7-extreme.png", "defaults", "Extreme", "233.6 fps; 27% changed (50% scale)", "Fine", "good", ""),

    (S_FIN, "final", "Final build (ring wait default, presets fixed) vs phase 7 defaults", "P7-defaults.png", "final-defaults.png", "phase 7 defaults", "final build",
     "222.8 vs 221.3 fps", "Same picture", "good", CROSS),
]


def write_pairs(d):
    with open(os.path.join(d, "pairs.txt"), "w", encoding="utf-8") as f:
        for it in ITEMS:
            f.write("%s|%s|%s\n" % (it[1], it[3], it[4]))
    print("%d pairs" % len(ITEMS))


CSS = """
:root{--bg:#f6f7f9;--panel:#ffffff;--ink:#16181d;--muted:#5d6470;--line:#e2e5ea;--accent:#d8411f;
--good:#1f7a4d;--good-bg:#e5f4ec;--bad:#b42318;--bad-bg:#fdecea;--same:#5d6470;--same-bg:#eef0f3;--info:#1d5fa8;--info-bg:#e7f0fb}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#111317;--panel:#1a1d23;--ink:#e9ebef;--muted:#9aa2ad;--line:#2b3038;
--accent:#ff6a45;--good:#5cc98d;--good-bg:#173226;--bad:#ff7b6e;--bad-bg:#3a1b19;--same:#a3abb6;--same-bg:#262a31;--info:#7fb2ee;--info-bg:#18273a}}
:root[data-theme="dark"]{--bg:#111317;--panel:#1a1d23;--ink:#e9ebef;--muted:#9aa2ad;--line:#2b3038;
--accent:#ff6a45;--good:#5cc98d;--good-bg:#173226;--bad:#ff7b6e;--bad-bg:#3a1b19;--same:#a3abb6;--same-bg:#262a31;--info:#7fb2ee;--info-bg:#18273a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
header{padding:28px 16px 8px;max-width:1240px;margin:0 auto}h1{margin:0 0 6px;font-size:26px}
.lede{color:var(--muted);max-width:820px;margin:0}
nav{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--line)}
nav .in{max-width:1240px;margin:0 auto;padding:8px 16px;display:flex;gap:6px;overflow-x:auto}
nav a{white-space:nowrap;text-decoration:none;color:var(--muted);font-size:13px;padding:4px 10px;border-radius:999px;border:1px solid var(--line)}
nav a:hover{color:var(--ink);border-color:var(--muted)}
main{max-width:1240px;margin:0 auto;padding:8px 16px 60px}
h2{margin:36px 0 4px;font-size:19px}.sec-note{color:var(--muted);margin:0 0 12px;font-size:14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:10px;margin:14px 0;overflow:hidden}
.head{display:flex;flex-wrap:wrap;gap:6px 12px;align-items:baseline;padding:12px 14px 8px}
.head h3{margin:0;font-size:16px;flex:1 1 360px}.id{font:12px ui-monospace,Consolas,monospace;color:var(--muted)}
.badge{font-size:12px;font-weight:600;padding:2px 9px;border-radius:999px}
.good{color:var(--good);background:var(--good-bg)}.bad{color:var(--bad);background:var(--bad-bg)}
.same{color:var(--same);background:var(--same-bg)}.info{color:var(--info);background:var(--info-bg)}
.facts{padding:0 14px 8px;color:var(--muted);font-size:14px}.facts b{color:var(--ink);font-weight:600}
.tabs{display:flex;gap:4px;padding:0 14px 10px;flex-wrap:wrap}
.tabs button{font:inherit;font-size:13px;padding:4px 11px;border-radius:6px;border:1px solid var(--line);background:transparent;color:var(--muted);cursor:pointer}
.tabs button[aria-pressed="true"]{background:var(--ink);color:var(--panel);border-color:var(--ink)}
.view{position:relative;background:#000;line-height:0}
.view img{width:100%;height:auto;display:block}
.swipe{position:relative;overflow:hidden}.swipe .top{position:absolute;inset:0;overflow:hidden;width:50%}
.swipe .top img{width:auto;height:100%;max-width:none}
.swipe .bar{position:absolute;top:0;bottom:0;width:2px;background:var(--accent);left:50%;pointer-events:none}
.swipe .lab{position:absolute;top:8px;font:12px/1.4 system-ui;color:#fff;background:rgba(0,0,0,.6);padding:2px 7px;border-radius:4px}
.swipe .la{left:8px}.swipe .lb{right:8px}
.swipe input{position:absolute;inset:0;width:100%;height:100%;opacity:0;cursor:ew-resize;margin:0}
.side{display:grid;grid-template-columns:1fr 1fr;gap:2px}.side figure{margin:0;position:relative}
.side figcaption{position:absolute;top:8px;left:8px;font:12px/1.4 system-ui;color:#fff;background:rgba(0,0,0,.6);padding:2px 7px;border-radius:4px}
.links{padding:8px 14px 12px;font-size:13px;color:var(--muted);display:flex;gap:14px;flex-wrap:wrap}
.links a{color:var(--muted)}
.theme{position:absolute;top:18px;right:16px;font:inherit;font-size:13px;padding:4px 10px;border-radius:6px;border:1px solid var(--line);background:var(--panel);color:var(--ink);cursor:pointer}
@media (max-width:700px){.side{grid-template-columns:1fr}h1{font-size:22px}}
"""

JS = """
document.querySelectorAll('.card').forEach(card=>{
  const views=card.querySelectorAll('[data-view]');const btns=card.querySelectorAll('.tabs button');
  btns.forEach(b=>b.addEventListener('click',()=>{btns.forEach(x=>x.setAttribute('aria-pressed',x===b));
    views.forEach(v=>v.hidden=v.dataset.view!==b.dataset.view);}));
  const sw=card.querySelector('.swipe');if(sw){const r=sw.querySelector('input'),t=sw.querySelector('.top'),bar=sw.querySelector('.bar'),
    ti=t.querySelector('img');const fit=()=>{ti.style.width=sw.clientWidth+'px';};
    r.addEventListener('input',()=>{t.style.width=r.value+'%';bar.style.left=r.value+'%';});
    new ResizeObserver(fit).observe(sw);fit();}
});
const tb=document.querySelector('.theme');tb.addEventListener('click',()=>{const r=document.documentElement;
  const dark=r.dataset.theme?r.dataset.theme==='dark':matchMedia('(prefers-color-scheme: dark)').matches;r.dataset.theme=dark?'light':'dark';});
"""


def write_html(d):
    stats = {}
    sp = os.path.join(d, "report", "pairs-stats.txt")
    if os.path.exists(sp):
        for line in open(sp, encoding="utf-8-sig"):
            p = line.strip().split("|")
            if len(p) == 4:
                stats[p[0]] = (int(p[1]), int(p[2]), int(p[3]))
    e = html.escape
    sections = []
    for it in ITEMS:
        if it[0] not in sections:
            sections.append(it[0])
    out = ["<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>",
           "<title>FM3 Perf Pictures</title><style>%s</style></head><body>" % CSS,
           "<header><button class='theme' type='button'>Light / dark</button><h1>FM3 performance tests: picture comparisons</h1>",
           "<p class='lede'>Session 23, 2026-11-11. Every comparison from the hands-off tests on the frozen CrownJewel grid "
           "(8th, Scion xD; RX 7900 XTX). A is the reference (switch off or defaults), B the test. <b>Swipe</b> drags between them; "
           "<b>Difference</b> shows B in grey with every pixel that differs by more than 8 (of 255) in red, computed from the "
           "original PNGs. Numbers and verdicts: PERF-RESULTS.md. Click a picture's link for the full-size original.</p></header>",
           "<nav><div class='in'>%s</div></nav><main>" % "".join("<a href='#s%d'>%s</a>" % (i, e(s.split(':')[0])) for i, s in enumerate(sections))]
    for si, sec in enumerate(sections):
        out.append("<h2 id='s%d'>%s</h2>" % (si, e(sec)))
        for it in [x for x in ITEMS if x[0] == sec]:
            _, key, title, a, b, la, lb, nums, verdict, kind, note = it
            st = stats.get(key)
            px = ""
            if st and st[0] >= 0:
                px = "<b>%s</b> of %s pixels differ (%.3f%%), worst %d" % (format(st[0], ","), format(st[1], ","), 100.0 * st[0] / st[1], st[2])
            ra, rb, rd = "report/%s-a.jpg" % key, "report/%s-b.jpg" % key, "report/%s-d.png" % key
            out.append("<section class='card'><div class='head'><h3>%s</h3><span class='badge %s'>%s</span><span class='id'>%s</span></div>"
                       % (e(title), kind, e(verdict), e(key)))
            out.append("<div class='facts'>%s%s%s</div>" % (e(nums), ("<br>" + px) if px else "", ("<br>" + e(note)) if note else ""))
            out.append("<div class='tabs'><button type='button' data-view='swipe' aria-pressed='true'>Swipe</button>"
                       "<button type='button' data-view='side' aria-pressed='false'>Side by side</button>"
                       "<button type='button' data-view='diff' aria-pressed='false'>Difference</button></div>")
            out.append("<div class='view swipe' data-view='swipe'><img loading='lazy' src='%s' alt='%s'>"
                       "<div class='top'><img loading='lazy' src='%s' alt='%s'></div><div class='bar'></div>"
                       "<span class='lab la'>A: %s</span><span class='lab lb'>B: %s</span>"
                       "<input type='range' min='0' max='100' value='50' aria-label='Swipe between A and B'></div>"
                       % (rb, e(lb), ra, e(la), e(la), e(lb)))
            out.append("<div class='view side' data-view='side' hidden><figure><img loading='lazy' src='%s' alt='%s'><figcaption>A: %s</figcaption></figure>"
                       "<figure><img loading='lazy' src='%s' alt='%s'><figcaption>B: %s</figcaption></figure></div>" % (ra, e(la), e(la), rb, e(lb), e(lb)))
            out.append("<div class='view' data-view='diff' hidden><img loading='lazy' src='%s' alt='difference'></div>" % rd)
            out.append("<div class='links'><a href='%s'>A original: %s</a><a href='%s'>B original: %s</a><a href='%s'>difference (PNG)</a></div></section>"
                       % (e(a), e(a), e(b), e(b), rd))
    out.append("</main><script>%s</script></body></html>" % JS)
    with open(os.path.join(d, "report.html"), "w", encoding="utf-8") as f:
        f.write("".join(out))
    print("report.html: %d comparisons, %d with pixel counts" % (len(ITEMS), len(stats)))


if __name__ == "__main__":
    {"pairs": write_pairs, "html": write_html}[sys.argv[1]](sys.argv[2])
