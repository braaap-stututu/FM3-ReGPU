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

#include <windows.h>

#include "regpu/driver.h"
#include "regpu/regpu.h"

#include "kernel/runtime.h"
#include "kernel/thread.h"
#include "live_switch.h"
#include "log.h"
#include "memory.h"
#include "settings.h"
#include "hooks/frame_time.h"

std::string CurrentHostStack(int maxFrames); // crash.cpp
void LogAllHostThreadStacks(const char* why); // hang.cpp

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
    c.upscaler = regpu::Config::Upscaler(uint32_t(s.upscaler));
    c.quality = s.quality;
    c.sharpness = s.sharpness;
}

// Test aid: FM3_WRITEBACK="WxH,WxH,..." writes colour resolves of those sizes back to memory at once, to find out
// which images the game reads on the CPU. Re-read whenever it changes, so it can be set live
// (Fm3 "env FM3_WRITEBACK 256x256"; Fm3 "env FM3_WRITEBACK" clears it). Each size written is logged once per value.
bool ExperimentWriteback(const regpu::ResolveCopy& c)
{
    static std::mutex m;
    static std::string text;
    static std::set<std::pair<uint32_t, uint32_t>> sizes, logged;
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
} // namespace

void GpuStart()
{
    regpu::Config& config = regpu::GetConfig();
    ApplySettings(config);
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

    // Parallel draw workers (OPEN-ISSUES.md P): draw translation on 3 threads, which helps where the
    // command processor is the bound (near other cars). With workers our pinned ReGPU can't count
    // occlusion queries (each answers a fixed 1000, which FM3 reads as "barely visible": no sun flare or
    // glare); PGR4's logged queries would fix that but conflict with regpu-zpd-tile-spans. The player
    // picks: settings.ini accurate_sun=1 (launcher: Graphics, "Accurate sun flare") keeps them off and
    // the sun right. REGPU_DRAW_WORKERS=<n> overrides; live A/B: Fm3 "switch REGPU_NO_DRAW_WORKERS 1".
    const bool accurateSun = GetSettings().Extra("accurate_sun") == "1";
    if (!accurateSun)
        DefaultEnv("REGPU_DRAW_WORKERS", "3");
    Log("gpu: %s", accurateSun ? "accurate sun flare (draw workers off)" : "draw workers on (no sun flare; accurate_sun=1 turns them off)");

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
}
