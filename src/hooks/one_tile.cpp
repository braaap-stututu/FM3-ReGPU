// One guest tile (session 26, PERF-PLAN L36).
//
// FM3 draws its 720p 2x MSAA scene in two EDRAM tiles (rows 0-511, 512-719) because the console's 10 MB EDRAM
// can't hold it in one: the game records each frame once and the GPU replays it per tile (predicated tiling), so
// every draw that touches both halves runs twice (18% of the draw thread's draws, session 25). ReGPU's surfaces are
// host render targets of any size, so one 720-row tile is enough.
//
// The game's tiling: three static scenarios at 0x82BF60D0, 1232 bytes each ("1280x720 4xMSAA Horizontal Split",
// "2xMSAA Horizontal Split", "1xMSAA No Split"; the front end runs the first, races the second): +0 name, +4 width,
// +8 height, +12 MSAA type, +16 tile count, +32 tile rectangles (x1, y1, x2, y2). The renderer's tiling object
// (r3 of sub_82798890 / sub_82797C58) holds the scenario index at +0 and byte +90 = tiled. Its set-up, sub_82798890,
// takes the tiled path (tiled surfaces) only for a count above 1; every frame BeginTiling (sub_82797C58 ->
// sub_824BBA08), the per-tile resolves (sub_82797ED0), the per-draw tile masks (sub_8271CA40), the predication
// masks (sub_82850280) and the tiled command buffers (sub_82855870) read the count and rectangles again.
//
// So the set-up runs with the game's own counts, and right after it a tiled scenario gets count 1 with tile 0
// over all rows. Command buffers recorded with two tiles still carry their predicated per-tile window fixups,
// which all run outside a tiling pass: ReGPU merges them (REGPU_WINDOW_FIXUP_UNION, set in gpu_host.cpp).
// Tried first, and wrong: the scenario's count edited to 1 before the set-up takes the game's non-tiled path, and
// the game's own "one tile this frame" byte (+91) leaves the per-tile resolves at two tiles.
//
// Frozen grid (session 26): 720p 1x 290.0 -> 316.4 fps (+9.1%, 3 boots each), draws 3,288 -> 2,647; pictures within
// the cross-boot noise (the seam rows too). Off by default until driven: FM3_ONE_TILE=1 or settings.ini
// one_tile=1 (start time; logged "one tile: ...").
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "kernel/imports.h"
#include "log.h"
#include "settings.h"

namespace
{
constexpr uint32_t kScenarios = 0x82BF60D0, kScenarioBytes = 1232;

// The scenario's tile count as the game made it (0: not changed).
uint32_t g_count[3]{};

void Restore()
{
    for (uint32_t i = 0; i < 3; i++)
        if (g_count[i])
        {
            const uint32_t s = kScenarios + i * kScenarioBytes;
            const uint32_t split = g_count[i] == 3 ? 256 : 512; // (4x: rows 256 / 512; 2x: 512)
            StoreBE32(s + 16, g_count[i]);
            StoreBE32(s + 32 + 12, split); // tile 0's bottom
            g_count[i] = 0;
        }
}
}

bool OneTileEnabled()
{
    static const bool on = [] {
        const char* v = getenv("FM3_ONE_TILE");
        const std::string s = v ? std::string(v) : GetSettings().Extra("one_tile");
        return !s.empty() && s != "0";
    }();
    return on;
}

PPC_FUNC_IMPL(__imp__sub_82798890);
PPC_FUNC(sub_82798890)
{
    if (!OneTileEnabled())
        return __imp__sub_82798890(ctx, base);
    Restore();
    const uint32_t object = ctx.r3.u32;
    __imp__sub_82798890(ctx, base);
    const uint32_t index = LoadBE32(object);
    const uint32_t s = kScenarios + index * kScenarioBytes;
    if (index < 3 && *GuestPtr<uint8_t>(object + 90) && LoadBE32(s + 16) > 1 && LoadBE32(s + 4) == 1280 && LoadBE32(s + 8) == 720)
    {
        g_count[index] = LoadBE32(s + 16);
        StoreBE32(s + 16, 1);
        StoreBE32(s + 32 + 12, 720); // tile 0's bottom: all rows
        Log("one tile: scenario %u (%u tiles) drawn as one 1280x720 tile", index, g_count[index]);
    }
    else
        Log("one tile: scenario %u left as it is (tiled %u)", index, *GuestPtr<uint8_t>(object + 90));
}
