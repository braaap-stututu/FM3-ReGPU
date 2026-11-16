"use strict"
// Controller navigation (session 39): the launcher with a gamepad, in its own window (WebView2, WebKitGTK) and in a
// browser (Proton/Wine browser mode), through the browser's Gamepad API. Only controllers with the standard layout
// (Xbox, PlayStation, Steam Deck...) count: a racing wheel's buttons don't drive the menus while it is being bound.
//   LB / RB          previous / next tab
//   D-pad, left stick move to the nearest option that way (held: repeats)
//   A                press / switch on; a list opens as a controller menu; a slider or number box: adjust with left /
//                    right (A or B when done)
//   B                close the list, the adjusting, a dialog; else back to the Play tab
//   Start            this tab's Play button
// The focused option shows a ring, and its explanation (the mouse tooltip) shows in a bar at the bottom.

const gp = {
  prev: {},        // button -> pressed in the last poll
  next: {},        // direction / button -> time of its next repeat
  focus: null,     // the focused element
  sub: null,       // { kind: "list" | "adjust", el, menu, index }
  active: false,   // a controller has been used since the mouse last moved
}
const GP_A = 0, GP_B = 1, GP_LB = 4, GP_RB = 5, GP_START = 9, GP_UP = 12, GP_DOWN = 13, GP_LEFT = 14, GP_RIGHT = 15

function gpVisible(e) {
  if (!e || e.disabled || e.closest("[hidden]")) return false
  const r = e.getBoundingClientRect()
  if (r.width === 0 || r.height === 0) return false
  const s = getComputedStyle(e)
  return s.visibility !== "hidden" && s.display !== "none"
}
// Where focus moves: the open dialog, else the page on show (the header's tabs go by LB / RB).
function gpRoot() {
  const modals = [...document.querySelectorAll(".modal")].filter((m) => !m.hidden)
  if (modals.length) return modals[modals.length - 1]
  return [...document.querySelectorAll("main.page")].find((p) => !p.hidden) || document.body
}
function gpCandidates() {
  const root = gpRoot()
  return [...root.querySelectorAll("button, select, input:not([type=hidden]), textarea, .tile[tabindex], [data-gp]")].filter(gpVisible)
}
function gpSetFocus(e) {
  if (gp.focus) gp.focus.classList.remove("gp-focus")
  gp.focus = e
  if (!e) return gpHint()
  e.classList.add("gp-focus")
  e.scrollIntoView({ block: "nearest", inline: "nearest" })
  gpHint()
}
// The first option in reading order (the page's first panel first).
function gpFirst() {
  gpSetFocus(gpCandidates()[0] || null)
}
// The nearest candidate in a direction from the focused one: along the direction first, sideways counting double.
function gpMove(dx, dy) {
  const c = gpCandidates()
  if (!gp.focus || !c.includes(gp.focus)) return gpFirst()
  const r0 = gp.focus.getBoundingClientRect(), x0 = r0.left + r0.width / 2, y0 = r0.top + r0.height / 2
  let best = null, bestScore = Infinity
  for (const e of c) {
    if (e === gp.focus) continue
    const r = e.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2
    const along = dx ? (x - x0) * dx : (y - y0) * dy
    const side = dx ? Math.abs(y - y0) : Math.abs(x - x0)
    // (In the direction, past the focused one's edge: not just beside it.)
    const past = dx ? (dx > 0 ? r.left >= r0.right - 4 : r.right <= r0.left + 4) : (dy > 0 ? r.top >= r0.bottom - 4 : r.bottom <= r0.top + 4)
    if (along <= 0 || !past) continue
    const score = along + side * 2
    if (score < bestScore) { bestScore = score; best = e }
  }
  if (best) gpSetFocus(best)
}

// ---- A
function gpPress() {
  const e = gp.focus
  if (!e || !gpVisible(e)) return gpFirst()
  if (e.tagName === "SELECT") return gpOpenList(e)
  if (e.tagName === "INPUT" && (e.type === "range" || e.type === "number")) return gpOpenAdjust(e)
  if (e.tagName === "INPUT" && e.type === "checkbox") return e.click()
  if (e.tagName === "INPUT" || e.tagName === "TEXTAREA") { e.focus(); return }
  e.click()
  // (A press may have changed the page: keep a focus that is still there.)
  setTimeout(() => { if (!gpVisible(gp.focus)) gpFirst() }, 60)
}

// A list as a controller menu: up / down, A picks, B closes.
function gpOpenList(sel) {
  const menu = document.createElement("div")
  menu.className = "gp-menu"
  const title = sel.closest(".field") && sel.closest(".field").querySelector("span")
  const head = document.createElement("div")
  head.className = "gp-menu-title"
  head.textContent = title ? title.textContent : "Choose"
  menu.append(head)
  const items = [...sel.options].map((o, i) => {
    const d = document.createElement("div")
    d.className = "gp-item"
    d.textContent = o.text
    d.onclick = () => { gp.sub.index = i; gpPickList() }
    menu.append(d)
    return d
  })
  document.body.append(menu)
  gp.sub = { kind: "list", el: sel, menu, items, index: Math.max(0, sel.selectedIndex) }
  gpListShow()
}
function gpListShow() {
  const s = gp.sub
  s.items.forEach((d, i) => d.classList.toggle("on", i === s.index))
  s.items[s.index] && s.items[s.index].scrollIntoView({ block: "nearest" })
}
function gpPickList() {
  const s = gp.sub
  if (s.el.selectedIndex !== s.index) {
    s.el.selectedIndex = s.index
    s.el.dispatchEvent(new Event("change", { bubbles: true }))
  }
  gpCloseSub()
}

// A slider or number box: left / right (and up / down) step it, A or B when done.
function gpOpenAdjust(input) {
  gp.sub = { kind: "adjust", el: input }
  input.classList.add("gp-adjust")
  gpHint()
}
function gpAdjust(dir) {
  const e = gp.sub.el
  const step = parseFloat(e.step) || 1, min = e.min === "" ? -Infinity : parseFloat(e.min), max = e.max === "" ? Infinity : parseFloat(e.max)
  const v = Math.min(max, Math.max(min, (parseFloat(e.value) || 0) + dir * step))
  e.value = String(Math.round(v / step) * step)
  e.dispatchEvent(new Event("input", { bubbles: true }))
  e.dispatchEvent(new Event("change", { bubbles: true }))
}

function gpCloseSub() {
  const s = gp.sub
  if (!s) return
  if (s.menu) s.menu.remove()
  if (s.kind === "adjust") s.el.classList.remove("gp-adjust")
  gp.sub = null
  gpHint()
}

// ---- B
function gpBack() {
  if (gp.sub) return gpCloseSub()
  const root = gpRoot()
  if (root.classList.contains("modal")) {
    const close = root.querySelector('[data-choice="no"], button.close')
    if (close) close.click()
    else root.hidden = true
    return setTimeout(gpFirst, 60)
  }
  if (typeof wh !== "undefined" && wh.bind) return whCancelBind()
  if (typeof wh !== "undefined" && wh.cal) { wh.cal = null; whRenderBindings(true); return }
  if (document.activeElement && ["INPUT", "TEXTAREA"].includes(document.activeElement.tagName)) return document.activeElement.blur()
  const play = document.querySelector('#tabs button[data-tab="play"]')
  if (play && !play.classList.contains("on")) { play.click(); setTimeout(gpFirst, 60) }
}

// ---- LB / RB, Start
function gpTab(dir) {
  gpCloseSub()
  const tabs = [...document.querySelectorAll("#tabs button")].filter(gpVisible)
  const now = tabs.findIndex((t) => t.classList.contains("on"))
  const next = tabs[(now + dir + tabs.length) % tabs.length]
  if (next) { next.click(); setTimeout(gpFirst, 60) }
}
function gpStart() {
  const page = gpRoot()
  const b = page.querySelector("button.launch") || document.getElementById("launch")
  if (b && !b.disabled) b.click()
}

// ---- the hint bar: what A / B do now, and the focused option's explanation
function gpHint() {
  let bar = document.getElementById("gp-hint")
  if (!bar) {
    bar = document.createElement("div")
    bar.id = "gp-hint"
    document.body.append(bar)
  }
  bar.hidden = !gp.active
  if (!gp.active) return
  const s = gp.sub
  const keys = s && s.kind === "list" ? "↑↓ choose · A pick · B close"
    : s && s.kind === "adjust" ? "← → change · A / B done"
    : "LB RB tabs · ✚ / stick move · A select · B back · Start play"
  const f = gp.focus
  const tip = f && !s ? (f.dataset.tip || (f.closest("[data-tip]") && f.closest("[data-tip]").dataset.tip) || "") : ""
  bar.innerHTML = ""
  const k = document.createElement("b")
  k.textContent = keys
  bar.append(k)
  if (tip) { const t = document.createElement("span"); t.textContent = tip; bar.append(t) }
}

// ---- polling
function gpPoll() {
  requestAnimationFrame(gpPoll)
  const pads = (navigator.getGamepads ? [...navigator.getGamepads()] : []).filter((p) => p && p.connected && p.mapping === "standard")
  if (!pads.length) return
  const now = performance.now()
  const down = {}
  for (const p of pads) {
    p.buttons.forEach((b, i) => { if (b.pressed) down[i] = true })
    const x = p.axes[0] || 0, y = p.axes[1] || 0
    if (x < -0.5) down[GP_LEFT] = true
    if (x > 0.5) down[GP_RIGHT] = true
    if (y < -0.5) down[GP_UP] = true
    if (y > 0.5) down[GP_DOWN] = true
  }
  // A press now, then every 110 ms after 350 ms held (directions); other buttons once a press.
  const fire = (i, repeat) => {
    if (!down[i]) { delete gp.next[i]; return false }
    if (!gp.prev[i]) { gp.next[i] = now + 350; return true }
    if (repeat && now >= (gp.next[i] || Infinity)) { gp.next[i] = now + 110; return true }
    return false
  }
  const any = Object.keys(down).length > 0
  if (any && !gp.active) { gp.active = true; if (!gpVisible(gp.focus)) gpFirst(); else gpHint() }
  const s = gp.sub
  if (fire(GP_UP, true)) s ? (s.kind === "list" ? (s.index = Math.max(0, s.index - 1), gpListShow()) : gpAdjust(1)) : gpMove(0, -1)
  if (fire(GP_DOWN, true)) s ? (s.kind === "list" ? (s.index = Math.min(s.items.length - 1, s.index + 1), gpListShow()) : gpAdjust(-1)) : gpMove(0, 1)
  if (fire(GP_LEFT, true)) s ? (s.kind === "adjust" ? gpAdjust(-1) : null) : gpMove(-1, 0)
  if (fire(GP_RIGHT, true)) s ? (s.kind === "adjust" ? gpAdjust(1) : null) : gpMove(1, 0)
  if (fire(GP_A, false)) s ? (s.kind === "list" ? gpPickList() : gpCloseSub()) : gpPress()
  if (fire(GP_B, false)) gpBack()
  if (fire(GP_LB, false)) gpTab(-1)
  if (fire(GP_RB, false)) gpTab(1)
  if (fire(GP_START, false)) gpStart()
  gp.prev = down
}
// The mouse takes over again: no ring, no bar.
addEventListener("mousemove", () => {
  if (!gp.active) return
  gp.active = false
  gpCloseSub()
  gpSetFocus(null)
})
requestAnimationFrame(gpPoll)
