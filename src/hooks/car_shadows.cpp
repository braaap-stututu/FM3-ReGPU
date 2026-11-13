// Car shadow map at a lower rate (session 32, user request).
//
// Where AI cars cost (session 32, frozen grid, FM3's developer option DontDrawCars=1 for no cars): each car is ~183
// draws a frame, ~0.19 ms of ReGPU's draw thread (1.02 us a draw) and ~0.15 ms of GPU at 4K. ~70 of a car's draws go
// into the car shadow maps (the frame's first passes; ~500-600 draws on the grid, ~20% of the draw thread a frame,
// almost nothing on the GPU).
//
// sub_827B5088 drives the car shadows once a frame, in two phases (r4 = 1, then 0; the object's +720 holds the phase,
// -1 after): for each phase sub_827B4D30 begins the pass (render target, clear), the drop-shadow effect's method
// sub_82592168 (virtual slot +20) draws the cars (through the car renderer sub_82837530, called from 82592444; its
// shadow loop draws from 828379A0), and sub_827B4E40 ends it (two D3D resolves, sub_824C3CF0). Phase 1 makes the cars'
// depth from the sun (256x256 depth copy), phase 0 the images the ground-shadow passes (pixel shaders fd6b3d2a...,
// 9e50ba14...: a 512x512 image filled in its lower half) and the car materials read.
//
// Skipping only the method keeps the begin/end: the passes are cleared and resolved without cars, so the shadows
// vanish (session 32: rate 2 blinked). Skipping the whole pass (begin, method, end) leaves no clear and no resolve, so
// ReGPU keeps last frame's images and the ground shadows show them: rendered every Nth frame, the shadows are up to
// N-1 frames old.
//
// settings.ini shadow_rate=N (launcher Settings > Testing "Half-rate car shadows" = 2): the car shadow passes run
// every Nth frame (1 = every frame, as the game does; default). FM3_SHADOW_RATE=N overrides it (read 4 times a second,
// so `Fm3 "env FM3_SHADOW_RATE 2"` works live).
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>

#include "kernel/imports.h"
#include "log.h"
#include "settings.h"

namespace
{
int Rate()
{
    static std::atomic<int> rate{ -1 };
    static std::atomic<int64_t> next{ 0 };
    const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (now >= next.load(std::memory_order_relaxed))
    {
        next.store(now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count(),
            std::memory_order_relaxed);
        char v[8] = {};
        static const int fromSettings = [] {
            const std::string s = GetSettings().Extra("shadow_rate");
            return s.empty() ? 1 : atoi(s.c_str());
        }();
        int r = GetEnvironmentVariableA("FM3_SHADOW_RATE", v, sizeof(v)) && v[0] ? atoi(v) : fromSettings;
        r = r < 1 ? 1 : r > 1000 ? 1000 : r;
        if (r != rate.exchange(r, std::memory_order_relaxed))
            Log("car shadows: shadow passes run every %d frame%s", r, r == 1 ? " (as the game does)" : "s");
    }
    return rate.load(std::memory_order_relaxed);
}

bool g_skipPasses = false; // this frame's shadow passes are skipped (render thread only)
} // namespace

// The driver: decides for the frame.
PPC_FUNC_IMPL(__imp__sub_827B5088);
PPC_FUNC(sub_827B5088)
{
    static uint64_t frames = 0;
    const int rate = Rate();
    g_skipPasses = rate > 1 && frames++ % uint64_t(rate) != 0;
    __imp__sub_827B5088(ctx, base);
    g_skipPasses = false;
}

// Begin a shadow pass (render target, clear).
PPC_FUNC_IMPL(__imp__sub_827B4D30);
PPC_FUNC(sub_827B4D30)
{
    if (g_skipPasses)
        return;
    __imp__sub_827B4D30(ctx, base);
}

// The drop-shadow effect's method: draws the cars into the pass.
PPC_FUNC_IMPL(__imp__sub_82592168);
PPC_FUNC(sub_82592168)
{
    if (g_skipPasses)
        return;
    __imp__sub_82592168(ctx, base);
}

// End a shadow pass (resolves).
PPC_FUNC_IMPL(__imp__sub_827B4E40);
PPC_FUNC(sub_827B4E40)
{
    if (g_skipPasses)
        return;
    __imp__sub_827B4E40(ctx, base);
}
