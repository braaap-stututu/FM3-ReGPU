// Front-end frame rate (session 40, user report: "the front end is sped up by the times the frame cap is faster
// than 60"; then "menu speed is half the cap").
//
// FM3's menu animations (the front end's sliding menus and transitions) advance per rendered frame by at least
// 1/120 s, whatever the frame time: measured on the Free Play submenu opening (10 keyframe steps, consecutive debug
// shots), 19 frames at a 60 cap (0.32 s, console speed), ~40 frames at 120, 240 and 360 caps (0.33 / 0.17 / 0.11 s:
// cap/120 times too fast above 120). It is neither the main loop's frame time (dt frozen at 0, or 8x: unchanged), nor
// its UI update (sub_8231A838 skipped: the menu still animated), nor the vblank count (FM3_VBLANK_DIVIDER=1:
// unchanged); with presentation capped at 120 while the main loop ran at 240 it took 35 frames at 120 fps, console
// speed. The clamp itself (somewhere on the render thread, sub_82210098) was not found.
//
// So the front end presents at most 120 fps (min of the cap and 120): regpu::SetFrameLimit when the renderer is set up
// for scenario 0 (the front end; photo mode too), the full cap again for scenario 1 (races). The main loop keeps its
// rate (the guest vblank stays at twice the cap). The race's own menus (pre-race, pause) still run at the race's cap.
// FM3_FRONTEND_FPS=<n> or settings.ini frontend_fps=<n> sets the front end's limit (0 = the full cap, as before).
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <regpu/regpu.h>

#include "log.h"
#include "settings.h"

namespace
{
uint32_t FrontEndLimit()
{
    static const uint32_t limit = [] {
        const char* v = getenv("FM3_FRONTEND_FPS");
        const std::string s = v ? std::string(v) : GetSettings().Extra("frontend_fps");
        return s.empty() ? 120u : uint32_t(std::max(0, atoi(s.c_str())));
    }();
    return limit;
}
}

void FrontEndRateScenario(uint32_t index)
{
    // (Our settings keep the player's cap; regpu::SetFrameLimit changes ReGPU's copy only.)
    const uint32_t userCap = GetSettings().maxFps, limit = FrontEndLimit();
    if (!limit || limit >= userCap)
        return;
    static uint32_t applied = userCap;
    const uint32_t cap = index == 0 ? std::max(limit, 30u) : userCap;
    if (cap == applied)
        return;
    applied = cap;
    regpu::SetFrameLimit(cap, GetSettings().vsync);
    Log("front end: frame limit %u fps (%s; cap %u)", cap, index == 0 ? "front end, menus animate per frame" : "race", userCap);
}
