// Triple screen (session 39; ReGPU REGPU_TRIPLE draws the main view once per screen, regpu::triple::View).
//
// Each screen gets the exact view of its own plane from the driver's eye: the camera turned to face the screen and an
// off-axis projection over the screen's picture (the eye is not on a side screen's centre line, and bezels and gaps
// move the pictures apart). FM3's own camera is widened at the hand-off so its frustum (and its culling) covers all
// three pictures; ReGPU maps that frustum's clip position to each screen's.
//
// Two ways to describe the screens (settings.ini, launcher "Triple screens" page; src/gpu_host.cpp sets the variables):
//  - physical (FM3_TRIPLE_MODE=physical): the real layout. FM3_TRIPLE_DIAG (inches, 16:9 screens), FM3_TRIPLE_DIST
//    (eye to the centre screen, cm), FM3_TRIPLE_BEZEL_MM (one screen's bezel), FM3_TRIPLE_GAP_MM (space between two
//    screens' frames), FM3_TRIPLE_ANGLE_LEFT / _RIGHT (degrees each side screen is turned in from the centre screen's
//    plane; 0 = so it faces the eye). The game camera's own field of view is not used (the layout decides it).
//  - auto (default): FM3_TRIPLE_FOV (each screen's vertical field of view, 0 = the camera's own), FM3_TRIPLE_ANGLE (the
//    turn to a side screen, 0 = one screen's width), FM3_TRIPLE_BEZEL (degrees added), all three screens alike.
#include "hooks/triple_screen.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "live_switch.h"
#include "log.h"
#include "regpu/regpu.h"

#include <ppc_context.h>

namespace
{
constexpr uint32_t kDirectorFov = 0x110;
constexpr float kPi = 3.14159265f, kD2R = kPi / 180.0f, kAspect = 16.0f / 9.0f;
constexpr float kMaxHalf = 83.0f * kD2R; // FM3's frustum stays under 166 degrees across

uint32_t Load32(uint8_t* base, uint32_t a)
{
    uint32_t v;
    memcpy(&v, base + a, 4);
    return _byteswap_ulong(v);
}
float LoadF(uint8_t* base, uint32_t a)
{
    const uint32_t v = Load32(base, a);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
void StoreF(uint8_t* base, uint32_t a, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    v = _byteswap_ulong(v);
    memcpy(base + a, &v, 4);
}
float Env(const char* name, float fallback)
{
    const char* v = getenv(name);
    return v && *v ? float(atof(v)) : fallback;
}

// A screen seen from the eye (top view, x right, z forward): turned by `yaw` (right positive), its plane `d` in front
// of the eye along its own forward, the picture from `l` to `r` across that plane, `halfH` above and below eye height.
struct Screen
{
    float yaw = 0, d = 1, l = -1, r = 1, halfH = 1;
};

// The eye-space directions of a screen's picture corners: max |x|/z and |y|/z (tangents), false if a corner is beside
// or behind the eye.
bool Extent(const Screen& s, float& tanX, float& tanY)
{
    const float c = cosf(s.yaw), sn = sinf(s.yaw);
    for (float u : { s.l, s.r })
    {
        const float x = u * c + s.d * sn, z = -u * sn + s.d * c;
        if (z <= 1e-3f)
            return false;
        tanX = std::max(tanX, fabsf(x) / z);
        tanY = std::max(tanY, s.halfH / z);
    }
    return true;
}

// The screen's view of the game's frustum (P00g, P11g: the game projection's x and y scales).
regpu::triple::View ViewOf(const Screen& s, float p00g, float p11g)
{
    const float a = 2.0f * s.d / (s.r - s.l), cs = (s.r + s.l) / (s.r - s.l);
    const float c = cosf(s.yaw), sn = sinf(s.yaw);
    regpu::triple::View v;
    v.a = (a * c - cs * sn) / p00g;
    v.b = -a * sn - cs * c;
    v.c = sn / p00g;
    v.d = c;
    v.e = (s.d / s.halfH) / p11g;
    return v;
}

// The three screens of a physical layout (metres).
void PhysicalScreens(Screen (&out)[3], float& turnL, float& turnR)
{
    const float diag = Env("FM3_TRIPLE_DIAG", 27.0f) * 0.0254f;
    const float w = diag * 16.0f / sqrtf(16.0f * 16.0f + 9.0f * 9.0f), h = w * 9.0f / 16.0f;
    const float dist = std::max(0.2f, Env("FM3_TRIPLE_DIST", 60.0f) * 0.01f);
    const float bezel = std::max(0.0f, Env("FM3_TRIPLE_BEZEL_MM", 0.0f) * 0.001f);
    const float gap = std::max(0.0f, Env("FM3_TRIPLE_GAP_MM", 0.0f) * 0.001f);
    const float edge = bezel + gap * 0.5f;     // from a picture's edge to the hinge between two screens
    const float hinge = w * 0.5f + edge;       // the hinge's distance from the centre line
    // A side screen turned by phi in from the centre screen's plane: plane distance and picture span in its plane.
    auto side = [&](float phi, Screen& s) {
        s.d = hinge * sinf(phi) + dist * cosf(phi);
        s.l = hinge * cosf(phi) - dist * sinf(phi) + edge;
        s.r = s.l + w;
        s.halfH = h * 0.5f;
    };
    // "Facing the eye": the angle that puts the eye on the side screen's centre line (l + r = 0).
    auto facing = [&] {
        float lo = 0.0f, hi = 89.0f * kD2R;
        for (int i = 0; i < 40; i++)
        {
            const float mid = 0.5f * (lo + hi);
            Screen s;
            side(mid, s);
            (s.l + s.r > 0.0f ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    };
    const float wantL = Env("FM3_TRIPLE_ANGLE_LEFT", 0.0f) * kD2R, wantR = Env("FM3_TRIPLE_ANGLE_RIGHT", 0.0f) * kD2R;
    turnL = wantL > 0.0f ? std::min(wantL, 89.0f * kD2R) : facing();
    turnR = wantR > 0.0f ? std::min(wantR, 89.0f * kD2R) : facing();
    out[1] = Screen{ 0.0f, dist, -w * 0.5f, w * 0.5f, h * 0.5f };
    side(turnR, out[2]);
    out[2].yaw = turnR;
    side(turnL, out[0]);
    out[0].yaw = -turnL;
    std::swap(out[0].l, out[0].r); // (mirrored: its picture lies left of the hinge)
    out[0].l = -out[0].l, out[0].r = -out[0].r;
}

// The three screens of the auto mode: all alike, a unit distance, turned by `turn`.
void AutoScreens(Screen (&out)[3], float vs, float turn)
{
    const float halfW = tanf(vs * 0.5f) * kAspect, halfH = tanf(vs * 0.5f);
    for (int i = 0; i < 3; i++)
        out[i] = Screen{ (i - 1) * turn, 1.0f, -halfW, halfW, halfH };
}
} // namespace

bool TripleOn()
{
    static const bool on = [] {
        const char* v = getenv("REGPU_TRIPLE");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return on;
}

void TripleHandOff(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*))
{
    const uint32_t view = ctx.r3.u32;
    const uint32_t director = view ? Load32(base, view + 4) : 0;
    const float fovSaved = director ? LoadF(base, director + kDirectorFov) : 0.0f;
    if (!(fovSaved > 0.05f && fovSaved < 3.0f))
    {
        original(ctx, base);
        return;
    }
    static const bool physical = [] {
        const char* m = getenv("FM3_TRIPLE_MODE");
        return m && strcmp(m, "physical") == 0;
    }();
    Screen screens[3];
    float turnL = 0, turnR = 0;
    if (physical)
        PhysicalScreens(screens, turnL, turnR);
    else
    {
        // Auto: each screen's vertical field (vs) from the setting or the camera; the turn one screen's width (plus the
        // bezel setting) or the setting; vs made smaller until the outer edges fit one frustum.
        const float wantFov = Env("FM3_TRIPLE_FOV", 0.0f) * kD2R, wantTurn = Env("FM3_TRIPLE_ANGLE", 0.0f) * kD2R, bezel = Env("FM3_TRIPLE_BEZEL", 0.0f) * kD2R;
        auto hFov = [](float v) { return 2.0f * atanf(tanf(v * 0.5f) * kAspect); };
        auto turnFor = [&](float v) { return (wantTurn > 0.0f ? wantTurn : hFov(v)) + bezel; };
        float vs = wantFov > 0.05f ? wantFov : fovSaved;
        if (turnFor(vs) + hFov(vs) * 0.5f > kMaxHalf)
        {
            float lo = 0.05f, hi = vs;
            for (int i = 0; i < 30; i++)
            {
                const float mid = 0.5f * (lo + hi);
                (turnFor(mid) + hFov(mid) * 0.5f > kMaxHalf ? hi : lo) = mid;
            }
            vs = lo;
        }
        turnL = turnR = std::min(turnFor(vs), kMaxHalf - hFov(vs) * 0.5f);
        AutoScreens(screens, vs, turnR);
    }
    // FM3's frustum: wide enough for every picture corner (16:9, so the wider of the two needs decides), at most 166
    // degrees across (a corner beyond that is cut off at the edge).
    float tanX = 0, tanY = 0;
    bool fits = true;
    for (const Screen& s : screens)
        fits &= Extent(s, tanX, tanY);
    float tanH = std::max(tanX, tanY * kAspect) * 1.01f;
    if (!fits || tanH > tanf(kMaxHalf))
        tanH = tanf(kMaxHalf);
    // FM3_TRIPLE_NARROW=1 (experiment, live): FM3's camera keeps the centre screen's field of view (its shadows, fitted to
    // the view, stay sharp), the side screens drawn from what it didn't cull.
    static LiveSwitch narrow("FM3_TRIPLE_NARROW");
    if (narrow)
        tanH = std::max(screens[1].r, -screens[1].l) / screens[1].d * 1.01f;
    const float p00g = 1.0f / tanH, p11g = kAspect / tanH;
    const float gameFov = 2.0f * atanf(tanH / kAspect);
    regpu::triple::View views[3];
    for (int i = 0; i < 3; i++)
        views[i] = ViewOf(screens[i], p00g, p11g);
    StoreF(base, director + kDirectorFov, gameFov);
    original(ctx, base);
    StoreF(base, director + kDirectorFov, fovSaved);
    regpu::triple::SubmitViews(views);
    static float logged = -1;
    const float sig = turnL * 3 + turnR + screens[1].d + screens[1].r;
    if (fabsf(logged - sig) > 1e-4f)
    {
        logged = sig;
        auto angle = [](const Screen& s, float u) { return atan2f(u * cosf(s.yaw) + s.d * sinf(s.yaw), -u * sinf(s.yaw) + s.d * cosf(s.yaw)); };
        const float across = (angle(screens[2], screens[2].r) - angle(screens[0], screens[0].l)) / kD2R;
        Log("triple: %s layout: side screens turned %.1f / %.1f deg, %.1f deg across, centre %.1f deg high; FM3's frustum %.1f deg across%s",
            physical ? "physical" : "auto", turnL / kD2R, turnR / kD2R, across, 2.0f * atanf(screens[1].halfH / screens[1].d) / kD2R,
            2.0f * atanf(tanH) / kD2R, fits && tanH < tanf(kMaxHalf) * 0.999f ? "" : " (the outer edges don't fit: cut off)");
    }
}
