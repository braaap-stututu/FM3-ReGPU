// The Triple screens page (session 39): the monitors' layout (size, bezels, spacing, each side's angle, distance), the
// view it gives and a top view. The same geometry as the game's (src/hooks/triple_screen.cpp PhysicalScreens), so the
// figures here are what the game draws. Uses app.js's state, change() and $().

const TRI_D2R = Math.PI / 180
const TRI_MAX_HALF = 83 * TRI_D2R // FM3's frustum stays under 166 degrees across

// The three screens (metres, top view: x right, z forward from the eye): yaw, plane distance d, picture l..r in the
// plane, half height; plus the angle each side is at and the "facing the eye" angle.
function triLayout(diagIn, distCm, bezelMm, gapMm, leftDeg, rightDeg) {
  const diag = diagIn * 0.0254
  const w = diag * 16 / Math.hypot(16, 9), h = w * 9 / 16
  const dist = Math.max(0.2, distCm / 100), bezel = Math.max(0, bezelMm / 1000), gap = Math.max(0, gapMm / 1000)
  const edge = bezel + gap / 2, hinge = w / 2 + edge
  const side = (phi) => {
    const l = hinge * Math.cos(phi) - dist * Math.sin(phi) + edge
    return { d: hinge * Math.sin(phi) + dist * Math.cos(phi), l, r: l + w, halfH: h / 2 }
  }
  let lo = 0, hi = 89 * TRI_D2R
  for (let i = 0; i < 40; i++) {
    const mid = (lo + hi) / 2, s = side(mid)
    if (s.l + s.r > 0) lo = mid
    else hi = mid
  }
  const facing = (lo + hi) / 2
  const turnL = leftDeg > 0 ? Math.min(leftDeg, 89) * TRI_D2R : facing
  const turnR = rightDeg > 0 ? Math.min(rightDeg, 89) * TRI_D2R : facing
  const right = { ...side(turnR), yaw: turnR }
  const l0 = side(turnL)
  const left = { yaw: -turnL, d: l0.d, l: -l0.r, r: -l0.l, halfH: l0.halfH }
  const centre = { yaw: 0, d: dist, l: -w / 2, r: w / 2, halfH: h / 2 }
  return { screens: [left, centre, right], turnL, turnR, facing, w, h, bezel, dist }
}

// A point of a screen's plane (u across it, at height 0) in eye space.
function triPoint(s, u) {
  return { x: u * Math.cos(s.yaw) + s.d * Math.sin(s.yaw), z: -u * Math.sin(s.yaw) + s.d * Math.cos(s.yaw) }
}

function triFigures(L) {
  const [left, centre, right] = L.screens
  const a = (s, u) => { const p = triPoint(s, u); return Math.atan2(p.x, p.z) }
  const across = (a(right, right.r) - a(left, left.l)) / TRI_D2R
  const vfov = 2 * Math.atan(centre.halfH / centre.d) / TRI_D2R
  const outerR = triPoint(right, right.r + L.bezel), outerL = triPoint(left, left.l - L.bezel)
  const span = (outerR.x - outerL.x) * 100
  // Does FM3's widest frustum reach every corner? (16:9: the wider of the across and up needs decides.)
  let tanX = 0, tanY = 0, fits = true
  for (const s of L.screens)
    for (const u of [s.l, s.r]) {
      const p = triPoint(s, u)
      if (p.z <= 1e-3) { fits = false; continue }
      tanX = Math.max(tanX, Math.abs(p.x) / p.z)
      tanY = Math.max(tanY, s.halfH / p.z)
    }
  fits = fits && Math.max(tanX, tanY * 16 / 9) * 1.01 <= Math.tan(TRI_MAX_HALF)
  const hfovCentre = 2 * Math.atan(centre.r / centre.d) / TRI_D2R
  return { across, vfov, span, fits, hfovCentre }
}

let triLinked = null // "Same angle both sides" (kept on the page only)

function triSave(patch) { change(patch) }

function tripleRender() {
  if (!state || !state.settings) return
  const set = state.settings
  const L = triLayout(set.tripleDiag, set.tripleDist, set.tripleBezelMm, set.tripleGapMm, set.tripleAngleLeft, set.tripleAngleRight)
  const F = triFigures(L)
  const manual = set.tripleAngleLeft > 0 || set.tripleAngleRight > 0
  if (triLinked === null) triLinked = set.tripleAngleLeft === set.tripleAngleRight
  // Inputs (not while the user is typing in one).
  const put = (id, v) => { if (document.activeElement !== $(id)) $(id).value = v }
  put("tri-diag", set.tripleDiag); put("tri-diag-r", set.tripleDiag)
  put("tri-bezel", set.tripleBezelMm); put("tri-bezel-r", set.tripleBezelMm)
  put("tri-gap", set.tripleGapMm); put("tri-gap-r", set.tripleGapMm)
  put("tri-dist", set.tripleDist); put("tri-dist-r", set.tripleDist)
  const shownL = (L.turnL / TRI_D2R).toFixed(1), shownR = (L.turnR / TRI_D2R).toFixed(1)
  put("tri-left", shownL); put("tri-left-r", shownL)
  put("tri-right", shownR); put("tri-right-r", shownR)
  $("tri-link").checked = triLinked
  for (const e of document.querySelectorAll(".tri-simple")) e.hidden = set.tripleMode === "physical"
  for (const b of $("tri-mode").children) b.classList.toggle("on", b.dataset.mode === (set.tripleMode === "physical" ? "physical" : "auto"))
  for (const b of $("tri-angle-mode").children) b.classList.toggle("on", (b.dataset.amode === "manual") === manual)
  for (const e of document.querySelectorAll(".tri-angle")) e.classList.toggle("off", !manual)
  for (const id of ["tri-left", "tri-left-r", "tri-right", "tri-right-r", "tri-link"]) $(id).disabled = !manual
  $("tri-rec").textContent = `facing you: ${(L.facing / TRI_D2R).toFixed(1)}°`
  // Per-screen resolution from the Graphics tab's (the three together).
  // (The Triple screens tab's own resolution; "same" = the Graphics tab's.)
  const triRes = set.tripleResolution && set.tripleResolution !== "same" ? set.tripleResolution : set.resolution
  const [rw, rh] = String(triRes || "").split("x").map(Number)
  const res = $("tri-res")
  res.replaceChildren()
  const kv = (k, v) => { res.append(el("span", "k", k), el("span", "v", v)) }
  if (rw && rh) {
    const each = Math.floor(rw / 3)
    kv("Resolution", `${rw}×${rh}${triRes === set.resolution && (!set.tripleResolution || set.tripleResolution === "same") ? " (Graphics)" : ""}: ${each}×${Math.min(rh, Math.round(each * 9 / 16))} per screen`)
    if (Math.abs(rw / rh - 48 / 9) > 0.05) kv("Note", "not 3 × 16:9: set the resolution to three screens side by side (e.g. 7680×1440)")
  }
  const ppd = rw ? (Math.floor(rw / 3) / F.hfovCentre).toFixed(0) : "–"
  // Figures.
  const figs = $("tri-figs")
  figs.replaceChildren()
  const fig = (v, k) => { const d = el("div", "", ""); d.append(el("b", "", v), el("span", "", k)); figs.append(d) }
  fig(`${F.across.toFixed(1)}°`, "H-FOV")
  fig(`${F.vfov.toFixed(1)}°`, "V-FOV")
  fig(`${shownL}°`, "Left angle")
  fig(`${shownR}°`, "Right angle")
  fig(`${F.span.toFixed(1)} cm`, "Screen span")
  $("tri-sub").innerHTML = `${set.tripleDiag}" 16:9 · eye ${set.tripleDist} cm · bezel ${set.tripleBezelMm} mm · spacing ${set.tripleGapMm} mm · <b>${ppd}</b> pixels per degree (centre)` +
    (set.tripleMode === "physical" ? "" : " · <b>Simple</b> field of view is on: the game uses the Graphics tab's setting, not this layout")
  const warn = $("tri-warn")
  warn.hidden = F.fits
  warn.textContent = "The outer edges are too far round for FM3's view (166° at most, more at the corners): they'll be cut off. Turn the side screens in less, or sit further back."
  triDraw(L)
}

// Top view: pictures white, bezels grey, the eye and the lines to each picture's edges.
function triDraw(L) {
  const svg = $("tri-svg")
  const pts = [{ x: 0, z: 0 }]
  for (const s of L.screens) for (const u of [s.l - L.bezel, s.r + L.bezel]) pts.push(triPoint(s, u))
  const minX = Math.min(...pts.map((p) => p.x)), maxX = Math.max(...pts.map((p) => p.x))
  const minZ = Math.min(...pts.map((p) => p.z)), maxZ = Math.max(...pts.map((p) => p.z))
  const scale = Math.min(900 / Math.max(maxX - minX, 0.01), 440 / Math.max(maxZ - minZ, 0.01))
  const cx = 500 - ((minX + maxX) / 2) * scale, by = 480 + minZ * scale
  const X = (p) => (cx + p.x * scale).toFixed(1), Y = (p) => (by - p.z * scale).toFixed(1)
  let out = ""
  for (const s of L.screens) {
    const a = triPoint(s, s.l), b = triPoint(s, s.r), a0 = triPoint(s, s.l - L.bezel), b0 = triPoint(s, s.r + L.bezel)
    out += `<line x1="${X(a)}" y1="${Y(a)}" x2="${X({ x: 0, z: 0 })}" y2="${Y({ x: 0, z: 0 })}" stroke="#8a90a0" stroke-dasharray="4 5" stroke-width="1.2"/>`
    out += `<line x1="${X(b)}" y1="${Y(b)}" x2="${X({ x: 0, z: 0 })}" y2="${Y({ x: 0, z: 0 })}" stroke="#8a90a0" stroke-dasharray="4 5" stroke-width="1.2"/>`
    out += `<line x1="${X(a0)}" y1="${Y(a0)}" x2="${X(b0)}" y2="${Y(b0)}" stroke="#4a505c" stroke-width="7" stroke-linecap="round"/>`
    out += `<line x1="${X(a)}" y1="${Y(a)}" x2="${X(b)}" y2="${Y(b)}" stroke="#f2f3f5" stroke-width="5"/>`
  }
  const eye = { x: 0, z: 0 }
  out += `<circle cx="${X(eye)}" cy="${Y(eye)}" r="20" fill="none" stroke="#8a90a0" stroke-width="3"/>`
  out += `<circle cx="${X(eye)}" cy="${Y(eye)}" r="4" fill="#e3262f"/>`
  svg.innerHTML = out
}

// ---- inputs: a slider and its number box set the same setting
function triBind(id, key, min, max) {
  const apply = (v) => {
    v = Math.min(max, Math.max(min, Number(v)))
    if (Number.isFinite(v)) triSave({ [key]: v })
  }
  $(id + "-r").oninput = (e) => { $(id).value = e.target.value; apply(e.target.value) }
  $(id).onchange = (e) => apply(e.target.value)
}
triBind("tri-diag", "tripleDiag", 10, 100)
triBind("tri-bezel", "tripleBezelMm", 0, 100)
triBind("tri-gap", "tripleGapMm", 0, 300)
triBind("tri-dist", "tripleDist", 20, 300)
function triAngle(side, v) {
  v = Math.min(89, Math.max(0.5, Number(v)))
  if (!Number.isFinite(v)) return
  if (triLinked) triSave({ tripleAngleLeft: v, tripleAngleRight: v })
  else triSave(side === "left" ? { tripleAngleLeft: v } : { tripleAngleRight: v })
}
for (const side of ["left", "right"]) {
  $(`tri-${side}-r`).oninput = (e) => triAngle(side, e.target.value)
  $(`tri-${side}`).onchange = (e) => triAngle(side, e.target.value)
}
$("tri-link").onchange = (e) => {
  triLinked = e.target.checked
  if (triLinked) triSave({ tripleAngleRight: state.settings.tripleAngleLeft })
}
$("tri-mode").onclick = (e) => { const b = e.target.closest("button"); if (b) triSave({ tripleMode: b.dataset.mode }) }
$("tri-angle-mode").onclick = (e) => {
  const b = e.target.closest("button")
  if (!b) return
  if (b.dataset.amode === "auto") triSave({ tripleAngleLeft: 0, tripleAngleRight: 0 })
  else {
    // Manual starts from the "facing you" angle.
    const s = state.settings
    const f = Math.round(triLayout(s.tripleDiag, s.tripleDist, s.tripleBezelMm, s.tripleGapMm, 0, 0).facing / TRI_D2R * 2) / 2
    triSave({ tripleAngleLeft: f, tripleAngleRight: f })
  }
}
