// Stage B (2026-11-27, user request: "boot the game and load into races without the game's original render killing
// the frame rate; a switch for now"): while a race runs, the game leaves out the drawing the Godot viewer does
// (..\Godot\frame-ir, the live mirror), so the GPU and the draw thread are free for Godot.
//
// What is left out in a race (renderer scenario 1; the front end, scenario 0, draws as before):
//   - the scene renderer sub_8283A220's world steps (mirror.cpp's FM3_MIRROR_STEP list, all but the cars' loop
//     sub_82837530): the track, its objects, sky, crowd, ...;
//   - each car's draw sub_828329C8, after the live stream has taken the car's matrix and the drawn camera from the main
//     view's call (82837F0C: LiveCarDrawn / DataOutCarDrawn still run, so the viewer and Data Out keep their data);
//   - the rear-view mirror's scene (sub_827FDB88), the car reflection cube's faces (sub_827FF238) and the car shadow
//     passes (sub_827B5088's passes).
// The game's HUD, menus over the race (pre-race, pause), post-processing and the race logic run as before; the game's
// window shows the HUD over an undrawn scene. Physics, AI, input and audio are untouched.
//
// The race also runs at its own frame cap: stage_b_fps (default 120; 0 = the player's cap), as the Godot viewer
// interpolates between the game's frames and every game frame still costs the CPU its simulation and command
// building.
//
// Switches: settings.ini stage_b=1 (launcher: not yet), FM3_STAGE_B=1 / 0 (environment, read 4 times a second, so
// `Fm3 "env FM3_STAGE_B 1"` works live), FM3_STAGE_B_FPS=<n> / stage_b_fps=<n>.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <regpu/regpu.h>

#include "hooks/live_camera.h"
#include "log.h"
#include "settings.h"

bool LiveRealTrackLoaded(); // live_camera.cpp: the last scene loaded is a real track, not a menu's
void FrontEndRateRefresh(); // frontend_rate.cpp: the frame cap for the scenario (front end, race, stage B race)

namespace
{
std::atomic<uint32_t> g_scenario{ 0xFFFFFFFFu }; // the renderer's scenario (0 front end, 1 race), from one_tile.cpp
std::atomic<bool> g_enabled{ false };
std::atomic<bool> g_active{ false };

uint32_t StageBFps()
{
    char v[16] = {};
    static const std::string fromSettings = GetSettings().Extra("stage_b_fps");
    const std::string s = GetEnvironmentVariableA("FM3_STAGE_B_FPS", v, sizeof(v)) && v[0] ? std::string(v) : fromSettings;
    return s.empty() ? 120u : uint32_t(std::max(0, atoi(s.c_str())));
}

void Update(bool now_ = false)
{
    static int64_t next = 0;
    const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (now < next && !now_)
        return;
    next = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count();
    static const bool fromSettings = GetSettings().Extra("stage_b") == "1";
    char v[8] = {};
    const bool on = GetEnvironmentVariableA("FM3_STAGE_B", v, sizeof(v)) ? (v[0] && v[0] != '0') : fromSettings;
    g_enabled.store(on, std::memory_order_relaxed);
    const uint32_t scenario = g_scenario.load(std::memory_order_relaxed);
    // (the race, and the front end while a real track is loaded: the pre-race orbit and menu, 2026-11-27, user: "the
    // pre-race orbit is still kinda unsmooth" through the game's frames; the garage and showroom (UI* scenes) stay the
    // game's)
    const bool active = on && (scenario == 1 || (scenario == 0 && LiveRealTrackLoaded()));
    if (on)
    {
        // (the game's frames for the Godot window's menus: ReGPU's frame feed, renderer.cpp REGPU_FRAME_FEED, into
        // out/run/frame_feed.bin unless the environment names another file; its header carries this tag: bits 0-7 the
        // scenario, bit 8 stage B active, bit 31 set by the game)
        static bool feedSet = false;
        if (!feedSet)
        {
            feedSet = true;
            char f[8] = {};
            if (!GetEnvironmentVariableA("REGPU_FRAME_FEED", f, sizeof(f)))
            {
                _putenv_s("REGPU_FRAME_FEED", "frame_feed.bin");
                // (60 a second: in a race the viewer lays the game's HUD and menus over its picture from it)
                char hz[8] = {};
                if (!GetEnvironmentVariableA("REGPU_FRAME_FEED_HZ", hz, sizeof(hz)))
                    _putenv_s("REGPU_FRAME_FEED_HZ", "60");
                Log("stage B: the game's frames to frame_feed.bin for the Godot window (menus)");
            }
        }
        regpu::SetFrameFeedTag(0x80000000u | (scenario & 0xFFu) | (active ? 0x100u : 0u));
    }
    if (active != g_active.exchange(active, std::memory_order_relaxed))
    {
        if (active)
            Log("stage B: on (race or pre-race): the game leaves the world, cars, mirror, reflections and shadows to the Godot viewer");
        else
            Log("stage B: off (the game draws everything)");
        FrontEndRateRefresh();
    }
}
}

// Stage B is on and the renderer is set up for a race (render thread; cheap: the environment is read 4 times a second).
std::atomic<bool> g_stageBHold{ false };

bool StageB()
{
    Update();
    return g_active.load(std::memory_order_relaxed) && !g_stageBHold.load(std::memory_order_relaxed);
}

// A short pause of stage B (live_camera.cpp: the game draws the cars normally for a moment, so ReGPU's fetch watch sees
// each car's damage table): StageB() is false while held; the frame cap is left as it is.
void StageBHold(bool hold)
{
    if (hold != g_stageBHold.exchange(hold))
        Log("stage B: %s", hold ? "held (the game draws a moment: the cars' damage tables)" : "resumed");
}

// The switch is on (in any scenario; frontend_rate.cpp caps the front end at 60 with it).
bool StageBEnabled()
{
    Update();
    return g_enabled.load(std::memory_order_relaxed);
}

// FM3_STAGE_B_KEEP=<hex>[,<hex>...] (live; a diagnosis switch): scene steps (mirror.cpp's list) that still run in stage B.
bool StageBKeeps(uint32_t function)
{
    static uint32_t keep[16] = {};
    static int count = 0;
    static int64_t next = 0;
    const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (now >= next)
    {
        next = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count();
        char v[256] = {};
        GetEnvironmentVariableA("FM3_STAGE_B_KEEP", v, sizeof(v));
        static std::string last;
        if (last != v)
        {
            last = v;
            count = 0;
            for (const char* p = v; *p && count < 16;)
            {
                char* end = nullptr;
                const unsigned long a = strtoul(p, &end, 16);
                if (end == p)
                    break;
                keep[count++] = uint32_t(a);
                p = *end ? end + 1 : end;
            }
            Log("stage B: keeping %d scene step(s): %s", count, v[0] ? v : "(none)");
        }
    }
    for (int i = 0; i < count; i++)
        if (keep[i] == function)
            return true;
    return false;
}

// The race's frame cap with stage B active (0: not active), for frontend_rate.cpp.
uint32_t StageBRaceCap()
{
    if (!g_active.load(std::memory_order_relaxed))
        return 0;
    const uint32_t fps = StageBFps(), userCap = GetSettings().maxFps;
    return fps ? std::max(30u, std::min(fps, userCap)) : userCap;
}

// The renderer's scenario as it is set up (one_tile.cpp's hook of sub_82798890): 0 front end (and photo mode), 1 race.
void StageBScenario(uint32_t index)
{
    if (g_scenario.exchange(index, std::memory_order_relaxed) != index)
        LiveResetCars(); // (live_camera.cpp: the cars of the last scenario are gone)
    Update(true);
}
