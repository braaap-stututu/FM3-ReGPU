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

    // Q24: guest vblank at 60 Hz.
    if (const char* hz = getenv("FM3_VBLANK_HZ"))
        _putenv_s("REGPU_VBLANK_HZ", hz);
    DefaultEnv("REGPU_VBLANK_HZ", "60");

    Log("gpu: ReGPU at %ux%u, msaa %u, vblank %s Hz", config.width, config.height, config.msaa, getenv("REGPU_VBLANK_HZ"));
    regpu::Start();
}
