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

// Experiment (session 27, user request: what can the mirror leave out?). Session 32: FM3_SCENE_SKIP=<hex>[,...] skips
// the same steps/call sites outside the mirror too (main view; e.g. 8283A8D0 = the opaque cars, 8283AB40 = their glass
// and lights, 828379A0 = the cars in the shadow map). While the mirror renders, the scene
// renderer sub_8283A220's steps can be skipped one call site at a time: FM3_MIRROR_SKIP=<hex>[,<hex>...] lists
// return addresses (the call site + 4, as ctx.lr holds them) or function addresses; FM3_MIRROR_TRACE=1 logs each
// distinct (function, call site) the mirror reaches once. Read every 250 ms with the rate, so both work live.
namespace
{
bool g_inMirror = false; // render thread only (sub_827FDB88 and everything below it run there)
uint32_t g_skip[32];
int g_skipCount = 0;
bool g_trace = false;
uint32_t g_sceneSkip[32]; // FM3_SCENE_SKIP (session 32 experiment): the same steps skipped outside the mirror too
int g_sceneSkipCount = 0;
int g_carLod = 0; // FM3_MIRROR_CAR_LOD (below)

void ReadSkips()
{
    static int64_t next = 0;
    const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (now < next)
        return;
    next = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count();
    char v[512] = {};
    GetEnvironmentVariableA("FM3_MIRROR_SKIP", v, sizeof(v));
    static std::string last;
    if (last != v)
    {
        last = v;
        g_skipCount = 0;
        for (const char* p = v; *p && g_skipCount < 32;)
        {
            char* end = nullptr;
            const unsigned long a = strtoul(p, &end, 16);
            if (end == p)
                break;
            if (a)
                g_skip[g_skipCount++] = uint32_t(a);
            p = *end ? end + 1 : end;
        }
        Log("mirror: skipping %d step(s) in the mirror: %s", g_skipCount, v[0] ? v : "(none)");
    }
    char s[512] = {};
    GetEnvironmentVariableA("FM3_SCENE_SKIP", s, sizeof(s));
    static std::string lastScene;
    if (lastScene != s)
    {
        lastScene = s;
        g_sceneSkipCount = 0;
        for (const char* p = s; *p && g_sceneSkipCount < 32;)
        {
            char* end = nullptr;
            const unsigned long a = strtoul(p, &end, 16);
            if (end == p)
                break;
            if (a)
                g_sceneSkip[g_sceneSkipCount++] = uint32_t(a);
            p = *end ? end + 1 : end;
        }
        Log("scene: skipping %d step(s) outside the mirror: %s", g_sceneSkipCount, s[0] ? s : "(none)");
    }
    char t[4] = {};
    g_trace = GetEnvironmentVariableA("FM3_MIRROR_TRACE", t, sizeof(t)) && t[0] == '1';
    char l[4] = {};
    const int lod = GetEnvironmentVariableA("FM3_MIRROR_CAR_LOD", l, sizeof(l)) && l[0] ? atoi(l) : 0;
    if (lod != g_carLod)
    {
        g_carLod = lod;
        Log("mirror: cars drawn at detail level %d%s", lod, lod ? "" : " (as the game does: full detail)");
    }
}

bool SkipInMirror(uint32_t function, uint32_t lr)
{
    if (!g_inMirror)
    {
        ReadSkips();
        for (int i = 0; i < g_sceneSkipCount; i++)
            if (g_sceneSkip[i] == lr || g_sceneSkip[i] == function)
                return true;
        return false;
    }
    if (g_trace)
    {
        static uint64_t seen[256];
        static int seenCount = 0;
        const uint64_t key = uint64_t(function) << 32 | lr;
        bool found = false;
        for (int i = 0; i < seenCount && !found; i++)
            found = seen[i] == key;
        if (!found && seenCount < 256)
        {
            seen[seenCount++] = key;
            Log("mirror: step sub_%08X from %08X", function, lr);
        }
    }
    for (int i = 0; i < g_skipCount; i++)
        if (g_skip[i] == lr || g_skip[i] == function)
            return true;
    return false;
}
} // namespace

#define FM3_MIRROR_STEP(addr)                                     \
    PPC_FUNC_IMPL(__imp__sub_##addr);                             \
    PPC_FUNC(sub_##addr)                                          \
    {                                                             \
        if (SkipInMirror(0x##addr, uint32_t(ctx.lr)))             \
        {                                                         \
            ctx.r3.u64 = 0;                                       \
            return;                                               \
        }                                                         \
        __imp__sub_##addr(ctx, base);                             \
    }
// The scene renderer sub_8283A220's steps (its direct calls, apart from small helpers).
FM3_MIRROR_STEP(828374C8)
FM3_MIRROR_STEP(82855790)
FM3_MIRROR_STEP(82839508)
FM3_MIRROR_STEP(828363C0)
FM3_MIRROR_STEP(82837530)
FM3_MIRROR_STEP(8284A370)
FM3_MIRROR_STEP(828342D8)
FM3_MIRROR_STEP(82834380)
FM3_MIRROR_STEP(82838518)
FM3_MIRROR_STEP(828339F0)
FM3_MIRROR_STEP(82833A78)
FM3_MIRROR_STEP(8283A128)
FM3_MIRROR_STEP(82835E18)

// Car detail in the mirror (session 27). sub_82837530 draws the cars; per car it calls sub_828329C8 with r10 = an
// object whose +596 holds the car's detail level. The main view ranks the cars and sets +596 = 0 for the nearest
// 1-2, 1 up to rank 3-4, 2 beyond (code at 82837E50..82837F08), then resets it to 0. The mirror's loop (82838044..
// 828380BC) never sets it, so every car in the mirror is drawn at level 0, the full model (Catalunya grid, 2nd, six
// cars behind: the cars are 668 of the mirror's ~1,316 draws, and skipping them = +40% fps).
// FM3_MIRROR_CAR_LOD=N (live): the mirror draws its cars at level N (0 = as the game does).
PPC_FUNC_IMPL(__imp__sub_828329C8);
PPC_FUNC(sub_828329C8)
{
    if (SkipInMirror(0x828329C8, uint32_t(ctx.lr)))
    {
        ctx.r3.u64 = 0;
        return;
    }
    if (g_inMirror && g_carLod > 0 && uint32_t(ctx.lr) == 0x828380BC)
    {
        const uint32_t field = ctx.r10.u32 + 596;
        const uint32_t old = LoadBE32(field);
        StoreBE32(field, uint32_t(g_carLod));
        __imp__sub_828329C8(ctx, base);
        StoreBE32(field, old);
        return;
    }
    __imp__sub_828329C8(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_827FDB88);
PPC_FUNC(sub_827FDB88)
{
    static uint64_t calls = 0;
    const int rate = Rate();
    if (rate > 1 && calls++ % uint64_t(rate) != 0)
        return; // (the mirror keeps last frame's picture)
    ReadSkips();
    g_inMirror = true;
    __imp__sub_827FDB88(ctx, base);
    g_inMirror = false;
}
