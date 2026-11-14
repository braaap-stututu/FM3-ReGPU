// Test grid (session 23): reach the reference career race (CrownJewel = Ladera Test Track, Short Circuit
// Reverse, 8th in the Scion xD, the Baseline profile's next Season Play event) and stop its world on the start
// grid by ourselves, for image A/Bs without the pause menu (frame_time.cpp: FM3_FREEZE_TIME / FrameTimeFreeze).
//
// grid_test=1 (settings.ini, launcher "Start on the test grid", FM3_GRID_TEST=1; implies skip_intro):
// 1. Press A about three times a second from boot (what the user does by hand: title > Forza Motorsport 3 >
//    Go Race > Season Play > calendar > event Start Race > track load > Pre-Race Start Race; every step is the
//    first item, and a press on a menu that hid itself after ~5 s idle only wakes it).
// 2. Stop pressing once the race-start sequence runs (sub_822E6AD0's flag +25 set: Start Race taken).
// 3. Freeze the world when the sequence's clock (double at +16) reaches FM3_GRID_FREEZE_AT (default 9.0 s) with
//    the intro running (flags +24/+25/+26 = 0/1/0). Measured in session 23 (log clock vs a video of the
//    countdown): the clock restarts at 0 when the car intro starts, "3" shows at ~6.7, "2" at ~7.7, "1" with the
//    chase camera settled at ~8.6, GO at 9.5 (the clock stops there and flag +26 is set). 9.0 is mid-"1": the
//    chase view, the AI cars on their grid spots, nothing moving yet.
// Then `Fm3 "env FM3_FREEZE_TIME 0"` lets the race go on (and `... 1` stops it again anywhere).
//
// Benchmark race (session 33): FM3_BENCHMARK=<seconds> (the launcher's Benchmark button, tools/benchmark.ps1 -Self)
// takes the same route without the freeze; the race AI drives the player's car (hooks/ai_player.cpp, FM3_AI_PLAYER set
// in gpu_host.cpp with the light performance log). At GO (flag +26) a thread waits FM3_BENCHMARK_SETTLE seconds
// (default 10), logs "benchmark: measuring N s", waits N seconds, logs "benchmark: done" and closes the game's window
// (as the user would). The launcher (Benchmark.cs) / tools/analysis/bench.py read the perf lite lines in between.
//
// sub_822E6AD0 (this r3, dt f1) is the race-start sequence's update; its class reads the "321Tuning" settings
// at 0x82C37520..0x82C37570 (loaded by sub_822E77D0 with HumanRevScript/AIRevScript/PreRaceCamsFadeOut). Phase
// byte +4 (3 in a career race), flags +24..+26, the sequence clock at +16. Each flag change is logged.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hooks/frame_time.h"
#include "hooks/grid_test.h"
#include "kernel/imports.h"
#include "log.h"
#include "settings.h"

namespace
{
enum class Route : int { Off, Pressing, Waiting, Frozen, Racing };

int BenchmarkSeconds()
{
    static const int s = getenv("FM3_BENCHMARK") ? std::max(0, atoi(getenv("FM3_BENCHMARK"))) : 0;
    return s;
}

BOOL CALLBACK CloseOurWindow(HWND hwnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr)
        PostMessageA(hwnd, WM_CLOSE, 0, 0); // what Process.CloseMainWindow (and the user's close button) does
    return TRUE;
}

void StartBenchmarkClock()
{
    std::thread([] {
        const char* s = getenv("FM3_BENCHMARK_SETTLE");
        const int settle = s ? std::max(0, atoi(s)) : 10;
        Sleep(DWORD(settle) * 1000);
        Log("benchmark: measuring %d s (from %d s after GO)", BenchmarkSeconds(), settle);
        Sleep(DWORD(BenchmarkSeconds()) * 1000);
        Log("benchmark: done");
        Sleep(1500); // (one more perf lite line, and the log written)
        EnumWindows(CloseOurWindow, 0);
    }).detach();
}
std::atomic<Route> g_route{ Route::Off };

double FreezeAt()
{
    static const double at = getenv("FM3_GRID_FREEZE_AT") ? atof(getenv("FM3_GRID_FREEZE_AT")) : 9.0;
    return at;
}

double LoadBEDouble(uint32_t address)
{
    const uint64_t bits = uint64_t(LoadBE32(address)) << 32 | LoadBE32(address + 4);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}
} // namespace

bool GridTestPressA()
{
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    static bool armed = [] {
        if (!GetSettings().gridTest)
            return false;
        g_route = Route::Pressing;
        if (BenchmarkSeconds())
            Log("grid test: benchmark race: pressing A until the race starts; the AI drives, %d s measured", BenchmarkSeconds());
        else
            Log("grid test: on: pressing A until the race starts, then freezing at %.2f s of its countdown clock", FreezeAt());
        return true;
    }();
    if (!armed || g_route.load(std::memory_order_relaxed) != Route::Pressing)
        return false;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    if (ms > 180000)
    {
        g_route = Route::Off;
        Log("grid test: no race start after 3 minutes: presses stopped");
        return false;
    }
    return ms % 300 < 100; // 100 ms down, 200 ms up
}

PPC_FUNC_IMPL(__imp__sub_822E6AD0);
PPC_FUNC(sub_822E6AD0)
{
    const uint32_t self = ctx.r3.u32;
    __imp__sub_822E6AD0(ctx, base);
    const uint8_t f24 = *GuestPtr<uint8_t>(self + 24), f25 = *GuestPtr<uint8_t>(self + 25), f26 = *GuestPtr<uint8_t>(self + 26);
    const double clock = LoadBEDouble(self + 16);
    static uint32_t lastKey = 0xFFFFFFFF;
    const uint32_t key = uint32_t(*GuestPtr<uint8_t>(self + 4)) << 24 | uint32_t(f24) << 16 | uint32_t(f25) << 8 | f26;
    if (key != lastKey)
    {
        lastKey = key;
        Log("grid test: race start %08X phase %u flags %u %u %u, clock %.3f s (main frame %llu)", self, key >> 24, f24, f25, f26, clock,
            (unsigned long long)FrameTimeMainFrames());
    }
    Route route = g_route.load(std::memory_order_relaxed);
    if (route == Route::Pressing && f25)
    {
        g_route = route = Route::Waiting;
        Log("grid test: race start under way: presses stopped");
    }
    if (route == Route::Waiting && BenchmarkSeconds())
    {
        if (f26) // GO
        {
            g_route = Route::Racing;
            Log("grid test: benchmark race: GO at %.3f s of the countdown clock", clock);
            StartBenchmarkClock();
        }
        return;
    }
    if (route == Route::Waiting && !f24 && f25 && !f26 && clock >= FreezeAt())
    {
        g_route = Route::Frozen;
        FrameTimeFreeze(true);
        SetEnvironmentVariableA("FM3_FREEZE_TIME", "1"); // so `Fm3 "env FM3_FREEZE_TIME 0"` is a change that lets it go
        Log("grid test: frozen on the grid at %.3f s of the countdown clock (main frame %llu)", clock,
            (unsigned long long)FrameTimeMainFrames());
    }
}
