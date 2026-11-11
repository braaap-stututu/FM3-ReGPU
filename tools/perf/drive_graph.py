"""Frame-rate graph of driving sessions from fm3.log (session 23): fps per 60 swaps (the log's "gpu: swap #N" lines,
so it works without the perf log), one chart per segment on one y scale, events from the log marked
(shader compiles, pipeline-count changes, vertex arena resets). Writes a self-contained HTML page.

  python -I tools/perf/drive_graph.py LOG OUT.html "name:start:end" ["name:start:end" ...]   (log seconds)
"""
import html
import json
import re
import sys


def main():
    log, out, segs = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    swaps, events, pipes = [], [], []
    for l in lines:
        m = re.match(r"\[\s*([\d.]+)\] (.*)", l)
        if not m:
            continue
        t, rest = float(m.group(1)), m.group(2)
        s = re.match(r"gpu: swap #(\d+)", rest)
        if s:
            swaps.append((t, int(s.group(1))))
            continue
        c = re.match(r"renderer: ps ([0-9a-f]+) compiled in (\d+) ms", rest)
        if c:
            events.append((t, "shader compile %s ms" % c.group(2)))
        if rest.startswith("vertex arena: full"):
            events.append((t, "vertex arena full, rebuilt"))
        g = re.match(r"grid test: race start \S+ phase \d+ flags 0 1 1", rest)
        if g:
            events.append((t, "GO"))
        p = re.match(r"renderer: frame \d+: (\d+) draws, \d+ skipped, \d+ shaders, (\d+) pipelines", rest)
        if p:
            pipes.append((t, int(p.group(2)), int(p.group(1))))
    blocks = [((t0 + t1) / 2, (n1 - n0) / (t1 - t0), t1 - t0) for (t0, n0), (t1, n1) in zip(swaps, swaps[1:]) if t1 > t0]
    charts = []
    for sg in segs:
        name, a, b = sg.rsplit(":", 2)
        a, b = float(a), float(b)
        pts = [(round(x[0], 2), round(x[1], 1)) for x in blocks if a <= x[0] <= b]
        frames = 60 * len(pts)
        secs = sum(x[2] for x in blocks if a <= x[0] <= b)
        ev = [(round(t, 2), e) for t, e in events if a <= t <= b]
        # new pipelines between consecutive renderer lines inside the segment
        for (t0, p0, _), (t1, p1, _) in zip(pipes, pipes[1:]):
            if a <= t1 <= b and p1 > p0:
                ev.append((round(t1, 2), "+%d pipelines (built since %.1f s)" % (p1 - p0, t0)))
        fps = [p[1] for p in pts]
        srt = sorted(fps)
        p1 = srt[max(0, int(len(srt) * 0.01) - 1)] if srt else 0
        charts.append({"name": name, "start": a, "end": b, "pts": pts, "events": sorted(ev),
                       "avg": round(frames / secs, 1) if secs else 0, "min": min(fps) if fps else 0, "p1": p1,
                       "under180": sum(1 for f in fps if f < 180), "blocks": len(pts)})
    page = TEMPLATE.replace("__DATA__", json.dumps(charts))
    open(out, "w", encoding="utf-8").write(page)
    for c in charts:
        print("%s: avg %.1f fps, lowest block %.1f, 1%% low %.1f, %d of %d blocks under 180" % (c["name"], c["avg"], c["min"], c["p1"], c["under180"], c["blocks"]))


TEMPLATE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Drive Test Frame Rate</title>
<style>
:root{color-scheme:light;--bg:#f4f4f2;--surface-1:#fcfcfb;--text-primary:#0b0b0b;--text-secondary:#52514e;--text-muted:#77766f;
--grid:#e6e5e0;--axis:#c9c8c1;--series-1:#2a78d6;--event:#8a897f;--event-hot:#b3261e;--cap:#c9c8c1}
@media (prefers-color-scheme:dark){:root:where(:not([data-theme="light"])){color-scheme:dark;--bg:#121211;--surface-1:#1a1a19;
--text-primary:#f0efec;--text-secondary:#c3c2b7;--text-muted:#94938a;--grid:#2a2a28;--axis:#45443f;--series-1:#3987e5;--event:#8e8d84;--event-hot:#ff8a80;--cap:#45443f}}
:root[data-theme="dark"]{color-scheme:dark;--bg:#121211;--surface-1:#1a1a19;--text-primary:#f0efec;--text-secondary:#c3c2b7;
--text-muted:#94938a;--grid:#2a2a28;--axis:#45443f;--series-1:#3987e5;--event:#8e8d84;--event-hot:#ff8a80;--cap:#45443f}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text-primary);font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:1180px;margin:0 auto;padding:24px 16px 48px}h1{font-size:24px;margin:0 0 4px}
.lede{color:var(--text-secondary);margin:0 0 18px;max-width:820px}
.card{background:var(--surface-1);border:1px solid var(--grid);border-radius:10px;padding:16px;margin:16px 0}
.card h2{font-size:17px;margin:0 0 10px}
.tiles{display:flex;gap:24px;flex-wrap:wrap;margin-bottom:8px}
.tile .v{font-size:26px;font-weight:650;line-height:1.1}.tile .k{color:var(--text-secondary);font-size:13px}
.chart{position:relative}svg{display:block;width:100%;height:auto;overflow:visible}
.tip{position:absolute;pointer-events:none;background:var(--surface-1);border:1px solid var(--axis);border-radius:6px;padding:6px 9px;
font-size:13px;line-height:1.4;box-shadow:0 2px 8px rgba(0,0,0,.12);white-space:nowrap;display:none}
.tip b{font-size:15px}.muted{color:var(--text-secondary)}
details{margin-top:8px}summary{cursor:pointer;color:var(--text-secondary);font-size:14px}
table{border-collapse:collapse;font-size:13px;margin-top:6px}td,th{padding:3px 10px;border-bottom:1px solid var(--grid);text-align:left}
.theme{float:right;font:inherit;font-size:13px;padding:4px 10px;border-radius:6px;border:1px solid var(--axis);background:var(--surface-1);color:var(--text-primary);cursor:pointer}
</style></head><body><main>
<button class="theme" type="button">Light / dark</button>
<h1>Drive test: frame rate</h1>
<p class="lede">CrownJewel (Ladera Test Track), 2560x1440 4x MSAA, cap 360, with the +5.1% shader set (lean fetch 1, plain
multiply, 10-bit copies), perf log off. Frame rate per 60 frames, from the log's swap stamps; x = seconds after GO, the
tooltip also gives the log time (your overlay's timer). Grey lines: events from the log; red: shader compiles.</p>
<div id="charts"></div></main>
<script>
const DATA = __DATA__;
const W = 1100, H = 300, M = {l: 46, r: 14, t: 14, b: 30}, YMAX = 400;
const root = document.getElementById('charts');
DATA.forEach(c => {
  const go = (c.events.find(e => e[1] === 'GO') || [c.start])[0];
  const x0 = c.pts.length ? c.pts[0][0] - go : 0, x1 = c.pts.length ? c.pts[c.pts.length - 1][0] - go : 1;
  const sx = v => M.l + (v - x0) / (x1 - x0) * (W - M.l - M.r), sy = v => M.t + (1 - Math.min(v, YMAX) / YMAX) * (H - M.t - M.b);
  const card = document.createElement('section'); card.className = 'card';
  card.innerHTML = `<h2>${c.name}</h2><div class="tiles">
    <div class="tile"><div class="v">${c.avg.toFixed(1)}</div><div class="k">average fps</div></div>
    <div class="tile"><div class="v">${c.p1.toFixed(0)}</div><div class="k">1% low (per 60 frames)</div></div>
    <div class="tile"><div class="v">${c.min.toFixed(0)}</div><div class="k">lowest 60-frame stretch</div></div>
    <div class="tile"><div class="v">${c.under180}</div><div class="k">stretches under 180 fps</div></div></div>`;
  let svg = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="${c.name} frame rate over time">`;
  for (let v = 0; v <= YMAX; v += 100) svg += `<line x1="${M.l}" x2="${W - M.r}" y1="${sy(v)}" y2="${sy(v)}" stroke="var(--grid)"/><text x="${M.l - 8}" y="${sy(v) + 4}" text-anchor="end" font-size="12" fill="var(--text-muted)">${v}</text>`;
  svg += `<line x1="${M.l}" x2="${W - M.r}" y1="${sy(360)}" y2="${sy(360)}" stroke="var(--cap)" stroke-dasharray="4 4"/><text x="${W - M.r}" y="${sy(360) - 5}" text-anchor="end" font-size="12" fill="var(--text-muted)">cap 360</text>`;
  const step = (x1 - x0) > 120 ? 20 : 10;
  for (let v = Math.ceil(x0 / step) * step; v <= x1; v += step) svg += `<text x="${sx(v)}" y="${H - 8}" text-anchor="middle" font-size="12" fill="var(--text-muted)">${v} s</text>`;
  c.events.filter(e => e[1] !== 'GO').forEach(e => { const hot = e[1].startsWith('shader');
    svg += `<line x1="${sx(e[0] - go)}" x2="${sx(e[0] - go)}" y1="${M.t}" y2="${H - M.b}" stroke="${hot ? 'var(--event-hot)' : 'var(--event)'}" stroke-width="1" stroke-dasharray="${hot ? '' : '3 3'}" opacity="0.8"/>`; });
  const d = c.pts.map((p, i) => (i ? 'L' : 'M') + sx(p[0] - go).toFixed(1) + ' ' + sy(p[1]).toFixed(1)).join('');
  svg += `<path d="${d}" fill="none" stroke="var(--series-1)" stroke-width="2" stroke-linejoin="round"/>`;
  svg += `<line class="xh" y1="${M.t}" y2="${H - M.b}" stroke="var(--text-secondary)" stroke-width="1" visibility="hidden"/><circle class="dot" r="4.5" fill="var(--series-1)" stroke="var(--surface-1)" stroke-width="2" visibility="hidden"/>`;
  svg += `<rect x="${M.l}" y="${M.t}" width="${W - M.l - M.r}" height="${H - M.t - M.b}" fill="transparent"/></svg>`;
  const ch = document.createElement('div'); ch.className = 'chart'; ch.innerHTML = svg + '<div class="tip"></div>'; card.appendChild(ch);
  const rows = c.events.map(e => `<tr><td>${(e[0] - go).toFixed(1)} s</td><td>${e[0].toFixed(1)}</td><td>${e[1]}</td></tr>`).join('');
  const low = [...c.pts].sort((a, b) => a[1] - b[1]).slice(0, 8).sort((a, b) => a[0] - b[0]).map(p => `<tr><td>${(p[0] - go).toFixed(1)} s</td><td>${p[0].toFixed(1)}</td><td>${p[1].toFixed(0)} fps</td></tr>`).join('');
  card.insertAdjacentHTML('beforeend', `<details><summary>Events and the lowest stretches as tables</summary><table><tr><th>after GO</th><th>log time</th><th>event</th></tr>${rows}</table>
    <table><tr><th>after GO</th><th>log time</th><th>lowest 60-frame stretches</th></tr>${low}</table></details>`);
  root.appendChild(card);
  const s = ch.querySelector('svg'), tip = ch.querySelector('.tip'), xh = s.querySelector('.xh'), dot = s.querySelector('.dot');
  s.addEventListener('pointermove', ev => {
    const r = s.getBoundingClientRect(), vx = (ev.clientX - r.left) / r.width * W;
    const tx = x0 + (vx - M.l) / (W - M.l - M.r) * (x1 - x0);
    let best = c.pts[0]; for (const p of c.pts) if (Math.abs(p[0] - go - tx) < Math.abs(best[0] - go - tx)) best = p;
    const px = sx(best[0] - go), py = sy(best[1]);
    xh.setAttribute('x1', px); xh.setAttribute('x2', px); xh.setAttribute('visibility', 'visible');
    dot.setAttribute('cx', px); dot.setAttribute('cy', py); dot.setAttribute('visibility', 'visible');
    const near = c.events.filter(e => Math.abs(e[0] - best[0]) < 1.5).map(e => e[1]);
    tip.innerHTML = `<b>${best[1].toFixed(0)} fps</b><br><span class="muted">${(best[0] - go).toFixed(1)} s after GO · log ${best[0].toFixed(1)} s</span>` + (near.length ? '<br>' + near.join('<br>') : '');
    tip.style.display = 'block'; const left = px / W * r.width; tip.style.left = Math.min(left + 12, r.width - tip.offsetWidth) + 'px'; tip.style.top = (py / H * r.height - 10) + 'px';
  });
  s.addEventListener('pointerleave', () => { tip.style.display = 'none'; xh.setAttribute('visibility', 'hidden'); dot.setAttribute('visibility', 'hidden'); });
});
document.querySelector('.theme').addEventListener('click', () => { const r = document.documentElement;
  const dark = r.dataset.theme ? r.dataset.theme === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches; r.dataset.theme = dark ? 'light' : 'dark'; });
</script></body></html>"""

main()
