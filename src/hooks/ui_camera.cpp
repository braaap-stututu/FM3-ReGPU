// The front end's animated cameras at any frame rate (session 40, user: "the car in the career menu background where
// it gets showcased" ran several times too fast at a high frame cap).
//
// CUI4AnimatedCamera (vtable 0x820182A4), the camera that shows a car off behind the career and other menus, advances its
// animation player (the object at +108, vtable 0x82001BB4) by a fixed 1/60 s (the constant at 0x820072BC) on every update
// (sub_8230DEB0 -> the player's slot 3, sub_82474758). It updates once per main-loop frame, so the camera ran cap/60
// times too fast (as Forza Horizon 1's port found for its 1/30 step in the same engine). Only that call is changed (its
// return address 0x8230DF08): the step becomes the real time since that player's last update, so the camera keeps
// console speed at any frame rate. A player not updated for more than 250 ms (a camera coming back) or the first update
// takes the game's step, so a shot doesn't jump ahead. FM3_UI_CAMERA=game: the game's fixed step (A/B, at start).
//
// CUI4CarTargetCamera (vtable 0x82018F44; garage/showroom), in its automatic orbit (state 2, +256), adds 0.1 degree
// (0x820131B4) to its angle (+112) on every update in sub_823138D0, then builds its view (sub_823135F0, called from
// 0x82313AEC). At the console's 60 updates a second that is 6 degrees a second; here the fixed step is taken back and
// 6 degrees per elapsed second added instead (same 250 ms rule).
// Checked (career menu showcase, shots 4 s and 8 s after opening it, mean pixel difference to a 60 fps run): 240 fps
// with the game's step 43 / 75, with this 4.1 / 4.8.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "kernel/imports.h"
#include "log.h"

namespace
{
constexpr uint32_t kAnimatedCameraStepReturn = 0x8230DF08; // sub_8230DEB0's call of the player's slot 3
constexpr uint32_t kOrbitViewReturn = 0x82313AEC;          // sub_823138D0's call of sub_823135F0
constexpr float kOrbitStep = 0.00174532924f;                // 0.1 degree (the game's constant at 0x820131B4)
constexpr float kTwoPi = 6.28318548f;

float LoadFloat(uint32_t a)
{
    const uint32_t b = LoadBE32(a);
    float f;
    memcpy(&f, &b, 4);
    return f;
}
void StoreFloat(uint32_t a, float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    StoreBE32(a, b);
}

bool GameStep()
{
    static const bool game = [] {
        const char* v = getenv("FM3_UI_CAMERA");
        return v && strcmp(v, "game") == 0;
    }();
    return game;
}

double ElapsedFor(uint32_t player, double fixed)
{
    using Clock = std::chrono::steady_clock;
    static std::unordered_map<uint32_t, Clock::time_point> last; // (main thread only)
    const auto now = Clock::now();
    if (last.size() > 64)
        last.clear();
    auto [it, inserted] = last.try_emplace(player, now);
    const double dt = std::chrono::duration<double>(now - it->second).count();
    it->second = now;
    return inserted || dt > 0.25 ? fixed : dt;
}
}

PPC_FUNC_IMPL(__imp__sub_82474758);
PPC_FUNC(sub_82474758)
{
    if (ctx.lr == kAnimatedCameraStepReturn && !GameStep())
        ctx.f1.f64 = double(float(ElapsedFor(ctx.r3.u32, ctx.f1.f64)));
    __imp__sub_82474758(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_823135F0);
PPC_FUNC(sub_823135F0)
{
    const uint32_t camera = ctx.r3.u32;
    if (ctx.lr == kOrbitViewReturn && !GameStep() && LoadBE32(camera + 256) == 2)
    {
        const double dt = ElapsedFor(camera, 1.0 / 60.0);
        float angle = LoadFloat(camera + 112) - kOrbitStep + float(kOrbitStep * 60.0 * dt);
        angle = std::fmod(angle, kTwoPi);
        if (angle < 0)
            angle += kTwoPi;
        StoreFloat(camera + 112, angle);
    }
    __imp__sub_823135F0(ctx, base);
}
