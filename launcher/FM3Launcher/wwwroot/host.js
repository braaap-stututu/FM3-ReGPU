"use strict"
// The message channel to the launcher (Bridge.cs): Photino's window.external, on WebView2 (Windows) and WebKitGTK
// (Linux) alike. A page in a frame of the main window (the Controls tab) goes through its parent, which tags the
// messages with the frame's name both ways. Same shape as WebView2's channel: postMessage(object), "message"
// listeners getting { data }.

const host = (() => {
  const listeners = []
  const deliver = (data) => { for (const f of listeners) f({ data }) }
  const api = (send) => ({ postMessage: send, addEventListener: (_, f) => listeners.push(f) })

  if (window.parent !== window) {
    addEventListener("message", (e) => { if (e.source === window.parent && e.data && e.data.fm2) deliver(e.data.msg) })
    return api((msg) => window.parent.postMessage({ fm2: true, frame: window.name, msg }, "*"))
  }
  const ext = window.external
  if (!ext || typeof ext.sendMessage !== "function") {
    // In the system's browser (Shell.cs BrowserShell: Proton/Wine, --browser): the page's messages as POST msg, the
    // launcher's as the server-sent events of events, both under the page's own (token) path.
    if (location.protocol !== "http:" || !/^\/[0-9a-f]{32}\//.test(location.pathname)) return null
    const es = new EventSource("events")
    es.onmessage = (e) => { try { deliver(JSON.parse(e.data)) } catch (_) {} }
    return api((msg) => fetch("msg", { method: "POST", body: JSON.stringify(msg), headers: { "Content-Type": "application/json" } }).catch(() => {}))
  }
  ext.receiveMessage((text) => {
    const m = JSON.parse(text)
    if (m.frame === undefined) return deliver(m)
    const frame = window.frames[m.frame]
    if (frame) frame.postMessage({ fm2: true, msg: m.msg }, "*")
  })
  addEventListener("message", (e) => {
    const d = e.data
    if (d && d.fm2 && typeof d.frame === "string") ext.sendMessage(JSON.stringify({ frame: d.frame, msg: d.msg }))
  })
  return api((msg) => ext.sendMessage(JSON.stringify(msg)))
})()
