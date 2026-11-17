// FM3 shaders server: players' pipelines.manifest files (launcher/FM3Launcher/Sharing.cs), kept in a private GitHub
// repo as manifests/<installId>.manifest for tools/pipeline_seed.py. Same shape as FM2:ReGPU's reports server.
//   POST /v1/pipelines  { version, installId, pipelines }   pipelines: the manifest, gzip, base64
// Only well-formed manifest lines are stored (hashes and render state): anything else is sender text and dropped.
// Secrets: GITHUB_TOKEN (fine-grained: Contents read/write on SHADERS_REPO only).
// Vars: SHADERS_REPO ("owner/name"). Binding: LIMITER (rate limit per client address).

const MAX_BODY = 1 << 20; // compressed; a manifest is a few hundred KB of text
const MAX_TEXT = 4 << 20; // decompressed
const MAX_LINES = 50000;
const LINE = /^(P( [0-9a-f]{1,16}){6}( [0-9a-f:.]{1,40})*|V [0-9a-f]{1,16} [0-9a-f]{1,16}( [0-9a-f:.]{1,40})+)$/;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/") return json({ ok: true });
    if (request.method !== "POST" || url.pathname !== "/v1/pipelines") return json({ error: "Not found." }, 404);
    if (!(request.headers.get("user-agent") || "").startsWith("FM3Launcher/")) return json({ error: "Uploads come from the FM3 launcher." }, 403);

    if (env.LIMITER) {
      const { success } = await env.LIMITER.limit({ key: request.headers.get("cf-connecting-ip") || "local" });
      if (!success) return json({ error: "Too many uploads from here: try again later." }, 429);
    }
    if (Number(request.headers.get("content-length") || 0) > MAX_BODY) return json({ error: "Too big." }, 413);

    let r;
    try {
      const text = await request.text();
      if (text.length > MAX_BODY) return json({ error: "Too big." }, 413);
      r = JSON.parse(text);
    } catch {
      return json({ error: "That upload couldn't be read." }, 400);
    }
    const installId = String(r.installId || "").replace(/[^a-z0-9]/gi, "").slice(0, 16);
    const version = String(r.version || "").replace(/[^0-9a-z.\-]/gi, "").slice(0, 24) || "unknown";
    if (!installId || typeof r.pipelines !== "string") return json({ error: "Missing fields." }, 400);

    let lines;
    try {
      lines = [...new Set((await gunzip(r.pipelines)).split(/\r?\n/).map((l) => l.trim()).filter((l) => LINE.test(l)))];
    } catch (e) {
      return json({ error: e.message === "too big" ? "Too big." : "The manifest couldn't be read." }, 400);
    }
    if (!lines.length || lines.length > MAX_LINES) return json({ error: "No manifest lines." }, 400);
    lines.sort();

    // One file per install, replaced by each upload (the game's manifest only grows).
    const gh = github(env);
    const path = `/contents/manifests/${installId}.manifest`;
    try {
      let sha;
      try {
        sha = (await gh(path, "GET")).sha;
      } catch {}
      await gh(path, "PUT", {
        message: `Pipelines ${installId} (${lines.length}, v${version})`,
        content: base64(lines.join("\n") + "\n"),
        sha,
      });
      return json({ ok: true, lines: lines.length });
    } catch (e) {
      console.log("github", e.message);
      const status = /: (\d{3}) /.exec(e.message)?.[1] || "unreachable";
      return json({ error: `The shaders server couldn't save it (GitHub ${status}).` }, 502);
    }
  },
};

function json(o, status = 200) {
  return new Response(JSON.stringify(o), { status, headers: { "content-type": "application/json" } });
}

async function gunzip(b64) {
  const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
  const reader = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("gzip")).getReader();
  const parts = [];
  let total = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.length;
    if (total > MAX_TEXT) throw new Error("too big");
    parts.push(value);
  }
  return new TextDecoder().decode(await new Blob(parts).arrayBuffer());
}

function base64(text) {
  const bytes = new TextEncoder().encode(text);
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

function github(env) {
  return async (path, method, body) => {
    // GITHUB_API: a local stand-in for testing (.dev.vars)
    const res = await fetch(`${env.GITHUB_API || "https://api.github.com"}/repos/${env.SHADERS_REPO}${path}`, {
      method,
      headers: {
        authorization: `Bearer ${env.GITHUB_TOKEN}`,
        accept: "application/vnd.github+json",
        "x-github-api-version": "2022-11-28",
        "user-agent": "fm3-shaders",
        "content-type": "application/json",
      },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    if (!res.ok) throw new Error(`${method} ${path}: ${res.status} ${(await res.text()).slice(0, 300)}`);
    return res.json();
  };
}
