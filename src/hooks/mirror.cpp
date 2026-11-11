// Rear-view mirror at a lower rate (session 26, user request).
//
// Cockpit view cost ~12% fps while driving (Catalunya, 1440p 4x: chase ~243 fps / ~2,630 draws, cockpit ~215 /
// ~2,950): the mirror renders the scene behind the car a second time (a 384x128 target, ~150-300 opaque world draws
// with the main view's shaders), and the draw thread limits at that setting.
//
// sub_827FDB88 renders the mirror's scene: it runs once a frame in cockpit view only (render-thread profile, chase vs
// cockpit: sub_821FC878 -> sub_82839A78 -> sub_827FDB88 -> sub_8283A220, the scene draw the main view uses too), and
// skipping it removes the mirror pass (~141 draws on the grid). The mirror texture keeps its last picture when a
// render is skipped (frozen grid, never rendered: 0 pixels different), so rendering it every Nth frame shows the
// previous mirror picture in between, with no gaps. (sub_8270C3B0, the "RenderMirror" effect method, runs in every
// view and changed 0 pixels when skipped: not the scene render.)
//
// settings.ini mirror_rate=N (launcher Settings > Testing "Half-rate mirror" = 2): the mirror renders every Nth
// frame (1 = every frame, as the game does; default). FM3_MIRROR_RATE=N overrides it (read 4 times a second, so
// `Fm3 "env FM3_MIRROR_RATE 2"` works live). Frozen grid, cockpit, live 5/5 rounds: 2 = +1.9% (253.9 -> 258.8 fps),
// never = +4.7%, pictures 0 pixels different (the grid's mirror shows empty track; driving it holds ~2x the draws).
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
            const std::string s = GetSettings().Extra("mirror_rate");
            return s.empty() ? 1 : atoi(s.c_str());
        }();
        int r = GetEnvironmentVariableA("FM3_MIRROR_RATE", v, sizeof(v)) && v[0] ? atoi(v) : fromSettings;
        r = r < 1 ? 1 : r > 8 ? 8 : r;
        if (r != rate.exchange(r, std::memory_order_relaxed))
            Log("mirror: rendered every %d frame%s", r, r == 1 ? " (as the game does)" : "s");
    }
    return rate.load(std::memory_order_relaxed);
}
}

PPC_FUNC_IMPL(__imp__sub_827FDB88);
PPC_FUNC(sub_827FDB88)
{
    static uint64_t calls = 0;
    const int rate = Rate();
    if (rate > 1 && calls++ % uint64_t(rate) != 0)
        return; // (the mirror keeps last frame's picture)
    __imp__sub_827FDB88(ctx, base);
}
