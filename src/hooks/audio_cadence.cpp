// Engine sound updates at high frame rates (session 44; FM2 ReGPU port's fix a7aea015b, re-found in FM3).
//
// FM3's audio runs like FM2's: every main-loop iteration the game records per-frame audio updates as commands into a
// packet that its audio thread (CAudioThread, loop body sub_821E9390) replays from its queue (CAudioThread + 0x18).
// Commands recorded with "droppable" (sub_821FB200's r8 = 0, the command's byte +16 = 0) are skipped when the audio
// thread replays in catch-up mode: when it finds more packets waiting than it replayed last time + 2, it calls the
// replay sub_821FC878(queue, 1) instead of (queue, 0). It sleeps 5 ms when idle, so at 60 fps that is rare; at a
// 360 cap the main loop runs several times per audio-thread wake and most per-frame updates are dropped.
// The per-frame droppable updates (vtable slot, 0-based):
//   CCarAudioDeferred slot 7      sub_821F8308  (this, dt f1, 528-byte car snapshot by value: rpm, throttle, ...)
//   CAudioManagerDeferred slot 7  sub_8222C120  (this, dt f1, "a game mode is running" byte r5)
//   CAudioManagerDeferred slot 9  sub_821E9520  (this, 224-byte listener/emitter snapshot by value; no dt)
// The car's engine model then sees only part of the time that passed (its rpm moves in jumps, drivetrain spring on
// the wrong clock): FM2 heard it as warble while revving.
//
// Fixes (each live):
// - The audio thread never skips them: its catch-up replays run them too (FM3_AUDIO_CATCHUP_DROP=1: the game's way).
// - The main thread sends the two dt updates about FM3_AUDIO_UPDATE_HZ times a second at most (default 120, what a
//   60 fps game gives; 0 = every frame), with the dt of the calls held back added up, so the audio thread does about
//   the work it does at 60 fps. A call with dt <= 0 (pause pushes its state with one) always goes through.
// FM3_TRACE_AUDIO_CADENCE=1 logs every 5 s: updates sent/held, catch-up replays, car updates the audio thread ran.
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <unordered_map>

#include "kernel/imports.h"
#include "live_switch.h"
#include "log.h"

namespace
{
LiveSwitch g_catchupDrop("FM3_AUDIO_CATCHUP_DROP");
LiveSwitch g_trace("FM3_TRACE_AUDIO_CADENCE");

std::atomic<uint32_t> g_audioQueue{ 0 };
std::atomic<uint32_t> g_carSent{ 0 }, g_carHeld{ 0 }, g_carRun{ 0 }, g_catchups{ 0 }, g_replays{ 0 };

double Seconds()
{
    static const double hz = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return double(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart) / hz;
}

// FM3_AUDIO_UPDATE_HZ, read twice a second (live).
double Period()
{
    static std::atomic<double> period{ -1.0 };
    static std::atomic<double> next{ 0.0 };
    const double now = Seconds();
    if (now >= next.load(std::memory_order_relaxed))
    {
        next.store(now + 0.5, std::memory_order_relaxed);
        char v[16] = {};
        const double hz = GetEnvironmentVariableA("FM3_AUDIO_UPDATE_HZ", v, sizeof(v)) && v[0] ? atof(v) : 120.0;
        const double p = hz > 0 ? 1.0 / hz : 0.0;
        if (p != period.exchange(p, std::memory_order_relaxed))
        {
            if (hz > 0)
                Log("audio cadence: game audio updates at most about %d a second", int(hz));
            else
                Log("audio cadence: game audio updates every frame (as the game does)");
        }
    }
    return period.load(std::memory_order_relaxed);
}

void TraceTick()
{
    static double next = 0;
    const double now = Seconds();
    if (!g_trace)
        return;
    if (next == 0)
        next = now + 5;
    if (now < next)
        return;
    next = now + 5;
    Log("audio cadence: 5 s: car updates sent %u, held back %u, run by the audio thread %u; audio replays %u, "
        "%u in catch-up mode (drop %s)",
        g_carSent.exchange(0), g_carHeld.exchange(0), g_carRun.exchange(0), g_replays.exchange(0), g_catchups.exchange(0),
        g_catchupDrop ? "on" : "off");
}

struct Pending
{
    double dt = 0;   // the game's dt of the calls held back
    double last = 0; // when the last call went through
};

// Per deferred object (main thread only). False while the call should be held back; true with dt replaced by the
// game's dt summed since the last call that went through. Wall-clock cadence: a call goes through once 3/4 of the
// period has passed, so a game at the update rate or slower has every call go through.
bool Coalesce(std::unordered_map<uint32_t, Pending>& pending, uint32_t object, double& dt)
{
    const double period = Period();
    Pending& p = pending[object];
    if (period <= 0)
    {
        dt += p.dt;
        p.dt = 0;
        return true;
    }
    const double now = Seconds();
    p.dt += dt;
    if (dt > 0 && p.last != 0 && now - p.last < 0.75 * period)
        return false;
    dt = p.dt;
    p.dt = 0;
    p.last = now;
    return true;
}
} // namespace

// CCarAudioDeferred slot 7 (this, dt in f1, car snapshot by value).
PPC_FUNC_IMPL(__imp__sub_821F8308);
PPC_FUNC(sub_821F8308)
{
    static std::unordered_map<uint32_t, Pending> pending;
    TraceTick();
    double dt = ctx.f1.f64;
    if (!Coalesce(pending, ctx.r3.u32, dt))
    {
        g_carHeld.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_carSent.fetch_add(1, std::memory_order_relaxed);
    ctx.f1.f64 = dt;
    __imp__sub_821F8308(ctx, base);
}

// CAudioManagerDeferred slot 7 (this, dt in f1, "a game mode is running" in r5: a level, applied every update).
PPC_FUNC_IMPL(__imp__sub_8222C120);
PPC_FUNC(sub_8222C120)
{
    static std::unordered_map<uint32_t, Pending> pending;
    double dt = ctx.f1.f64;
    if (!Coalesce(pending, ctx.r3.u32, dt))
        return;
    ctx.f1.f64 = dt;
    __imp__sub_8222C120(ctx, base);
}

// The car update's command as the audio thread replays it (calls CCarAudio slot 7).
PPC_FUNC_IMPL(__imp__sub_828E6B10);
PPC_FUNC(sub_828E6B10)
{
    g_carRun.fetch_add(1, std::memory_order_relaxed);
    __imp__sub_828E6B10(ctx, base);
}

// The audio thread's loop body (CAudioThread in r3; its command queue at +0x18).
PPC_FUNC_IMPL(__imp__sub_821E9390);
PPC_FUNC(sub_821E9390)
{
    g_audioQueue.store(ctx.r3.u32 + 0x18, std::memory_order_relaxed);
    __imp__sub_821E9390(ctx, base);
}

// Replays the packet at the head of a queue (the audio thread's and the render side's); r4 = 1 (catch-up) skips the
// droppable commands. Only the audio thread's queue is changed.
PPC_FUNC_IMPL(__imp__sub_821FC878);
PPC_FUNC(sub_821FC878)
{
    if (ctx.r3.u32 == g_audioQueue.load(std::memory_order_relaxed))
    {
        g_replays.fetch_add(1, std::memory_order_relaxed);
        if (ctx.r4.u32 & 0xFF)
        {
            g_catchups.fetch_add(1, std::memory_order_relaxed);
            if (!g_catchupDrop)
                ctx.r4.u64 = 0;
        }
    }
    __imp__sub_821FC878(ctx, base);
}
