// The GPU is the ReGPU library (thirdparty/ReGPU, include/regpu/regpu.h), linked the way the FM4
// port links it. This file hands it the display settings and the runtime pieces it needs: guest
// memory, guest threads, guest calls and the interrupt lock. ReGPU provides the Vd* imports itself.
//
// Phase 1 skeleton: the runtime it calls into (memory.h, kernel/runtime.h, kernel/thread.h,
// settings.h, log.h) is ported from the FM2/FM4 templates in Phase 3; nothing compiles this file
// until then (CMakeLists.txt builds the game only once out/ppc exists).
//
// FM3-specific defaults set here (see FM3-QUIRKS.md):
//   Q24  FM3 paces itself on guest vblank. ReGPU's default vblank is 2x the frame cap (FM2 keeps
//        game time by the clock), which would run FM3 at double speed: start at 60 Hz.
//        FM3_VBLANK_HZ=<n> (or REGPU_VBLANK_HZ) overrides it, to measure game speed.
// Deliberately NOT carried over from FM4: its road-strip resolve write-back rule and its car-shadow
// cascade switches (FM4 addresses/formats). FM3's own versions (Q18, Q19) come in Phase 5, decided
// from hl360-vs-ReGPU frame-dump diffs.
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>

#include "regpu/regpu.h"

#include "kernel/runtime.h"
#include "kernel/thread.h"
#include "log.h"
#include "memory.h"
#include "settings.h"

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
    host.resolveWriteback = [](const regpu::ResolveCopy& c) {
        regpu::Writeback w;
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

    // Q24: guest vblank at 60 Hz.
    if (const char* hz = getenv("FM3_VBLANK_HZ"))
        _putenv_s("REGPU_VBLANK_HZ", hz);
    DefaultEnv("REGPU_VBLANK_HZ", "60");

    Log("gpu: ReGPU at %ux%u, msaa %u, vblank %s Hz", config.width, config.height, config.msaa, getenv("REGPU_VBLANK_HZ"));
    regpu::Start();
}
