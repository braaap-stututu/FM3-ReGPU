// Frame time at frame rates other than 60 (session 13; docs: NOTES.md "Frame rate").
//
// How FM3 paces itself (the FM4/FH1 engine; the scheme is described in Forza Horizon 1:
// ReImagined's docs/framerate.md, this is our own implementation of it):
// - D3D's vblank callback sub_822580C8 counts guest vblanks and sets the main loop's event
//   (every vblank: the developer options mainthread30fps/frontend30fps are off by default).
// - The main loop sub_821F5DE8 waits on that event, then takes the frame time from the
//   kernel's millisecond tick (KeTimeStampBundle): dt = ms * 0.001. A frame of 16 or 17 ms is
//   snapped to 1/59.94 s (the remainder carried to later frames); anything else is used as
//   it is. More than 4000 ms (a load) counts as 16 ms.
// - Physics takes dt in up to two sub-steps (CarPhysicsUpdateHz; as FH1 traced it, not checked in
//   FM3 yet), so game time follows the
//   clock, not the frame count: a faster vblank gives more frames, not a faster game.
// Above 60 fps the millisecond tick makes each frame's dt whole milliseconds (144 fps: 6 or
// 7 ms, sometimes 0), which judders though the sum is right. So at rates other than 60 the
// main loop's call of sub_821F6AF8 (true: take "dt = ms * 0.001" as it is) answers true with
// the loop's dt register f31 set from the host clock: each frame is stamped with the nominal
// time of the vblank that released it when it starts within 2 ms of it, else with the time
// it starts (a late frame). dt is the difference of two stamps, so frames that keep up get
// exactly the vblank period and the sum is real time. At 60 fps nothing changes.
//
// Live switches (Fm3 "switch NAME 1"): FM3_GAME_FRAME_TIME keeps the game's millisecond dt (A/B);
// FM3_FRAMETIME_LOG logs dt statistics every 10 s.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <windows.h>

#include "hooks/frame_time.h"
#include "kernel/imports.h"
#include "live_switch.h"
#include "log.h"

namespace
{
constexpr uint32_t kFrameTimeCallReturn = 0x821F6004; // sub_821F5DE8's call of sub_821F6AF8

std::atomic<uint32_t> g_hz{ 60 };

double Freq()
{
    static const double f = [] {
        LARGE_INTEGER q;
        QueryPerformanceFrequency(&q);
        return double(q.QuadPart);
    }();
    return f;
}
int64_t Now()
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

bool Precise()
{
    static LiveSwitch game("FM3_GAME_FRAME_TIME");
    return !game;
}

// The latest guest vblank: its nominal time on a grid of the vblank period, kept at the earliest
// time consistent with every callback seen (a callback is late, never early). One writer (the
// vblank interrupt thread); the main loop reads it.
std::atomic<int64_t> g_vblankNominal{ 0 };

void OnVblank()
{
    const int64_t now = Now();
    const int64_t period = int64_t(Freq() / g_hz.load(std::memory_order_relaxed));
    int64_t nominal = g_vblankNominal.load(std::memory_order_relaxed);
    if (nominal == 0)
        nominal = now;
    else
    {
        nominal += period;
        if (now < nominal)
            nominal = now;
        else if (now - nominal >= period)
            nominal += (now - nominal) / period * period; // vblanks not seen
    }
    g_vblankNominal.store(nominal, std::memory_order_release);
}

struct Stats
{
    uint64_t frames = 0, late = 0;
    double sum = 0, sum2 = 0, min = 1e9, max = 0, gameSum2 = 0;
    int64_t start = 0;
} g_stats;

void Account(double dt, double gameDt, bool late)
{
    static LiveSwitch log("FM3_FRAMETIME_LOG");
    if (!log)
    {
        g_stats = Stats{};
        return;
    }
    Stats& s = g_stats;
    const int64_t now = Now();
    if (!s.start)
        s.start = now;
    s.frames++;
    s.late += late;
    s.sum += dt;
    s.sum2 += dt * dt;
    s.min = std::min(s.min, dt);
    s.max = std::max(s.max, dt);
    s.gameSum2 += (gameDt - dt) * (gameDt - dt);
    if (double(now - s.start) / Freq() >= 10.0)
    {
        const double n = double(s.frames), mean = s.sum / n;
        Log("frame time: %llu frames in %.2f s (%.1f fps), dt mean %.3f ms, sd %.3f, min %.3f, max %.3f, late %llu; "
            "game's ms dt off by %.3f ms rms",
            (unsigned long long)s.frames, double(now - s.start) / Freq(), n / (double(now - s.start) / Freq()), mean * 1e3,
            std::sqrt(std::max(0.0, s.sum2 / n - mean * mean)) * 1e3, s.min * 1e3, s.max * 1e3, (unsigned long long)s.late,
            std::sqrt(s.gameSum2 / n) * 1e3);
        s = Stats{};
        s.start = now;
    }
}
} // namespace

void FrameTimeSetHz(uint32_t hz)
{
    g_hz.store(std::max<uint32_t>(hz, 1), std::memory_order_relaxed);
    Log("frame time: guest vblank %u Hz, %s", hz,
        hz == 60 ? "the game's own frame time" : "frame time from the vblank clock (switch FM3_GAME_FRAME_TIME: the game's)");
}

PPC_FUNC_IMPL(__imp__sub_822580C8);
PPC_FUNC(sub_822580C8)
{
    OnVblank();
    __imp__sub_822580C8(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_821F6AF8);
PPC_FUNC(sub_821F6AF8)
{
    if (ctx.lr != kFrameTimeCallReturn || g_hz.load(std::memory_order_relaxed) == 60 || !Precise())
    {
        __imp__sub_821F6AF8(ctx, base);
        return;
    }
    // f31 = ms * 0.001 (the game's dt), r30 = ms (16 after a load of more than 4 s).
    const double gameDt = ctx.f31.f64;
    static int64_t prev = 0;
    const int64_t now = Now();
    const int64_t nominal = g_vblankNominal.load(std::memory_order_acquire);
    const bool onTarget = nominal && now >= nominal && double(now - nominal) < 0.002 * Freq();
    const int64_t stamp = onTarget ? nominal : now;
    if (!prev || ctx.r30.u32 > 4000 || (ctx.r30.u32 == 16 && double(stamp - prev) / Freq() > 4.0))
    {
        // The first frame, or one after a load: the game's own rule.
        prev = stamp;
        __imp__sub_821F6AF8(ctx, base);
        return;
    }
    double dt = double(stamp - prev) / Freq();
    prev = stamp;
    dt = std::clamp(dt, 0.0001, 4.0);
    Account(dt, gameDt, !onTarget);
    ctx.f31.f64 = double(float(dt));
    ctx.r3.u64 = 1; // take f31 as it is (and drop the loop's carried remainder)
}
