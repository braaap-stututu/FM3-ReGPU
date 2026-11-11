// Ring wait without spinning (session 23, PERF-PLAN L11).
//
// When ReGPU is behind (GPU-bound), the game's render thread t7 waits for ring-buffer space in D3D's wait loop
// (sub_824B5048 -> sub_824AAA68), which spins: one call of sub_824AAA68 is ~32 db16cyc delays and a check, and
// answers 1 to go on waiting. Session 20's profile: ~65% of t7 in there, which reads as a busy core (power on
// laptops, a core taken from the command processor and draw thread on 4-core CPUs).
//
// FM3_RING_WAIT (read 4 times a second, so `Fm3 "env FM3_RING_WAIT 0"` flips it live):
//   0          as the game does it (spin)
//   1          SwitchToThread() after each round that goes on waiting (lets another ready thread run)
//   2 / unset  sleep ~100 us on a high-resolution waitable timer after each such round (gives the core back)
// The answer (r3) and everything the round does are the game's; only the time between rounds changes.
// Default 2 since session 23 (PERF-RESULTS.md): on the frozen test grid, 1440p 4x 220.5 -> 220.1 fps and 720p 1x
// 231.0 -> 230.7 (both noise), the game's CPU 3.39 -> 2.73 cores (-19%) / 3.40 -> 2.81 (-17%), frame times the same
// (avg 4.53 ms, p99 5.36-5.50 vs 5.36-5.41, nothing over 20 ms), pictures identical. Mode 1 gained nothing.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>

#include "kernel/imports.h"
#include "log.h"

namespace
{
int Mode()
{
    static std::atomic<int> mode{ -1 }; // (-1: not read yet, so the first read logs)
    static std::atomic<int64_t> next{ 0 };
    const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (now >= next.load(std::memory_order_relaxed))
    {
        next.store(now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count(),
            std::memory_order_relaxed);
        char v[8] = {};
        const int m = GetEnvironmentVariableA("FM3_RING_WAIT", v, sizeof(v)) && v[0] ? atoi(v) : 2;
        if (m != mode.exchange(m, std::memory_order_relaxed))
            Log("ring wait: mode %d (%s)", m, m == 1 ? "yield" : m == 2 ? "sleep 100 us" : "spin, as the game does");
    }
    return mode.load(std::memory_order_relaxed);
}

void ShortSleep()
{
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer)
    {
        SwitchToThread();
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -1000; // 100 us, relative, in 100 ns units
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject(timer, 1);
}
} // namespace

PPC_FUNC_IMPL(__imp__sub_824AAA68);
PPC_FUNC(sub_824AAA68)
{
    __imp__sub_824AAA68(ctx, base);
    if (ctx.r3.u32 != 1)
        return;
    switch (Mode())
    {
    case 1:
        SwitchToThread();
        break;
    case 2:
        ShortSleep();
        break;
    default:
        break;
    }
}
