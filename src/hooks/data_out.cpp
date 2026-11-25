// Forza "Data Out" telemetry (2026-11-25): the player car's state as UDP packets in the layout Forza Motorsport 7 (and,
// with ",horizon", Forza Horizon 4/5) send, so SimHub, dashboards, shakers and motion rigs work with FM3 unchanged.
//
//   FM3_DATA_OUT=1 | <port> | <ipv4>:<port> [,horizon]     (1 = 127.0.0.1:5300; settings.ini data_out=, same values;
//                                                           the variable wins; re-read twice a second, a live switch)
//
// Everything comes from the player's car-physics object (the HUD's redline getter, src/hooks/mirror.cpp sub_821ED218;
// offsets found against the game's own telemetry screen, frame-ir LIVE.md "Telemetry from the car-physics object",
// FM3-ReGPU NOTES 2026-11-25) and the car body's world matrix (car object +0x10, the car whose position is the physics
// object's):
//   +324 engine speed, +452 redline (rad/s); +1937 gear (byte, 1 = first); +2544 position, +2560 velocity, +2512
//   acceleration (world, x y z; position / velocity / acceleration checked against each other's differences on two
//   AI drives: slopes 1.00, correlations 0.90-0.97); +2792 speed (m/s); +3408 throttle, +3412 brake, +3424 handbrake
//   (FH1's controls block order: unconfirmed), +3416 steering; per wheel (1040-byte blocks, FL FR RR RL): +4164 ground speed (m/s) and
//   +4272 radius (m), +3440 suspension position (m), +3492 three tyre temperature zones (deg F).
// Checked on an AI race (tools/analysis/dataout_listen.py, 2026-11-25): forward speed, lateral acceleration = speed x
// yaw rate (35 m/s x 0.27 rad/s = 9.5 vs 9.1 m/s2), gears, pedals, tyre temperatures rising, race off on the frozen grid.
// Not known yet (sent as 0): slip ratio / angle, surface rumble, power / torque, boost, fuel, lap data, the car's
// ordinal / class / PI. Packet layout: Forza's published "Data Out" format (FM7 "Dash" = 311 bytes, little endian;
// Horizon 4/5 = 324 bytes with 12 more bytes after the 232-byte "Sled" block). arcanite24's pinyon-shift (FH1) sends
// the same packets from FH1's equivalent object (src/telemetry/forza_dash.cpp there), which is how this was scoped.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "hooks/data_out.h"
#include "log.h"
#include "settings.h"

namespace
{
uint32_t Load32(const uint8_t* base, uint32_t a)
{
    uint32_t v;
    memcpy(&v, base + a, 4);
    return _byteswap_ulong(v);
}

float LoadF(const uint8_t* base, uint32_t a)
{
    const uint32_t b = Load32(base, a);
    float v;
    memcpy(&v, &b, 4);
    return std::isfinite(v) ? v : 0.0f;
}

constexpr uint32_t kEngineSpeed = 324, kRedline = 452, kGear = 1937;
constexpr uint32_t kAccel = 2512, kPosition = 2544, kVelocity = 2560, kSpeed = 2792;
constexpr uint32_t kThrottle = 3408, kBrake = 3412, kSteer = 3416, kHandbrake = 3424;
constexpr uint32_t kWheelSpeed = 4164, kWheelRadius = 4164 + 108, kWheelStride = 1040, kSuspension = 3440,
                   kTyreTemp = 3492;
constexpr uint32_t kCarBody = 0x10; // the car object's body matrix (rows: the car's x, y, z axes, then its position)
constexpr float kRpm = 9.5492966f;
// The physics object's wheels are FL FR RR RL; Data Out's are FL FR RL RR.
constexpr int kWheelOf[4] = { 0, 1, 3, 2 };

std::atomic<uint32_t> g_physics{ 0 };
std::atomic<float> g_steer{ 0.0f };

struct Target
{
    bool on = false;
    bool horizon = false;
    sockaddr_in addr = {};
};

const Target& CurrentTarget()
{
    static Target target;
    static std::string last = "\x01";
    static ULONGLONG lastRead = 0;
    const ULONGLONG now = GetTickCount64();
    if (lastRead && now - lastRead < 500)
        return target;
    lastRead = now;
    char buf[128] = {};
    std::string v;
    if (GetEnvironmentVariableA("FM3_DATA_OUT", buf, sizeof(buf)))
        v = buf;
    else
    {
        static const std::string fromSettings = GetSettings().Extra("data_out");
        v = fromSettings;
    }
    if (v == last)
        return target;
    last = v;
    target = {};
    if (v.empty() || v == "0")
    {
        Log("data out: off");
        return target;
    }
    std::string spec = v;
    if (const size_t comma = spec.find(','); comma != std::string::npos)
    {
        target.horizon = spec.substr(comma + 1) == "horizon";
        spec = spec.substr(0, comma);
    }
    std::string host = "127.0.0.1";
    int port = 5300;
    if (const size_t colon = spec.find(':'); colon != std::string::npos)
    {
        host = spec.substr(0, colon);
        port = atoi(spec.c_str() + colon + 1);
    }
    else if (spec != "1")
        port = atoi(spec.c_str());
    target.addr.sin_family = AF_INET;
    target.addr.sin_port = htons(uint16_t(port));
    if (port <= 0 || port > 65535 || inet_pton(AF_INET, host.c_str(), &target.addr.sin_addr) != 1)
    {
        Log("data out: FM3_DATA_OUT=%s not understood (1, <port> or <ipv4>:<port>, optionally ,horizon): off", v.c_str());
        return target = {};
    }
    target.on = true;
    Log("data out: Forza %s packets to %s:%d", target.horizon ? "Horizon 4/5 (324 bytes)" : "Motorsport 7 Dash (311 bytes)",
        host.c_str(), port);
    return target;
}

SOCKET Socket()
{
    static SOCKET s = [] {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return INVALID_SOCKET;
        SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (u != INVALID_SOCKET)
        {
            u_long nonBlocking = 1;
            ioctlsocket(u, FIONBIO, &nonBlocking);
        }
        return u;
    }();
    return s;
}

double Seconds()
{
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

struct Vec
{
    float x = 0, y = 0, z = 0;
};
Vec Sub(Vec a, Vec b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec Cross(Vec a, Vec b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec LoadVec(const uint8_t* base, uint32_t a) { return { LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8) }; }

// The FM7 Dash layout's offsets (the 232-byte "Sled" block, then the dash fields).
namespace dash
{
constexpr size_t kIsRaceOn = 0, kTimestampMs = 4, kEngineMaxRpm = 8, kEngineIdleRpm = 12, kCurrentEngineRpm = 16;
constexpr size_t kAccelerationX = 20, kVelocityX = 32, kAngularVelocityX = 44, kYaw = 56, kPitch = 60, kRoll = 64;
constexpr size_t kNormalizedSuspensionTravel = 68, kWheelRotationSpeed = 100, kSuspensionTravelMeters = 196;
constexpr size_t kPositionX = 232, kSpeed = 244, kTireTemp = 256, kDistanceTraveled = 280;
constexpr size_t kAccel = 303, kBrake = 304, kClutch = 305, kHandBrake = 306, kGear = 307, kSteer = 308;
constexpr size_t kSize = 311, kHorizonGap = 12;
} // namespace dash

struct Packet
{
    uint8_t b[dash::kSize] = {};
    void U32(size_t o, uint32_t v) { memcpy(b + o, &v, 4); }
    void F32(size_t o, float v)
    {
        if (!std::isfinite(v))
            v = 0.0f;
        memcpy(b + o, &v, 4);
    }
};

uint8_t Unit(float v) { return uint8_t(std::lround(std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f) * 255.0f)); }

struct State
{
    double lastSent = 0, lastAxesAt = 0, stillSince = 0;
    Vec lastAxes[3];
    bool haveAxes = false;
    Vec angular; // car space
    float suspMin[4], suspMax[4];
    float suspRest[4];
    bool haveRest = false;
    uint32_t suspCar = 0;
    float stillValues[6] = {};
    float distance = 0;
    bool matchedOnce = false;
};
State g_state;
} // namespace

void DataOutPlayerPhysics(uint32_t physics)
{
    g_physics.store(physics, std::memory_order_relaxed);
}

void DataOutSteerInput(float steer)
{
    g_steer.store(steer, std::memory_order_relaxed);
}

std::atomic<bool> g_wantAccel{ false };
float g_accel[3] = {}; // (the car loop's thread only writes; readers take a copy)
std::atomic<uint32_t> g_accelSeq{ 0 };

bool DataOutPlayerAccel(float out[3])
{
    g_wantAccel.store(true, std::memory_order_relaxed);
    if (!g_accelSeq.load(std::memory_order_acquire))
        return false;
    memcpy(out, g_accel, sizeof(g_accel));
    return true;
}

void DataOutCarDrawn(uint8_t* base, uint32_t car)
{
    const Target& target = CurrentTarget();
    if ((!target.on && !g_wantAccel.load(std::memory_order_relaxed)) || !car)
        return;
    const uint32_t phys = g_physics.load(std::memory_order_relaxed);
    if (phys < 0x40000000 || phys >= 0x80000000)
        return;
    // the player's car: the one drawn where its physics object is
    const Vec pos = LoadVec(base, phys + kPosition);
    const Vec carPos = LoadVec(base, car + kCarBody + 48);
    const Vec d = Sub(pos, carPos);
    if (Dot(d, d) > 1.0f)
        return;
    State& st = g_state;
    if (!st.matchedOnce)
    {
        st.matchedOnce = true;
        Log("data out: the player's car is %08X (physics %08X)", car, phys);
    }
    {
        // the car-frame acceleration for the live stream (FM3E v8, live_camera.cpp): x right, y up, z forward, m/s2
        Vec ax[3];
        for (int i = 0; i < 3; i++)
            ax[i] = LoadVec(base, car + kCarBody + 16 * i);
        const Vec a = LoadVec(base, phys + kAccel);
        g_accel[0] = Dot(a, ax[0]);
        g_accel[1] = Dot(a, ax[1]);
        g_accel[2] = -Dot(a, ax[2]);
        g_accelSeq.fetch_add(1, std::memory_order_release);
    }
    if (!target.on)
        return;
    const double now = Seconds();
    if (now - st.lastSent < 1.0 / 60.0)
        return;
    const double dt = st.lastSent > 0 ? now - st.lastSent : 0.0;
    st.lastSent = now;
    const SOCKET s = Socket();
    if (s == INVALID_SOCKET)
        return;

    Vec axes[3];
    for (int i = 0; i < 3; i++)
        axes[i] = LoadVec(base, car + kCarBody + 16 * i);
    // (the body matrix's z axis points to the car's rear (a race showed the forward speed as -z) and its x axis to the
    // right (the Godot viewer's lateral G from the car's positions = this x, slope 0.96, r 0.91): Data Out's car frame is
    // x right, y up, z forward, so z turns round. The angular velocity's signs are not checked against a Forza game.)
    auto local = [&](Vec w) { return Vec{ Dot(w, axes[0]), Dot(w, axes[1]), -Dot(w, axes[2]) }; };
    // angular velocity from the axes' change: w = 1/2 sum(a_i x da_i/dt)
    if (st.haveAxes && dt > 0.0 && dt < 0.25)
    {
        Vec w;
        for (int i = 0; i < 3; i++)
        {
            const Vec da = Sub(axes[i], st.lastAxes[i]);
            const Vec c = Cross(st.lastAxes[i], da);
            w.x += c.x;
            w.y += c.y;
            w.z += c.z;
        }
        const float k = float(0.5 / dt);
        st.angular = local({ w.x * k, w.y * k, w.z * k });
    }
    for (int i = 0; i < 3; i++)
        st.lastAxes[i] = axes[i];
    st.haveAxes = true;

    Packet p;
    p.U32(dash::kTimestampMs, uint32_t(GetTickCount64()));
    const float rpm = LoadF(base, phys + kEngineSpeed) * kRpm;
    const float speed = LoadF(base, phys + kSpeed);
    float susp[4], wheelSpeed[4];
    for (int i = 0; i < 4; i++)
    {
        susp[i] = LoadF(base, phys + kSuspension + kWheelStride * i);
        wheelSpeed[i] = LoadF(base, phys + kWheelSpeed + kWheelStride * i);
    }
    // a stopped simulation (pause menu, replays' freeze) holds every value: race off after half a second, as the
    // games' own Data Out does in menus
    const float values[6] = { rpm, speed, susp[0], susp[1], susp[2], susp[3] };
    if (memcmp(values, st.stillValues, sizeof(values)) != 0)
    {
        memcpy(st.stillValues, values, sizeof(values));
        st.stillSince = now;
    }
    const bool racing = now - st.stillSince < 0.5;
    if (racing)
    {
        p.U32(dash::kIsRaceOn, 1);
        p.F32(dash::kEngineMaxRpm, LoadF(base, phys + kRedline) * kRpm);
        p.F32(dash::kEngineIdleRpm, 0.0f);
        p.F32(dash::kCurrentEngineRpm, rpm);
        const Vec acc = local(LoadVec(base, phys + kAccel));
        const Vec vel = local(LoadVec(base, phys + kVelocity));
        p.F32(dash::kAccelerationX, acc.x);
        p.F32(dash::kAccelerationX + 4, acc.y);
        p.F32(dash::kAccelerationX + 8, acc.z);
        p.F32(dash::kVelocityX, vel.x);
        p.F32(dash::kVelocityX + 4, vel.y);
        p.F32(dash::kVelocityX + 8, vel.z);
        p.F32(dash::kAngularVelocityX, st.angular.x);
        p.F32(dash::kAngularVelocityX + 4, st.angular.y);
        p.F32(dash::kAngularVelocityX + 8, st.angular.z);
        const Vec f = { -axes[2].x, -axes[2].y, -axes[2].z }, r = axes[0];
        p.F32(dash::kYaw, std::atan2(f.x, f.z));
        p.F32(dash::kPitch, std::asin(std::clamp(f.y, -1.0f, 1.0f)));
        p.F32(dash::kRoll, std::asin(std::clamp(r.y, -1.0f, 1.0f)));
        // suspension: travel from the position this car stood at when first seen (the grid), and 0..1 over the range
        // seen so far (the game's limits are not traced)
        if (st.suspCar != car)
        {
            st.suspCar = car;
            st.haveRest = false;
        }
        if (!st.haveRest)
        {
            for (int i = 0; i < 4; i++)
            {
                st.suspRest[i] = susp[i];
                st.suspMin[i] = susp[i] - 0.02f;
                st.suspMax[i] = susp[i] + 0.02f;
            }
            st.haveRest = true;
        }
        for (int i = 0; i < 4; i++)
        {
            const int w = kWheelOf[i];
            st.suspMin[w] = std::min(st.suspMin[w], susp[w]);
            st.suspMax[w] = std::max(st.suspMax[w], susp[w]);
            p.F32(dash::kNormalizedSuspensionTravel + 4 * i, (susp[w] - st.suspMin[w]) / (st.suspMax[w] - st.suspMin[w]));
            p.F32(dash::kSuspensionTravelMeters + 4 * i, susp[w] - st.suspRest[w]);
            const float radius = LoadF(base, phys + kWheelRadius + kWheelStride * w);
            p.F32(dash::kWheelRotationSpeed + 4 * i, radius > 0.05f ? wheelSpeed[w] / radius : 0.0f);
            const uint32_t t = phys + kTyreTemp + kWheelStride * w;
            p.F32(dash::kTireTemp + 4 * i, (LoadF(base, t) + LoadF(base, t + 4) + LoadF(base, t + 8)) / 3.0f);
        }
        p.F32(dash::kPositionX, pos.x);
        p.F32(dash::kPositionX + 4, pos.y);
        p.F32(dash::kPositionX + 8, pos.z);
        p.F32(dash::kSpeed, speed);
        if (dt > 0.0 && dt < 0.25)
            st.distance += float(std::fabs(speed) * dt);
        p.F32(dash::kDistanceTraveled, st.distance);
        p.b[dash::kAccel] = Unit(LoadF(base, phys + kThrottle));
        p.b[dash::kBrake] = Unit(LoadF(base, phys + kBrake));
        p.b[dash::kClutch] = 0;
        p.b[dash::kHandBrake] = Unit(LoadF(base, phys + kHandbrake));
        // gear: FM3's byte is 1 for first; 0 = reverse and >= 9 = neutral as in FH1 (unconfirmed in FM3); Data Out's
        // reverse is 0 and neutral 11
        const uint8_t g = base[phys + kGear];
        p.b[dash::kGear] = g >= 9 ? 11 : g;
        // steering: the car's own (+3416, FH1's controls block order: it moves when the AI drives too); player 1's pad
        // input only when that reads 0
        float steer = LoadF(base, phys + kSteer);
        if (steer == 0.0f)
            steer = g_steer.load(std::memory_order_relaxed);
        p.b[dash::kSteer] = uint8_t(int8_t(std::lround(std::clamp(steer, -1.0f, 1.0f) * 127.0f)));
    }
    if (target.horizon)
    {
        uint8_t h[dash::kSize + dash::kHorizonGap] = {};
        memcpy(h, p.b, 232);
        memcpy(h + 232 + dash::kHorizonGap, p.b + 232, dash::kSize - 232);
        sendto(s, reinterpret_cast<const char*>(h), int(sizeof(h)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
               int(sizeof(target.addr)));
    }
    else
        sendto(s, reinterpret_cast<const char*>(p.b), int(sizeof(p.b)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
               int(sizeof(target.addr)));
}
