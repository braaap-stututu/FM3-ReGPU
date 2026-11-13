// Car reflections updated less often (session 32, PERF-PLAN V8, user request).
//
// FM3 keeps a dynamic cube map of the surroundings for the cars' reflections (every car material reads it; six
// 256x256 faces drawn with 4x MSAA, three a frame, alternating between two sets of three). sub_827FF238 renders a
// frame's three faces: its loop (827FF368..827FF4FC) sets each face up, draws the scene into it through the scene
// renderer sub_8283A220 (called from 827FF4A0; the main view calls it from 821FC964, the mirror from 827FDEB4) and
// resolves it. Skipping only the scene draws left the faces cleared and resolved empty (2.9% of the picture changed);
// skipping the whole function leaves no clear and no resolve, so ReGPU keeps the faces' last images.
//
// settings.ini reflect_rate=N (launcher Settings > Testing "Half-rate car reflections" = 2): the reflection faces
// render every Nth time (1 = every frame, as the game does; default). FM3_REFLECT_RATE=N overrides it (read 4 times
// a second, so `Fm3 "env FM3_REFLECT_RATE 2"` works live). The face sets alternate inside the function, so skipping
// whole calls still updates all six faces (session 32 resolve trace: rate 2 = each face every 4th frame).
// FM3_REFLECT_GROUP=G (default 1) runs G calls in a row before skipping (experiment).
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>

#include "kernel/imports.h"
#include "log.h"
#include "settings.h"

namespace
{
std::atomic<int> g_group{ 1 };

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
            const std::string s = GetSettings().Extra("reflect_rate");
            return s.empty() ? 1 : atoi(s.c_str());
        }();
        int r = GetEnvironmentVariableA("FM3_REFLECT_RATE", v, sizeof(v)) && v[0] ? atoi(v) : fromSettings;
        r = r < 1 ? 1 : r > 1000 ? 1000 : r;
        char g[8] = {};
        const int group = GetEnvironmentVariableA("FM3_REFLECT_GROUP", g, sizeof(g)) && g[0] ? std::max(1, atoi(g)) : 1;
        g_group.store(group, std::memory_order_relaxed);
        if (r != rate.exchange(r, std::memory_order_relaxed))
            Log("car reflections: faces rendered every %d time%s", r, r == 1 ? " (as the game does)" : "s");
    }
    return rate.load(std::memory_order_relaxed);
}
} // namespace

PPC_FUNC_IMPL(__imp__sub_827FF238);
PPC_FUNC(sub_827FF238)
{
    static uint64_t calls = 0;
    const int rate = Rate();
    const uint64_t group = uint64_t(g_group.load(std::memory_order_relaxed));
    const uint64_t c = calls++;
    if (rate > 1 && (c / group) % uint64_t(rate) != 0)
        return; // (the faces keep their last images)
    __imp__sub_827FF238(ctx, base);
}
