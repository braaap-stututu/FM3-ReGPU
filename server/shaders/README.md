# FM3 shaders server

Collects players' `pipelines.manifest` files so each release can build their pipelines on the first boot
(`src/pipeline_seed.h`). FM2:ReGPU does the same thing. A manifest holds only shader hashes and render state: no game data,
nothing about the player. The launcher sends it only when the player has said yes (asked once, then a switch under
Settings › Shader cache).

```
launcher (Sharing.cs) ──POST /v1/pipelines──▶ this Worker ──GitHub API──▶ private repo: manifests/<installId>.manifest
                                                                            │
           tools/pipeline_seed.py <clone>/manifests ◀── git pull (GitHub Desktop) ┘
```

## Set up (once)

1. Create a **private** GitHub repo for the uploads, e.g. `fm3-shaders`, with a README so it has a `main` branch.
2. Create a fine-grained token: that repo only, **Contents: read and write**.
3. Here:
   ```
   npm i -D wrangler
   npx wrangler secret put GITHUB_TOKEN
   ```
   Set `SHADERS_REPO` in `wrangler.toml`, then `npx wrangler deploy`.
4. Put the Worker's URL + `/v1/pipelines` in `DefaultEndpoint` in `launcher/FM3Launcher/Sharing.cs`. Until then the
   launcher never asks and never sends.

Test locally: `npx wrangler dev`, then start the launcher with `FM3_SHADERS_URL=http://127.0.0.1:8787/v1/pipelines`.

## Each release

Clone the uploads repo with GitHub Desktop (or `git clone`) and press **Fetch origin / Pull** to get the new
manifests, then:

```
python -I tools/pipeline_seed.py <path to the clone>/manifests
```

This merges them into `src/pipeline_seed.h`, keeping its existing lines. Rebuild and ship.
