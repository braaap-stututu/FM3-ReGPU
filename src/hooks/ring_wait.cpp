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
#include <cstring>

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

// The app tick (session 47, low-end plan): the main thread (entry 822BDE68 -> sub_821E5380) runs
// `while (running) sub_82210098(app);` with no wait of its own: it renders and presents when the game loop has a new
// frame and otherwise calls the tick again at once. So between frames it polls, unless ReGPU's frame limiter (VdSwap)
// happens to hold it until the next frame is ready: at the 60 cap 8-78% of a core depending on the boot, 98% with the
// limiter out of the way (vblank-paced), same 60 fps and draws. Those polls were ~1.1 million empty ticks a second,
// each under 1 us (a frame's tick takes 1-5 ms).
// FM3_TICK_WAIT=<us> (live; default 20, 0 = off): after a tick shorter than that, sleep ~100 us (a poll that found
// nothing to do). Measured (iGPU, 8 threads, 60 fps unchanged): main thread 85% -> 9% of a core (process 1.5 -> 0.8
// cores) in a bad 60-cap boot, 98% -> 10% vblank-paced; RX 7900 XTX 1440p 4x frozen grid 348.1 vs 348.0 fps (4 ABBA).
// FM3_TICK_LOG=1 (live): ticks per second and how long they take, every 5 s.
namespace
{
int64_t QpcNow()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
double QpcFreq()
{
    static const double f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return double(q.QuadPart); }();
    return f;
}
int TickWaitUs()
{
    static std::atomic<int64_t> next{ 0 };
    static std::atomic<int> us{ -1 }; // (-1: not read yet, so the first read logs)
    const int64_t now = QpcNow();
    if (now >= next.load(std::memory_order_relaxed))
    {
        next.store(now + int64_t(QpcFreq() / 4), std::memory_order_relaxed);
        char v[16] = {};
        const int n = GetEnvironmentVariableA("FM3_TICK_WAIT", v, sizeof(v)) && v[0] ? atoi(v) : 20;
        if (n != us.exchange(n, std::memory_order_relaxed))
            Log("tick wait: %d us", n);
    }
    return us.load(std::memory_order_relaxed);
}
} // namespace

PPC_FUNC_IMPL(__imp__sub_82210098);
PPC_FUNC(sub_82210098)
{
    const int64_t t0 = QpcNow();
    __imp__sub_82210098(ctx, base);
    const int64_t t1 = QpcNow();
    const double us = double(t1 - t0) * 1e6 / QpcFreq();
    static uint64_t ticks = 0, slept = 0, buckets[5] = {};
    static double sumUs = 0;
    static int64_t since = 0;
    char v[4] = {};
    const bool log = GetEnvironmentVariableA("FM3_TICK_LOG", v, sizeof(v)) && v[0] == '1';
    const int waitUs = TickWaitUs();
    const bool sleep = waitUs > 0 && us < waitUs;
    if (sleep)
        ShortSleep();
    if (!log)
    {
        since = 0;
        return;
    }
    if (!since)
    {
        since = t1, ticks = slept = 0, sumUs = 0;
        memset(buckets, 0, sizeof(buckets));
    }
    ticks++, slept += sleep, sumUs += us;
    buckets[us < 20 ? 0 : us < 100 ? 1 : us < 1000 ? 2 : us < 5000 ? 3 : 4]++;
    if (const double s = double(t1 - since) / QpcFreq(); s >= 5.0)
    {
        Log("tick: %.0f ticks/s, mean %.1f us; <20 us %llu, <100 %llu, <1000 %llu, <5000 %llu, more %llu; slept after %llu",
            double(ticks) / s, sumUs / double(ticks), (unsigned long long)buckets[0], (unsigned long long)buckets[1],
            (unsigned long long)buckets[2], (unsigned long long)buckets[3], (unsigned long long)buckets[4], (unsigned long long)slept);
        since = 0;
    }
}

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
