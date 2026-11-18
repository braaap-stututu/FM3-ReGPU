// The GPU is the ReGPU library (thirdparty/ReGPU, include/regpu/regpu.h), linked the way the FM4
// port links it. This file hands it the display settings and the runtime pieces it needs: guest
// memory, guest threads, guest calls and the interrupt lock. ReGPU provides the Vd* imports itself.
//
// Phase 1 skeleton: the runtime it calls into (memory.h, kernel/runtime.h, kernel/thread.h,
// settings.h, log.h) is ported from the FM2/FM4 templates in Phase 3; nothing compiles this file
// until then (CMakeLists.txt builds the game only once out/ppc exists).
//
// FM3-specific defaults set here (see FM3-QUIRKS.md):
//   Q24  FM3's main loop runs once per guest vblank (game time follows the clock, src/hooks/
//        frame_time.cpp). So the vblank runs at the frame cap (max_fps, or the display's refresh
//        if lower and vsync is on; 60 by default), not at ReGPU's 2x the cap.
//        FM3_VBLANK_HZ=<n> (or REGPU_VBLANK_HZ) overrides it, to measure game speed.
// Deliberately NOT carried over from FM4: its road-strip resolve write-back rule and its car-shadow
// cascade switches (FM4 addresses/formats). FM3's own versions (Q18, Q19) come in Phase 5, decided
// from hl360-vs-ReGPU frame-dump diffs.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <windows.h>

#include "regpu/driver.h"
#include "regpu/regpu.h"
#include "regpu/hyper.h"

#include "kernel/runtime.h"
#include "kernel/thread.h"
#include "live_switch.h"
#include "log.h"
#include "memory.h"
#include "settings.h"
#include "hooks/frame_time.h"
#include "pipeline_seed.h"
#include "static_shaders.h"
#include "boot_screen.h"

std::string CurrentHostStack(int maxFrames); // crash.cpp
void LogAllHostThreadStacks(const char* why); // hang.cpp
void StartGpuMemoryLog(); // gpu_memory_log.cpp

namespace
{
void ApplySettings(regpu::Config& c)
{
    const Settings& s = GetSettings();
    c.width = s.width;
    c.height = s.height;
    c.aspectW = s.aspectW;
    c.aspectH = s.aspectH;
    c.fullscreen = s.fullscreen;
    c.vsync = s.vsync;
    c.maxFps = s.maxFps;
    c.msaa = s.msaa;
    // FSR 4 (session 36, ReGPU fsr3.cpp): the FidelityFX upscaler's 4.x version where the GPU offers it (RDNA 4, RX 9000
    // series), else FSR 3.1 with a log line; the same single-sample inputs (Settings forces MSAA 1x for it).
    if (s.upscaler == Settings::Upscaler::Bilinear)
    {
        // L63 (session 34): FSR 1's scene scaling with a plain bilinear upscale instead of EASU + RCAS (ReGPU patch 49,
        // REGPU_UPSCALE_BILINEAR, live): for small GPUs (2-CU iGPU, Low preset: frozen grid 77 -> 85 fps); softer.
        c.upscaler = regpu::Config::Upscaler::Fsr1;
        if (!getenv("REGPU_UPSCALE_BILINEAR"))
            _putenv_s("REGPU_UPSCALE_BILINEAR", "1");
    }
    else if (s.upscaler == Settings::Upscaler::Xess)
        c.upscaler = regpu::Config::Upscaler::Xess; // (session 43: Intel XeSS, libxess.dll beside fm3.exe; else FSR 1)
    else
        c.upscaler = regpu::Config::Upscaler(uint32_t(s.upscaler));
    c.quality = s.quality;
    c.sharpness = s.sharpness;
    // Anti-aliasing filters (session 36, ReGPU's postaa): settings.ini aa=, ssaa=, eqaa= (launcher Graphics).
    c.postAA = regpu::Config::PostAA(uint32_t(s.postAA));
    c.ssaa = s.ssaa;
    c.eqaa = s.eqaa;
    static const char* const kAaNames[] = { "off", "FXAA", "SMAA", "CMAA 2", "TAA" };
    Log("gpu: anti-aliasing filter %s, supersampling %.2fx, EQAA %s", kAaNames[std::min<uint32_t>(uint32_t(s.postAA), 4)], s.ssaa, s.eqaa ? "asked for" : "off");
}

// Test aid: FM3_WRITEBACK="WxH,WxH,..." writes colour resolves of those sizes back to memory at once, to find out
// which images the game reads on the CPU. Re-read whenever it changes, so it can be set live
// (Fm3 "env FM3_WRITEBACK 256x256"; Fm3 "env FM3_WRITEBACK" clears it). Each size written is logged once per value.
bool ExperimentWriteback(const regpu::ResolveCopy& c)
{
    static std::mutex m;
    static std::string text;
    static std::set<std::pair<uint32_t, uint32_t>> sizes, logged;
    // (Asked at every resolve: the variable is read 4 times a second, and nothing is locked while it is unset.)
    static std::atomic<int64_t> nextRead{ 0 };
    static std::atomic<bool> any{ false };
    const int64_t now = GetTickCount64();
    if (now < nextRead.load(std::memory_order_relaxed))
    {
        if (!any.load(std::memory_order_relaxed))
            return false;
    }
    else
    {
        nextRead.store(now + 250, std::memory_order_relaxed);
        const char* v = getenv("FM3_WRITEBACK");
        any.store(v && *v, std::memory_order_relaxed);
        if (!(v && *v))
        {
            std::lock_guard lock(m);
            text.clear();
            sizes.clear();
            logged.clear();
            return false;
        }
    }
    const char* env = getenv("FM3_WRITEBACK");
    std::lock_guard lock(m);
    if ((env ? env : "") != text)
    {
        text = env ? env : "";
        sizes.clear();
        logged.clear();
        for (const char* p = text.c_str(); *p;)
        {
            uint32_t w = 0, h = 0;
            if (sscanf(p, "%ux%u", &w, &h) == 2)
                sizes.insert({ w, h });
            const char* comma = strchr(p, ',');
            if (!comma)
                break;
            p = comma + 1;
        }
        Log("writeback experiment: FM3_WRITEBACK=\"%s\" (%zu sizes)", text.c_str(), sizes.size());
    }
    if (!sizes.count({ c.width, c.height }))
        return false;
    if (logged.insert({ c.width, c.height }).second)
        Log("writeback experiment: %ux%u format %u at %08X rows %d-%d written back", c.width, c.height, c.format, c.base, c.top, c.bottom);
    return true;
}

// Sets a ReGPU environment default unless the player (or a test script) already set it.
void DefaultEnv(const char* name, const char* value)
{
    if (!getenv(name))
        _putenv_s(name, value);
}

// Performance presets: ReGPU's Hyper (include/regpu/hyper.h, ported from the FM2 port's hyper-prototype branch,
// session 43, which replaced our older turbo/potato presets). settings.ini hyper= (or the older perf_preset=, the
// launcher's slider) = off|mild|low|extreme|potato|auto; FM3_PERF_PRESET or REGPU_HYPER override; each cut also
// has its own REGPU_HYPER_* switch and settings.ini hyper_* key (helpers, aniso, resolve1010, nodiscard, scene,
// upscale, cull, freeze, shader_cuts):
//   mild     scene 50% (FSR 1), anisotropy 4x, helper surfaces 75%, lossless copies and no-discard shaders
//   low      scene 35%, one-pass bilinear upscale, anisotropy 2x, helpers 50%
//   extreme  scene 25% (FSR 1's smallest step here: 33%), bilinear upscale, anisotropy 2x, helpers 50%
//   potato   native resolution; the lossless shader cuts (smart multiplies, swizzle selects, tail and fetch
//            specialisation, optimised builds, resolve by size), draws under 256 px culled (probe, hysteresis, pin),
//            3D helper views frozen after 120 frames and redrawn every 4th, helpers 25%, no anisotropic filtering
//   auto     potato on an integrated GPU, else off
// FM3 adds its own draw-cutting options from Mild up (game hooks ReGPU can't do: one tile, half-rate shadows,
// mirror and reflections, distant AI cars at detail level 3). The old "turbo" name maps to off plus the shader cuts.
// Mesh decimation (Extreme 25%, Potato 10% of triangles kept) and vertex-cache order (Potato) use meshoptimizer
// (thirdparty/meshoptimizer, session 43); settings.ini hyper_decimate= overrides.
void ApplyPerfPreset(regpu::Config& c)
{
    const Settings& s = GetSettings();
    regpu::hypermode::Settings h;
    std::string name = getenv("FM3_PERF_PRESET") ? getenv("FM3_PERF_PRESET") : s.Extra("hyper");
    if (name.empty())
        name = s.Extra("perf_preset");
    for (char& ch : name)
        ch = char(tolower(uint8_t(ch)));
    if (name == "turbo")
    {
        name = "off";
        h.shaderCuts = 1;
    }
    h.preset = name;
    auto num = [&s](const char* key) { const std::string v = s.Extra(key); return v.empty() ? -1 : atoi(v.c_str()); };
    h.scenePercent = num("hyper_scene");
    h.cheapUpscale = num("hyper_upscale");
    h.noDiscard = num("hyper_nodiscard");
    h.helpers = num("hyper_helpers");
    h.resolve1010 = num("hyper_resolve1010");
    h.aniso = num("hyper_aniso");
    h.cullPixels = num("hyper_cull");
    if (h.cullPixels == 0) // (the launcher's "Off": Hyper's 0 would cull only fully hidden draws)
    {
        h.cullPixels = -1;
        DefaultEnv("REGPU_HYPER_CULL_PIXELS", "-1");
    }
    h.freeze = num("hyper_freeze");
    h.decimate = num("hyper_decimate");
    if (const int cuts = num("hyper_shader_cuts"); cuts >= 0)
        h.shaderCuts = cuts;
    // Smaller helper surfaces wash FM3's whole picture out yellow (session 23, again in session 43: one of its
    // exposure passes counts as a helper and is read back at the wrong size), so FM3 keeps them full size unless
    // settings.ini hyper_helpers= or REGPU_HYPER_HELPERS asks.
    if (h.helpers < 0 && !getenv("REGPU_HYPER_HELPERS"))
        h.helpers = 100;
    // The launcher's iGPU tab (session 43, Intel/integrated GPU tests): each key to its ReGPU switch (patch
    // regpu-igpu-intel), an explicit environment variable still winning.
    static const std::pair<const char*, const char*> kIgpuKeys[] = {
        { "igpu_rootsig11", "REGPU_ROOTSIG_11" }, { "igpu_spec_constants", "REGPU_SPEC_CONSTANTS" }, { "igpu_half_regs", "REGPU_HALF_REGS" },
        { "igpu_uma_no_arena", "REGPU_UMA_NO_ARENA" }, { "igpu_uma_textures", "REGPU_UMA_TEXTURES" },
    };
    for (const auto& [key, env] : kIgpuKeys)
        if (s.Extra(key) == "1")
            DefaultEnv(env, "1");
    if (const std::string t = s.Extra("igpu_compile_threads"); !t.empty() && t != "auto")
        DefaultEnv("REGPU_COMPILE_BACKGROUND_THREADS", t.c_str());
    if (const std::string t = s.Extra("igpu_texture_budget"); !t.empty()) // (session 43: ReGPU's texture budget, default 1024 MB)
        DefaultEnv("REGPU_TEXTURE_BUDGET_MB", t.c_str());
    // Culling blended draws (REGPU_HYPER_CULL_ALL, for FM2's blended cars) drops FM3's HUD text: blended, depth-tested
    // glyphs whose strokes cover ~70 guest pixels (session 43, REGPU_TRACE_HYPER_CULL). FM3 culls opaque draws only.
    DefaultEnv("REGPU_HYPER_CULL_ALL", "0");
    regpu::hypermode::Configure(h);
    const regpu::hypermode::Preset p = regpu::hypermode::Level();
    if (const uint32_t aniso = regpu::hypermode::AnisotropyCap(); aniso < 16 && !getenv("REGPU_NO_ANISO"))
        regpu::SetAnisotropy(int(aniso));
    if (regpu::hypermode::CheapUpscale())
        DefaultEnv("REGPU_UPSCALE_BILINEAR", "1");
    if (const uint32_t scene = regpu::hypermode::SceneScalePercent())
    {
        // FSR 1's steps: 1 = 67%, 2 = 59%, 3 = 50%, 4 = 33% of the output per axis.
        const uint32_t quality = scene >= 67 ? 1 : scene >= 59 ? 2 : scene >= 50 ? 3 : 4;
        if (c.upscaler == regpu::Config::Upscaler::Off)
        {
            c.upscaler = regpu::Config::Upscaler::Fsr1;
            c.quality = quality;
        }
        else
            c.quality = std::max(c.quality, quality);
    }
    // Mild and up (user's request, session 33): FM3's draw-cutting options too, each normally its own launcher switch.
    // Through their environment variables (read before settings.ini by each hook), so an explicit FM3_* value wins.
    if (p != regpu::hypermode::Preset::Off)
    {
        DefaultEnv("FM3_ONE_TILE", "1");
        DefaultEnv("FM3_SHADOW_RATE", "2");
        DefaultEnv("FM3_MIRROR_RATE", "2");
        DefaultEnv("FM3_REFLECT_RATE", "2");
        DefaultEnv("FM3_CAR_LOD_FAR", "3");
    }
    regpu::hypermode::LogSummary();
}
} // namespace

void GpuStart()
{
    regpu::Config& config = regpu::GetConfig();
    ApplySettings(config);
    ApplyPerfPreset(config);
    config.userDir = UserDir(); // shader and pipeline caches
    config.windowTitle = L"Forza Motorsport 3";

    regpu::Host& host = regpu::GetHost();
    host.base = g_base;
    host.physicalSection = PhysicalSection();
    host.alloc = [](bool physical, uint32_t size, uint32_t align) {
        return GuestAlloc(physical ? Heap::Physical : Heap::Virtual, size, align);
    };
    host.createThread = [](const char* name, void (*body)(PPCContext&)) { CreateHostGuestThread(name, body); };
    host.currentPcr = [] { return CurrentGuestThread()->pcr; };
    host.guestCall = [](PPCContext& ctx, uint32_t address) { GuestCall(ctx, address); };
    host.interruptLockAcquire = [](int cpu) { InterruptLockAcquire(cpu); };
    host.interruptLockRelease = [] { InterruptLockRelease(); };
    host.cpuUsage = [](double wallSeconds) { return GuestThreadCpuUsage(wallSeconds); };
    host.logThreadStacks = [](const char* why) { LogAllHostThreadStacks(why); };
    host.currentStack = [](int maxFrames) { return CurrentHostStack(maxFrames); };
    // The vertex arena's write watch (src/memory.cpp; ReGPU REGPU_VERTEX_ARENA).
    host.watchArm = [](uint32_t offset, uint32_t bytes) { return WriteWatchArm(offset, bytes); };
    host.watchClean = [](uint32_t offset, uint32_t bytes, uint64_t since) { return WriteWatchClean(offset, bytes, since); };
    host.watchSeq = [] { return WriteWatchSeq(); };

    // First boot: every shader in the game files compiled up front behind ReGPU's progress overlay, then
    // the shipped pipeline list (src/pipeline_seed.h, tools/pipeline_seed.py) built (session 24). Later boots
    // load both from the caches. FM3_NO_STATIC_SHADERS=1 / REGPU_NO_STATIC_SHADERS=1 skip the shaders,
    // REGPU_NO_SEED_PIPELINES=1 the pipelines.
    host.staticShaders = [] { return StaticShaders(); };
    host.staticShadersSaved = [] { return StaticShadersSaved(); };
    host.pipelineSeed = [] { return kPipelineSeed; };
    // The first-boot shader compile screen (session 39): a rev counter climbing to the limiter (src/boot_screen.cpp).
    host.bootScreen = BootScreen;
    BootScreenTest();

    // The bar across the 3D view (rows ~360-511, ending at the EDRAM tile seam: FM3 draws its 2x MSAA 720p
    // scene in two tiles, 1280x512 and 1280x208): ReGPU's "foreign MSAA" path, which reinterprets a surface's
    // samples when a view with another MSAA layout reads it, washed those rows out. Off, the band is gone and
    // nothing else changed (paused start grid, A/B in NOTES.md session 8). FM3_FOREIGN_MSAA=1 keeps it on.
    if (!getenv("FM3_FOREIGN_MSAA"))
        DefaultEnv("REGPU_NO_FOREIGN_MSAA", "1");

    // Frame rate near other cars (OPEN-ISSUES.md P, session 13): at every INTERRUPT packet the command
    // processor waited until every queued fence had passed the host GPU (ReGPU's fence ordering, from the
    // FM4 port), so the game, the command processor and the GPU took turns. Off, as PGR4 runs: 1440p 4x
    // MSAA at a 120 cap near cars 69-77 -> 100-120 fps at the same draw counts (two live A/B rounds), no
    // visual change seen (spectator LOD popping is the same either way). FM3_FENCE_ORDER=1 keeps the
    // ordering; live: Fm3 "switch REGPU_NO_FENCE_ORDER 0|1". The switch is a global ReGPU reads at static
    // initialisation, so it is set through SetSwitch, not the environment.
    if (!getenv("FM3_FENCE_ORDER") && !getenv("REGPU_NO_FENCE_ORDER"))
        regpu::driver::SetSwitch("REGPU_NO_FENCE_ORDER", true);

    // Per-draw CPU cost (OPEN-ISSUES.md P, session 13), live A/B on a paused start grid (3,516 draws,
    // 1440p 4x MSAA): REGPU_TEXTURE_CHECK_STAGGER (unchanged textures rechecked every 4th/16th frame, as
    // FM4 ships it; texture checks 0.46 -> 0.33 ms) 110 -> 114.5 fps; REGPU_DIRECT_DYNAMIC_INDICES (large
    // index buffers read in place unless seen changing; indices 0.43 -> 0.16 ms) 110 -> 115.4; both
    // 108 -> 120. Driving: no stale textures or shadow flicker seen. FM3_NO_DRAW_SHORTCUTS=1 leaves both
    // off; live: Fm3 "switch NAME 0|1".
    if (!getenv("FM3_NO_DRAW_SHORTCUTS"))
    {
        if (!getenv("REGPU_TEXTURE_CHECK_STAGGER"))
            regpu::driver::SetSwitch("REGPU_TEXTURE_CHECK_STAGGER", true);
        if (!getenv("REGPU_DIRECT_DYNAMIC_INDICES"))
            regpu::driver::SetSwitch("REGPU_DIRECT_DYNAMIC_INDICES", true);
    }

    // Early fences (OPEN-ISSUES.md P, session 13): the game's render thread spent ~78% of its time in
    // D3D's block-on-fence (sub_824B5048 -> sub_824B4F78, the fence word at the device's +0x2A90),
    // waiting for fences ReGPU wrote only after the host GPU finished; the same paused grid ran equally
    // fast at 720p 1x and 1440p 4x MSAA, so it was the latency, not the GPU. Fences written when the
    // command processor reaches them (FH1R ships this on): paused grid 117 -> 147-153 fps, driving clean.
    // Needs our patch regpu-early-fence-writebacks: without it the fence behind the livery's deferred
    // write-back landed early and the CPU compressed an unwritten livery (noise or blank).
    // FM3_LATE_FENCES=1 keeps fences after the host GPU. Read at start (not live).
    if (!getenv("FM3_LATE_FENCES"))
        DefaultEnv("REGPU_EARLY_FENCES", "1");

    // Vertex arena (session 19, from the FM2 port): large vertex buffers kept in video memory, copied again only
    // when the game writes their pages (src/memory.cpp write watch); read in place, the GPU fetched them over PCIe
    // in every draw. ReGPU opens the GPU's heap over a view of guest memory of its own so the guest's views can be
    // made read-only (over the guest's view: VirtualProtect error 87, the arena switched itself off).
    // settings.ini vertex_arena=0 or FM3_NO_VERTEX_ARENA=1: off (read in place, as before); live A/B from the next
    // frame: Fm3 "switch REGPU_NO_VERTEX_ARENA 1". REGPU_VERTEX_VERIFY=1 (live) hashes "unchanged" buffers and
    // logs any the watch missed.
    if (!getenv("FM3_NO_VERTEX_ARENA") && GetSettings().Extra("vertex_arena") != "0")
        DefaultEnv("REGPU_VERTEX_ARENA", "1");

    // Alpha-test-free pixel shaders (session 20, our ReGPU patch regpu-split-alpha-test): draws with the alpha test
    // and alpha-to-mask off use a variant of their pixel shader without the run-time alpha test's `discard`, so the
    // GPU keeps early depth rejection. CrownJewel race grid, paused, 1440p 4x: 203 -> 213.6 fps (4 interleaved
    // rounds, every "on" above every "off"), screenshot byte-identical. FM3_ALPHA_KILL_ALWAYS=1: off; live A/B:
    // Fm3 "switch REGPU_SPLIT_ALPHA_TEST 0|1".
    if (!getenv("FM3_ALPHA_KILL_ALWAYS"))
        DefaultEnv("REGPU_SPLIT_ALPHA_TEST", "1");

    // Lean texture fetch + 2_10_10_10 copies kept in 10 bits (session 23, PERF-RESULTS.md; default at the user's
    // request after a race drive). REGPU_LEAN_FETCH=1: a pixel shader's 2D fetch samples once through the samplers
    // as an indexed table instead of a branch per sampler (read at the first translation; its shaders cache apart,
    // so their variants build once, then come from the pipeline list at boot). REGPU_COPIES_1010102=1: copies of
    // 2_10_10_10 surfaces at 10 bits instead of half float. Frozen CrownJewel grid, 1440p 4x: lean fetch 1 +1.8%,
    // 10-bit copies +2.3% (live, 4/4 rounds, worst pixel difference 2/255); the user drove a career race with both
    // and saw nothing wrong (crowd flags fine: that was plain multiply, which is not part of this). A preset that
    // sets lean fetch 2 keeps it (applied above). FM3_NO_SHADER_SHORTCUTS=1: both off (as before session 23); live
    // A/B for the copies: Fm3 "switch REGPU_COPIES_1010102 0|1".
    if (!getenv("FM3_NO_SHADER_SHORTCUTS"))
    {
        DefaultEnv("REGPU_LEAN_FETCH", "1");
        DefaultEnv("REGPU_COPIES_1010102", "1");
        // Session 24 (PERF-PLAN L30, patch regpu-fetch-variants): pixel-shader variants whose plain texture fetches
        // skip the fetch-constant special cases (car paint 117 KB -> 20 KB of GPU code): GPU frame -2.5% on the
        // grid, byte-identical picture. Live: Fm3 "switch REGPU_FETCH_VARIANTS 0|1".
        DefaultEnv("REGPU_FETCH_VARIANTS", "1");
        // Session 33 (iGPU work): every 2D fetch specialised by its fetch-constant words as literals (SpecVariant):
        // the car paint 117 KB -> 22 KB, 236 -> 41 sample instructions. 0 pixels changed; races 1440p 4x +3.6%,
        // 4K 4x +9.0%, the 9800X3D's iGPU at 720p +26.9% (p99 lower on all three). Live: switch REGPU_FETCH_SPECIALIZE 0|1.
        DefaultEnv("REGPU_FETCH_SPECIALIZE", "1");
    }

    // Parallel draw workers (OPEN-ISSUES.md P): draw translation on 3 threads, which helps where the
    // command processor is the bound (near other cars). Occlusion queries (the sun flare and glare) work
    // with them since our ReGPU patch regpu-worker-queries (session 14: a counting query's draws go to the
    // command processor's list; before, every query answered a fixed 1000 and the launcher had an
    // "Accurate sun flare" option that turned the workers off; settings.ini accurate_sun is now ignored).
    // REGPU_DRAW_WORKERS=<n> overrides; live A/B: Fm3 "switch REGPU_NO_DRAW_WORKERS 1",
    // Fm3 "switch REGPU_NO_WORKER_QUERIES 1" (the old fixed answer).
    DefaultEnv("REGPU_DRAW_WORKERS", "3");
    // The draw thread (session 14): the command processor only parses and queues; a draw thread translates
    // in stream order and hands big segments to the draw workers (our ReGPU patch regpu-draw-thread-pieces),
    // with occlusion queries answered on the command processor and recorded there. Cars grid, paused:
    // 720p 1x 175 -> 202 fps, 1440p 4x MSAA 175 -> 181 (GPU-bound there). FM3_NO_DRAW_THREAD=1: workers on
    // the command processor as before. Default: the draw thread alone (REGPU_DRAW_THREAD_SERIAL). The sun flare
    // with the workers on the draw thread works since session 15 (the command processor cleared the draw
    // thread's "query counting" flag at the query's end, ahead of it), but they are slower: cars grid, paused,
    // 720p 1x: serial ~216 fps, with workers ~195 (session 15 A/B). FM3_DRAW_THREAD_PIECES=1: with workers.
    if (!getenv("FM3_NO_DRAW_THREAD"))
    {
        DefaultEnv("REGPU_DRAW_THREAD", "1");
        if (!getenv("FM3_DRAW_THREAD_PIECES"))
            DefaultEnv("REGPU_DRAW_THREAD_SERIAL", "1");
    }
    // Asynchronous frame end (session 25, PERF-PLAN L12, our ReGPU patch regpu-async-present): the frame's close,
    // submit, Present and fence run on ReGPU's command-list replay thread in stream order; the draw thread no longer
    // waits there for the replay and then inside the driver's Present (AMD's driver sleeps there while frames are
    // queued: ~10% of the draw thread's time, the CPU limit). Frozen grid, live, 5/5 rounds each, byte-identical:
    // 720p 1x 216.5 -> 255.2 fps (+17.9%), 1440p 4x 232.4 -> 242.3 (+4.3%). FM3_SYNC_PRESENT=1: off; live A/B:
    // Fm3 "switch REGPU_ASYNC_PRESENT 0|1". settings.ini async_present=0 (launcher Settings > Testing "Asynchronous
    // present") is off too.
    // One guest tile (session 26, src/hooks/one_tile.cpp): the command buffers recorded for two tiles keep their
    // per-tile window fixups; ReGPU merges them into one window.
    bool OneTileEnabled();
    if (OneTileEnabled())
        DefaultEnv("REGPU_WINDOW_FIXUP_UNION", "1");
    Log("gpu: one guest tile %s", OneTileEnabled() ? "on" : "off");
    // Command processor waits under a frame cap (session 27, PERF-PLAN L40, our ReGPU patch regpu-cp-idle-sleep):
    // with the frame rate held by a cap the CP is idle most of each frame but spun a whole core (Sleep(0) on an empty
    // ring, and up to 100 ms of SwitchToThread in WAIT_REG_MEM). Frozen grid at a 60 cap, 1440p 4x: CP 98% -> ~20%
    // of a core, process 2.1 -> ~1.5 cores, 60 fps held. Uncapped it costs a little where the CPU limits (720p 1x
    // ~-1%, a few dips), so only at caps of 144 and below or with vsync. FM3_CP_SPIN=1: off (spin as before).
    {
        const Settings& s = GetSettings();
        if (!getenv("FM3_CP_SPIN") && (s.vsync || (s.maxFps > 0 && s.maxFps <= 144)))
        {
            DefaultEnv("REGPU_CP_SPIN_MS", "1");
            DefaultEnv("REGPU_CP_IDLE_SLEEP_US", "200");
        }
        Log("gpu: command processor %s", getenv("REGPU_CP_IDLE_SLEEP_US") && atoi(getenv("REGPU_CP_IDLE_SLEEP_US")) > 0
            ? "sleeps while idle (frame cap)" : "spins while idle");
    }
    if (!getenv("FM3_SYNC_PRESENT") && GetSettings().Extra("async_present") != "0")
        DefaultEnv("REGPU_ASYNC_PRESENT", "1");
    Log("gpu: asynchronous present %s", getenv("REGPU_ASYNC_PRESENT") && strcmp(getenv("REGPU_ASYNC_PRESENT"), "0") ? "on" : "off");
    // Upload ring in VRAM (session 29, PERF-PLAN L47, our ReGPU patch regpu-gpu-upload-ring): with resizable BAR the
    // ring that holds every draw's constants (and vertex/texture uploads) can live in a GPU upload heap. Frozen grid,
    // byte-identical: 1440p 4x +12%, 720p 1x (CPU-bound) -5%. Session 31, after session 30's per-draw byte cuts: grid
    // +26%, 12 s drives +18%, 720p 1x -0.8%; the user's races (perf log off) clearly faster. On by default since
    // session 31 (user's decision); settings.ini vram_upload_ring=0 (launcher Settings > Testing "Upload ring in
    // VRAM") turns it off; ReGPU falls back to system memory without ReBAR. Live A/B:
    // Fm3 "switch REGPU_GPU_UPLOAD_RING 0|1".
    // Light performance log (session 31, our ReGPU patch regpu-perf-lite): one line a second, no per-draw timing.
    // settings.ini perf_log_light=1 (launcher Settings > Testing "Light performance log"); REGPU_PERF_LITE=1 directly.
    if (GetSettings().Extra("perf_log_light") == "1")
        DefaultEnv("REGPU_PERF_LITE", "1");
    // Benchmark race (session 33, FM3_BENCHMARK=<seconds>, the launcher's Benchmark button): the AI drives the player's
    // car (hooks/ai_player.cpp) and the light performance log measures (hooks/grid_test.cpp has the route and timing).
    if (const char* bench = getenv("FM3_BENCHMARK"); bench && atoi(bench) > 0)
    {
        DefaultEnv("REGPU_PERF_LITE", "1");
        DefaultEnv("FM3_AI_PLAYER", "1");
    }
    // Long-frame stacks (session 33, src/long_stacks.cpp): settings.ini long_frame_stacks=1 (FM3_LONG_STACKS=1 or =N ms)
    // samples ReGPU's draw thread every millisecond and logs where it was during each long frame. Turns on the
    // long-frame log (REGPU_LONG_FRAMES) it hangs on.
    if (GetSettings().Extra("long_frame_stacks") == "1")
        DefaultEnv("FM3_LONG_STACKS", "1");
    if (const char* stacks = getenv("FM3_LONG_STACKS"); stacks && *stacks && strcmp(stacks, "0"))
    {
        DefaultEnv("REGPU_LONG_FRAMES", "1");
        void StartLongFrameSampler();
        StartLongFrameSampler();
    }
    // Anisotropy as the game asks (session 32, PERF-PLAN I8): each fetch's own anisotropy instead of 16x everywhere.
    // GPU -3.8% at 4K 4x, ~9% of pixels slightly softer at a distance (closer to the console). Off by default;
    // settings.ini aniso_from_game=1 (launcher Graphics "Anisotropy as the game asks"); REGPU_ANISO_FROM_FETCH=1 directly.
    if (GetSettings().Extra("aniso_from_game") == "1")
        DefaultEnv("REGPU_ANISO_FROM_FETCH", "1");
    if (GetSettings().Extra("vram_upload_ring") != "0")
        DefaultEnv("REGPU_GPU_UPLOAD_RING", "1");
    // (The VRAM ring wins where the GPU is the limit, system memory where ReGPU's draw thread is: session 36, 1440p 1x
    // grid +5.5% with vram_upload_ring=0, 4x MSAA -1.6%, 4K far worse. The launcher's tooltip says so; the user's choice
    // (session 36) is that toggle, not an automatic pick. REGPU_UPLOAD_RING_AUTO=1, ReGPU patch 57, stays for tests.)
    // DXC shader compiler (session 35, ReGPU patches regpu-dxc, regpu-vs-corners, regpu-pso-lock): translated shaders
    // compiled by DXC to DXIL instead of by FXC to DXBC. Races: iGPU 720p +4% over FXC, RX 7900 XTX 4K 4x +2.5% at
    // equal draws, same picture; its own shader and pipeline caches (the first boot after switching compiles everything).
    // Off by default; settings.ini shader_compiler=dxc (launcher Settings > Testing "DXC shader compiler"); REGPU_DXC=1
    // directly. Needs dxcompiler.dll + dxil.dll beside fm3.exe (else FXC, with a log line).
    if (GetSettings().Extra("shader_compiler") == "dxc")
        DefaultEnv("REGPU_DXC", "1");
    // VR (session 37, ReGPU src/render/xr.cpp): settings.ini vr=screen|cockpit (or FM3_VR / REGPU_VR) puts the game on a virtual
    // screen in an OpenXR headset (the active runtime: Virtual Desktop, SteamVR, Oculus, ...); cockpit also head-tracks
    // the in-car view (src/hooks/vr_camera.cpp); the window stays.
    // (The launcher passes FM3_VR on every run since session 39: its VR tab's mode, or off from the other Play buttons.)
    if (const char* v = getenv("FM3_VR"); v && *v)
    {
        if (strcmp(v, "off") != 0 && strcmp(v, "0") != 0)
            DefaultEnv("REGPU_VR", v);
    }
    else if (const std::string vr = GetSettings().Extra("vr"); !vr.empty() && vr != "off" && vr != "0")
        DefaultEnv("REGPU_VR", vr.c_str());
    // vr=stereo (session 38): ReGPU draws the main view a second time for the other eye. FM3 draws its scene turned
    // 180 degrees (the resolved scene image is upside down and mirrored; the final composite turns it back), so camera
    // +x lands at clip -x: the eye offsets are swapped (measured on the grid: unswapped, near objects moved right from
    // the left eye's picture to the right eye's; FM3_VR_FAKE head moves were the right way round).
    DefaultEnv("REGPU_VR_EYE_FLIP", "1");
    // In-car camera motion (shake, G lean) is off with VR (src/kernel/imports_io.cpp); vr_camera_motion=1 keeps it.
    if (GetSettings().Extra("vr_camera_motion") == "1")
        DefaultEnv("FM3_VR_CAMERA_MOTION", "1");
    // How far the head may move from the seat in VR (session 39): vr_eye_limit = cm each way, 0 = no limit (default).
    if (const std::string limit = GetSettings().Extra("vr_eye_limit"); !limit.empty())
        DefaultEnv("FM3_VR_EYE_LIMIT", limit.c_str());
    // The HUD on its own panel in the headset (stereo): FM3's last pass draws the HUD onto a cleared surface first and
    // then the tone-map composite under it by destination alpha (session 38 capture); this is that composite's pixel
    // shader, so ReGPU takes the HUD from the surface before it and draws the composite opaque.
    DefaultEnv("REGPU_VR_HUD_UNDER", "5b7efac9b0c87a00");
    // Triple screen (session 39, ReGPU REGPU_TRIPLE; src/hooks/vr_camera.cpp TripleHandOff): settings.ini triple=1 (or
    // FM3_TRIPLE=1); the resolution is the three screens together (e.g. 7680x1440); triple_fov (each screen's vertical
    // field of view, degrees; 0 = the camera's own where it fits), triple_angle (degrees between the screens' axes; 0 =
    // edge to edge), triple_bezel (degrees for the bezels). Not together with VR. FM3 draws its scene turned 180 degrees,
    // so the turn is flipped in clip space (as the VR eye offset).
    {
        const char* t = getenv("FM3_TRIPLE");
        const std::string triple = t ? t : GetSettings().Extra("triple");
        const char* vrNow = getenv("REGPU_VR");
        if (triple == "1" && !(vrNow && *vrNow && strcmp(vrNow, "off") != 0 && strcmp(vrNow, "0") != 0))
        {
            DefaultEnv("REGPU_TRIPLE", "1");
            DefaultEnv("REGPU_TRIPLE_FLIP", "1");
            // settings.ini triple_<key> -> FM3_TRIPLE_<KEY> (src/hooks/triple_screen.cpp): the auto layout's fov, angle,
            // bezel (degrees), the physical layout's mode=physical, diag (inches), dist (cm), bezel_mm, gap_mm,
            // angle_left, angle_right (degrees; 0 = facing the eye).
            for (const char* key : { "fov", "angle", "bezel", "mode", "diag", "dist", "bezel_mm", "gap_mm", "angle_left", "angle_right" })
            {
                const std::string v = GetSettings().Extra((std::string("triple_") + key).c_str());
                if (v.empty())
                    continue;
                std::string name = std::string("FM3_TRIPLE_") + key;
                for (char& c : name)
                    c = char(toupper(static_cast<unsigned char>(c)));
                DefaultEnv(name.c_str(), v.c_str());
            }
        }
    }
    // Headset pacing (session 39, FH1R's scheme): in the head-tracked modes the frame cap follows the headset's display
    // rate while a headset session runs (a new frame for every headset frame, none wasted), and the headset thread
    // waits a moment for a frame that is nearly done. vr_pace=0 in settings.ini (launcher: off) leaves the cap as set.
    if (GetSettings().Extra("vr_pace") != "0")
        DefaultEnv("REGPU_VR_PACE", "1");
    Log("gpu: shader compiler %s", getenv("REGPU_DXC") && *getenv("REGPU_DXC") && strcmp(getenv("REGPU_DXC"), "0") ? "DXC (if its DLLs load)" : "FXC");
    Log("gpu: upload ring in VRAM %s", getenv("REGPU_GPU_UPLOAD_RING") && strcmp(getenv("REGPU_GPU_UPLOAD_RING"), "0") ? "on (if resizable BAR)" : "off");
    // Sun glare size (session 14): FM3 sizes the glare from the occlusion query's raw sample count (not
    // the visible share): none below ~0.55x our count of a fully visible sun, largest at ~1.5-2x. At 1x
    // the glare covered half the screen, far bigger than console footage (YouTube Fx4sKnongdM, 1:16-1:21);
    // 0.6 matched it by eye (0.75, 0.65 still too big). Why the console counts fewer samples is not known.
    // REGPU_ZPD_SCALE=<f> overrides (live: Fm3 "env REGPU_ZPD_SCALE 1").
    DefaultEnv("REGPU_ZPD_SCALE", "0.6");
    // Host EDRAM surfaces 720 guest rows tall instead of ReGPU's 1024 (session 33, VRAM): FM3's one tall pass (the
    // 1024x1024 shadow atlas) draws at 1.5x scale, so its corner is 1536 rows at 4K either way. At 4K 4x: 3840x2160
    // surfaces instead of 3840x3072: -1.2 GB VRAM and +3.8% fps on the frozen grid, picture within cross-boot noise.
    // REGPU_SURFACE_ROWS=1024 = as before (start-time).
    DefaultEnv("REGPU_SURFACE_ROWS", "720");
    // settings.ini gpu=<part of a name | index | high | low> (launcher Display "Graphics card", session 33): the GPU
    // ReGPU renders on (REGPU_ADAPTER, read when its device is made). Empty = Windows' default.
    if (const std::string gpu = GetSettings().Extra("gpu"); !gpu.empty())
        DefaultEnv("REGPU_ADAPTER", gpu.c_str());
    Log("gpu: draw workers on by default (occlusion queries counted on the command processor)");

    // Q18, the road surface: the road shader samples a 512x512 detail image the game composes at run time
    // (half of it a frame, plus its mip chain) from a base asphalt texture and four 32x512 strips it reads
    // as DXT1. The strips are rendered and resolved once, when the track loads (none during a race; an
    // earlier capture had the 32x512 resolve). ReGPU keeps resolves on the GPU, so their memory never got
    // the strips: they held whatever was there before (pieces of car textures), and the composed image
    // came out with light crossing bands and dashed lines (streaks on the asphalt turning with the camera).
    // The same mechanism FM4's port fixed with this rule: 32x512 colour resolves written back to memory.
    // Now, not Deferred: these 8888 strips are compressed to separate DXT1 strips (32 KB each, elsewhere)
    // right after they are rendered, by code reading their memory; a write-back a frame or two later came
    // too late (streaks unchanged with 64 strips written back Deferred). Now stalls the GPU for each, ~64
    // resolves once per track load. Each one is logged once.
    // FM3_NO_STRIP_WRITEBACK=1: not written back (A/B, from the start of a track).
    //
    // Car pictures (My Cars, garage): when a car changes (paint saved, bought...), the game renders it and resolves
    // a 512x256 picture (the big one) and a 256x128 thumbnail, 8888 with the red/blue swap a CPU-read copy has, then
    // its CPU code copies them into the car's own images (e.g. a 512x256 8888 texture) that are saved with it. Kept
    // on the GPU, the copies read whatever was in memory: noise, or nothing. Found with FM3_WRITEBACK during
    // repaints (session 9: 512x256 at 14C5F000, 256x128 at 1205F000, once each per save); confirmed in game.
    // Pictures saved by earlier builds stay broken until the car is changed again. No resolve of these sizes was
    // seen elsewhere (menus, race loads). FM3_NO_PICTURE_WRITEBACK=1: not written back (A/B).
    host.resolveWriteback = [](const regpu::ResolveCopy& c) {
        regpu::Writeback w;
        if (!c.depth && ExperimentWriteback(c))
        {
            w.mode = regpu::Writeback::Mode::Now;
            return w;
        }
        static LiveSwitch noPictures("FM3_NO_PICTURE_WRITEBACK");
        if (!noPictures && !c.depth && c.format == 6 && c.top == 0 &&
            ((c.width == 512 && c.height == 256) || (c.width == 256 && c.height == 128)))
        {
            w.mode = regpu::Writeback::Mode::Now;
            static std::atomic<int> logged{ 0 };
            if (logged.fetch_add(1) < 32)
                Log("picture writeback: %08X %ux%u", c.base, c.width, c.height);
            return w;
        }
        // Car liveries (issue B2, session 11): when a car with vinyls is loaded (boot, garage), the game builds its
        // livery on the GPU as an 8888 2048x2048 texture (close-ups: photo mode, race countdown) and a 1024x1024 one
        // (racing): the first strip/base resolve declares the whole image, the other strips and the vinyl regions are
        // resolved into tile positions inside it (ReGPU draws them into the image: ResolveIntoEnclosing). Its CPU code
        // then compresses them to DXT from memory, so kept on the GPU the car showed noise or other cars' decals.
        // Written back Deferred (in memory before the next fence, no stall); ReGPU adds each strip/region drawn into
        // a written-back image as its own deferred rectangle. FM3_NO_LIVERY_WRITEBACK=1: not written (A/B);
        // FM3_LIVERY_WRITEBACK_NOW=1: written at once (stalls ~55 ms per 2048 image).
        static LiveSwitch noLivery("FM3_NO_LIVERY_WRITEBACK");
        static LiveSwitch liveryNow("FM3_LIVERY_WRITEBACK_NOW");
        if (!noLivery && !c.depth && c.format == 6 && c.top == 0 &&
            ((c.width == 2048 && c.height == 2048) || (c.width == 1024 && c.height == 1024)))
        {
            w.mode = liveryNow ? regpu::Writeback::Mode::Now : regpu::Writeback::Mode::Deferred;
            static std::atomic<int> logged{ 0 };
            if (logged.fetch_add(1) < 32)
                Log("livery writeback: %08X %ux%u", c.base, c.width, c.height);
            return w;
        }
        static const bool off = getenv("FM3_NO_STRIP_WRITEBACK") != nullptr;
        if (off || c.depth || c.width != 32 || c.height != 512)
            return w;
        w.mode = regpu::Writeback::Mode::Now;
        static std::mutex m;
        static std::set<uint32_t> seen;
        std::lock_guard lock(m);
        if (seen.insert(c.base).second && seen.size() <= 64)
            Log("strip writeback: %08X 32x512 format %u rows %d-%d", c.base, c.format, c.top, c.bottom);
        return w;
    };

    // Test aid: ReGPU's timer panel in the top-left corner (the log clock, to a tenth of a second), so a
    // screenshot says where to look in fm3.log. Drawn by the presenter, not the game. FM3_NO_TIMER=1 hides
    // it; live: Fm3 "switch REGPU_TIMER 0|1".
    if (!getenv("FM3_NO_TIMER"))
        DefaultEnv("REGPU_TIMER", "1");
    // Fps panel (session 32, our ReGPU patch regpu-fps-panel): fps, frame times and a frame-time graph at the top
    // centre. settings.ini fps_panel=1 (launcher Graphics "Fps counter and frame-time graph"); live:
    // Fm3 "switch REGPU_FPS_PANEL 0|1".
    if (GetSettings().Extra("fps_panel") == "1")
        DefaultEnv("REGPU_FPS_PANEL", "1");

    // Q24: the main loop runs on guest vblanks (src/hooks/frame_time.cpp). At the frame cap's 60
    // (the default) one vblank per frame, as before; at other caps (max_fps, or the display's
    // refresh if lower and vsync is on) the vblank runs at twice the cap with mainthread30fps on
    // (divider 2), so the render thread draws every main frame. FM3_VBLANK_DIVIDER=1: one vblank
    // per frame at any cap. FM3_VBLANK_HZ=<n> sets the rate itself (divider 1, for measuring).
    {
        const Settings& s = GetSettings();
        uint32_t cap = s.maxFps;
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (s.vsync && EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            cap = std::min<uint32_t>(cap, mode.dmDisplayFrequency);
        cap = std::clamp<uint32_t>(cap, 30, 360);
        const char* div = getenv("FM3_VBLANK_DIVIDER");
        uint32_t divider = cap == 60 || (div && atoi(div) == 1) ? 1 : 2;
        uint32_t hz = cap * divider;
        const char* fixed = getenv("FM3_VBLANK_HZ");
        if (!fixed)
            fixed = getenv("REGPU_VBLANK_HZ");
        if (fixed)
        {
            hz = uint32_t(std::clamp(atoi(fixed), 30, 1000));
            divider = 1;
        }
        _putenv_s("REGPU_VBLANK_HZ", std::to_string(hz).c_str());
        FrameTimeSetPacing(hz / divider, hz, divider);
    }

    Log("gpu: ReGPU at %ux%u, msaa %u, vblank %s Hz", config.width, config.height, config.msaa, getenv("REGPU_VBLANK_HZ"));
    regpu::Start();
    StartGpuMemoryLog();

    // FM3_MINIMIZED=1 (automated tests, session 24): minimize the game window once ReGPU has opened it, without
    // taking focus, so test boots don't cover the desktop. The game keeps rendering (debug-server screenshots are
    // the game's own frames); grid fps measured ~1.3% lower minimized than shown.
    if (const char* m = getenv("FM3_MINIMIZED"); m && *m && strcmp(m, "0"))
        std::thread([] {
            for (int i = 0; i < 400; i++)
            {
                struct Find { HWND hwnd = nullptr; } find;
                EnumWindows([](HWND h, LPARAM p) -> BOOL {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(h, &pid);
                    wchar_t cls[16] = {};
                    if (pid == GetCurrentProcessId() && GetClassNameW(h, cls, 16) && !wcscmp(cls, L"regpu") && IsWindowVisible(h))
                    {
                        reinterpret_cast<Find*>(p)->hwnd = h;
                        return FALSE;
                    }
                    return TRUE;
                }, LPARAM(&find));
                if (find.hwnd)
                {
                    ShowWindow(find.hwnd, SW_SHOWMINNOACTIVE);
                    Log("gpu: window minimized (FM3_MINIMIZED)");
                    return;
                }
                Sleep(50);
            }
        }).detach();
}
