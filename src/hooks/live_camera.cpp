// Live camera stream (2026-11-23, renderer replacement stage A, the "live mirror": Godot\frame-ir\LIVE.md).
//
// FM3 hands each player view's camera to its presentation camera in sub_821E9B50 (r3 = the view; view + 4 = its
// camera director, see src/hooks/vr_camera.cpp for the director's fields). With FM3_LIVE set, every hand-off sends the
// director's camera as one UDP datagram, so an outside renderer (frame-ir's Godot viewer, world_view.gd --live) can draw
// the same view while ReGPU keeps drawing the game as usual. Nothing in the game changes.
//
//   FM3_LIVE=1 | <port> | <host>:<port>    (1 = 127.0.0.1:7342; empty or 0 = off; read twice a second, so
//                                            Fm3 "env FM3_LIVE 1" switches it on in a running game)
//
// Datagram (96 bytes, little-endian, version 1):
//   0  char[4]  "FM3C"
//   4  u32      version (1)
//   8  u32      sequence number (per hand-off, from 1)
//  12  u32      director (guest address: tells the player view from the rear-view mirror and other views)
//  16  f32[16]  view matrix as the game keeps it (+0xD0): world to camera, row vectors (p_cam = p_world * V),
//               left-handed, x right, y up, z forward
//  80  f32      vertical field of view, radians, whole angle (+0x110)
//  84  u32      the active camera's vtable (director +0x0C; e.g. 0x8200206C = the in-car view CCamDriver)
//  88  f64      host time, seconds (QueryPerformanceCounter)
//
// Cars (2026-11-23): one datagram per car the main view draws, per drawn frame (src/hooks/mirror.cpp's sub_828329C8
// hook calls LiveCarDrawn), 96 bytes, little-endian, version 1:
//   0  char[4]  "FM3K"
//   4  u32      version (1)
//   8  u32      sequence number
//  12  u32      car object (guest address; one per car for the race)
//  16  f32[16]  the car body's world matrix (car object +0x10): rows = the car's axes in the world, row 3 = position
//  80  u32[2]   (0)
//  88  f64      host time, seconds
// Right after each car datagram, its wheels ("FM3W", 280 bytes): magic, version 1, seq (the car datagram's), car,
// f32[4][16] the four wheels' world matrices as drawn (car object +0x190 + 0x40 k: spin and steering included), f64 time.
//
// The player's engine and controls ("FM3E", 40 bytes, from the car loop at most every 4 ms): magic, version 3, u32 the
// player's car-physics object, f32 engine speed (rpm), f32 redline (rpm), f32 player 1's steering input (-1..1), u32
// the racing wheel's lock in degrees (0 = no wheel), f32 player 1's brake input (0..1), f64 time (2026-11-24: the
// cockpit tach and steering wheel, the player's brake lights).
//
// The drawn camera ("FM3V", the camera datagram's layout; director = the render object): the view matrix the frame
// with those cars is drawn with (render object +0x0), sent from the car loop whenever it changes. A viewer should take
// its camera from these when they come: "FM3C" (the hand-off) runs ahead of the drawn frame and so of the cars.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hooks/live_camera.h"
#include "log.h"

namespace
{
constexpr uint32_t kDirectorView = 0xD0, kDirectorFov = 0x110, kDirectorActive = 0x0C;

uint32_t Load32(const uint8_t* base, uint32_t a)
{
    uint32_t v;
    memcpy(&v, base + a, 4);
    return _byteswap_ulong(v);
}

#pragma pack(push, 1)
struct Packet
{
    char magic[4];
    uint32_t version;
    uint32_t seq;
    uint32_t director;
    float view[16];
    float fov;
    uint32_t cameraVtable;
    double time;
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 96, "live camera packet layout");

// The car object (sub_828329C8's r4 from the main view's car loop, call site 0x82837F0C) keeps its body's world
// matrix at +0x10 (found 2026-11-23 with FM3_LIVE_CAR_SCAN on the frozen grid): 4x4 floats, rows = the car's axes in
// the world, row 3 = its position (p_world = p_car * M, row vectors). +0x50 holds the same (the previous step's), and
// +0x90.. the four wheels (placed, then spun/steered copies).
constexpr uint32_t kCarBody = 0x10;
// The four wheels as drawn, spin and steering included (same scan): +0x190, +0x1D0, +0x210, +0x250 (+0x90.. hold them
// placed without the spin, +0x290.. again without it).
constexpr uint32_t kCarWheels = 0x190;
#pragma pack(push, 1)
struct WheelPacket
{
    char magic[4]; // "FM3W"
    uint32_t version; // 1
    uint32_t seq;
    uint32_t car;
    float wheel[4][16];
    double time;
};
#pragma pack(pop)
static_assert(sizeof(WheelPacket) == 280, "live wheel packet layout");
#pragma pack(push, 1)
struct CarPacket
{
    char magic[4]; // "FM3K"
    uint32_t version; // 1
    uint32_t seq;
    uint32_t car; // the car object's guest address (stable for the race)
    float body[16];
    uint32_t reserved[2];
    double time;
};
#pragma pack(pop)
static_assert(sizeof(CarPacket) == 96, "live car packet layout");

struct Target
{
    bool on = false;
    sockaddr_in addr = {};
};

// FM3_LIVE, re-read every 500 ms (a live switch).
const Target& CurrentTarget()
{
    static Target target;
    static char last[128] = "\x01";
    static ULONGLONG lastRead = 0;
    const ULONGLONG now = GetTickCount64();
    if (lastRead && now - lastRead < 500)
        return target;
    lastRead = now;
    char buf[128] = {};
    GetEnvironmentVariableA("FM3_LIVE", buf, sizeof(buf));
    if (strcmp(buf, last) == 0)
        return target;
    strcpy_s(last, buf);
    target = {};
    if (!buf[0] || strcmp(buf, "0") == 0)
    {
        Log("live: camera stream off");
        return target;
    }
    char host[96] = "127.0.0.1";
    int port = 7342;
    if (const char* colon = strchr(buf, ':'))
    {
        const size_t n = size_t(colon - buf);
        if (n > 0 && n < sizeof(host))
        {
            memcpy(host, buf, n);
            host[n] = 0;
        }
        port = atoi(colon + 1);
    }
    else if (strcmp(buf, "1") != 0)
        port = atoi(buf);
    target.addr.sin_family = AF_INET;
    target.addr.sin_port = htons(uint16_t(port));
    if (port <= 0 || port > 65535 || inet_pton(AF_INET, host, &target.addr.sin_addr) != 1)
    {
        Log("live: FM3_LIVE=%s not understood (1, <port> or <ipv4>:<port>): camera stream off", buf);
        return target = {};
    }
    target.on = true;
    Log("live: camera stream to %s:%d (frame-ir world_view.gd --live %d)", host, port, port);
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
} // namespace

// Diagnostic (FM3_LIVE_CAR_SCAN=1): candidate world matrices in a car object and one level of the objects it points to:
// three orthonormal rows of 4 floats (16-byte stride) followed by a translation row within the track's range.
static void ScanForMatrices(const uint8_t* base, uint32_t obj, uint32_t span, const char* what, uint32_t car, int depth)
{
    auto f = [&](uint32_t a) {
        const uint32_t b = Load32(base, a);
        float v;
        memcpy(&v, &b, 4);
        return v;
    };
    for (uint32_t o = 0; o + 64 <= span; o += 4)
    {
        const uint32_t a = obj + o;
        float r[3][3], t[3];
        bool ok = true;
        for (int i = 0; i < 3 && ok; i++)
        {
            for (int j = 0; j < 3; j++)
                r[i][j] = f(a + i * 16 + j * 4);
            const float n = r[i][0] * r[i][0] + r[i][1] * r[i][1] + r[i][2] * r[i][2];
            ok = std::isfinite(n) && fabsf(n - 1.0f) < 0.01f;
        }
        if (!ok)
            continue;
        const float d01 = r[0][0] * r[1][0] + r[0][1] * r[1][1] + r[0][2] * r[1][2];
        const float d02 = r[0][0] * r[2][0] + r[0][1] * r[2][1] + r[0][2] * r[2][2];
        const float d12 = r[1][0] * r[2][0] + r[1][1] * r[2][1] + r[1][2] * r[2][2];
        if (fabsf(d01) > 0.01f || fabsf(d02) > 0.01f || fabsf(d12) > 0.01f)
            continue;
        for (int j = 0; j < 3; j++)
            t[j] = f(a + 48 + j * 4);
        if (!(std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])) || fabsf(t[0]) + fabsf(t[2]) < 1.0f ||
            fabsf(t[0]) > 5000 || fabsf(t[1]) > 5000 || fabsf(t[2]) > 5000)
            continue;
        Log("live: car %08X %s+0x%X: rows (%.3f %.3f %.3f) (%.3f %.3f %.3f) (%.3f %.3f %.3f) translation (%.3f %.3f %.3f)", car,
            what, o, r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2], t[0], t[1], t[2]);
    }
    if (depth == 0)
        return;
    // One level down: pointers into guest heap memory.
    for (uint32_t o = 0; o + 4 <= span; o += 4)
    {
        const uint32_t p = Load32(base, obj + o);
        if (p >= 0x40000000 && p < 0x50000000 && p != obj)
        {
            char name[48];
            snprintf(name, sizeof(name), "[+0x%X]", o);
            ScanForMatrices(base, p, 0x400, name, car, 0);
        }
    }
}

static std::atomic<uint32_t> g_lastDirector{ 0 };

// The player's engine (2026-11-24, for the cockpit tach): the car-physics object's engine speed at +324 and redline at
// +452, both rad/s (FH1R's ICarDynamics has the same two offsets). Found through the getters that turn them into rpm
// (x 60/2pi, the constant at 0x82000BEC): sub_821ED218 = redline (+452), called virtually; the call returning to
// 0x821F2044 is made for the player's car (the HUD), so its r3 is the player's physics object. Checked on the CR-X:
// idle 848 rpm, ~5,665 at half throttle with the clutch in (HUD tach ~5,700), redline 9,500. Sent as "FM3E".
static std::atomic<uint32_t> g_playerPhysics{ 0 };
// Player 1's steering input as the game reads it (-1..1, src/kernel/imports_xam.cpp ReadPad) and, on a wheel, the
// configured lock (wheel_lock, degrees of wheel travel for full steering; 0 = no wheel): the viewer's steering wheel.
static std::atomic<float> g_steer{ 0.0f };
static std::atomic<uint32_t> g_steerLock{ 0 };
static std::atomic<float> g_brake{ 0.0f };

void LiveSteerInput(float steer, uint32_t lockDegrees, float brake)
{
    g_steer.store(steer, std::memory_order_relaxed);
    g_steerLock.store(lockDegrees, std::memory_order_relaxed);
    g_brake.store(brake, std::memory_order_relaxed);
}
constexpr uint32_t kEngineSpeed = 324, kEngineRedline = 452;

void LivePlayerPhysics(uint32_t physics)
{
    if (physics && g_playerPhysics.exchange(physics) != physics)
        Log("live: player car physics %08X", physics);
}

#pragma pack(push, 1)
struct EnginePacket
{
    char magic[4]; // "FM3E"
    uint32_t version; // 2
    uint32_t physics; // the player's car-physics object (guest address)
    float rpm; // engine speed, rpm
    float redline; // rpm
    float steer; // player 1's steering input, -1 (full left) .. 1
    uint32_t lock; // degrees of wheel travel for full steering on a racing wheel (wheel_lock); 0 = no wheel
    float brake; // player 1's brake input, 0..1 (v3; the player's brake lights)
    double time;
};
#pragma pack(pop)
static_assert(sizeof(EnginePacket) == 40, "live engine packet layout");

// The car object's shader settings list (a pointer to "Game:\Media\Cars\ShaderSettings\Normal.xml;...;
// Game:\Media\Cars\<CAR>\ColorShaderSettings<n>.xml"; found 2026-11-24 by searching the car objects for pointers to
// the cars' names): the car's identity and colour for "FM3N".
constexpr uint32_t kCarSettingsList = 0xC44;
#pragma pack(push, 1)
struct NamePacket
{
    char magic[4]; // "FM3N"
    uint32_t version; // 1
    uint32_t car; // the car object
    uint32_t color; // Combo_Colors Sequence (1-based) of the car's colour
    uint32_t metallic; // 1: Metallic.xml paint settings
    char name[44]; // media name, e.g. HON_CRXsi_91
    double time;
};
#pragma pack(pop)
static_assert(sizeof(NamePacket) == 72, "live name packet layout");

// Diagnostic (FM3_LIVE_CAM_SCAN=1): where the car loop's arguments hold a copy of the camera: any 3x3 (16-byte stride)
// within 0.02 of the director's current view rotation, plus the translation row next to it, every 2 s.
static void ScanForCamera(const uint8_t* base, const uint32_t args[7])
{
    static ULONGLONG next = 0;
    if (GetTickCount64() < next)
        return;
    next = GetTickCount64() + 2000;
    const uint32_t director = g_lastDirector.load();
    if (!director)
        return;
    auto f = [&](uint32_t a) {
        const uint32_t b = Load32(base, a);
        float v;
        memcpy(&v, &b, 4);
        return v;
    };
    float dv[16];
    for (int i = 0; i < 16; i++)
        dv[i] = f(director + kDirectorView + i * 4);
    Log("live: camscan director view row 3 (%.3f %.3f %.3f) rot (%.3f %.3f %.3f)", dv[12], dv[13], dv[14], dv[0], dv[1], dv[2]);
    static const char* names[7] = { "r3", "r5", "r6", "r7", "r8", "r9", "r10" };
    for (int k = 0; k < 7; k++)
    {
        const uint32_t obj = args[k];
        if (obj < 0x40000000 || obj >= 0x80000000)
            continue;
        for (uint32_t o = 0; o + 64 <= 0x1000; o += 4)
        {
            float m[12];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    m[i * 3 + j] = f(obj + o + i * 16 + j * 4);
            float err = 0;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    err = std::fmax(err, fabsf(m[i * 3 + j] - dv[i * 4 + j]));
            if (!(err < 0.02f))
                continue;
            Log("live: camscan %s(%08X)+0x%X: rot err %.4f, row 3 (%.3f %.3f %.3f)", names[k], obj, o, err, f(obj + o + 48),
                f(obj + o + 52), f(obj + o + 56));
        }
    }
}

void LiveCarDrawn(uint8_t* base, uint32_t car, const uint32_t args[7])
{
    const Target& target = CurrentTarget();
    if (!target.on || !car)
        return;
    static const bool camScan = [] {
        const char* v = getenv("FM3_LIVE_CAM_SCAN");
        return v && *v && strcmp(v, "0") != 0;
    }();
    if (camScan)
        ScanForCamera(base, args);
    static const bool scan = [] {
        const char* v = getenv("FM3_LIVE_CAR_SCAN");
        return v && *v && strcmp(v, "0") != 0;
    }();
    if (scan)
    {
        static uint32_t scanned[16] = {};
        for (uint32_t& c : scanned)
        {
            if (c == car)
                break;
            if (c == 0)
            {
                c = car;
                Log("live: car %08X (vtable %08X): scanning for world matrices", car, Load32(base, car));
                ScanForMatrices(base, car, 0x1000, "", car, 1);
                break;
            }
        }
    }
    const SOCKET s = Socket();
    if (s == INVALID_SOCKET)
        return;
    static uint32_t seq = 0;
    CarPacket p = {};
    memcpy(p.magic, "FM3K", 4);
    p.version = 1;
    p.seq = ++seq;
    p.car = car;
    for (int i = 0; i < 16; i++)
    {
        const uint32_t bits = Load32(base, car + kCarBody + i * 4);
        memcpy(&p.body[i], &bits, 4);
    }
    p.time = Seconds();
    // The camera this frame is drawn with (FM3_LIVE_CAM_SCAN, 2026-11-23): the car loop's render object (r10) holds the
    // frame's view matrix at +0x0, the director's (+0xD0) as it was when this frame was built, one logic step or more
    // behind the director by the time the render thread draws. Sent as "FM3V" (the camera packet's layout) whenever it
    // changes, so a viewer gets cars and camera from the same frame; the camera hand-off's "FM3C" runs ahead of both (on
    // the logic thread at 360 Hz), which shook the cars against the camera.
    if (const uint32_t view = args[6]; view >= 0x40000000 && view < 0x80000000)
    {
        static float last[16] = {};
        Packet v = {};
        memcpy(v.magic, "FM3V", 4);
        v.version = 1;
        for (int i = 0; i < 16; i++)
        {
            const uint32_t bits = Load32(base, view + i * 4);
            memcpy(&v.view[i], &bits, 4);
        }
        if (memcmp(v.view, last, sizeof(last)) != 0)
        {
            memcpy(last, v.view, sizeof(last));
            static uint32_t vseq = 0;
            v.seq = ++vseq;
            v.director = view;
            if (const uint32_t director = g_lastDirector.load())
            {
                const uint32_t fovBits = Load32(base, director + kDirectorFov);
                memcpy(&v.fov, &fovBits, 4);
                const uint32_t camera = Load32(base, director + kDirectorActive);
                v.cameraVtable = camera ? Load32(base, camera) : 0;
            }
            v.time = p.time;
            sendto(s, reinterpret_cast<const char*>(&v), int(sizeof(v)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
                   int(sizeof(target.addr)));
        }
    }
    // Which car this is ("FM3N", about once a second per car; 2026-11-24): the car object's +0xC44 points at its shader
    // settings list, "Game:\Media\Cars\ShaderSettings\<Normal|Metallic>.xml;Game:\Media\Cars\<CAR>\ShaderSettings.xml;
    // Game:\Media\Cars\<CAR>\ColorShaderSettings<n>.xml": the car's media name, its paint type and its colour, n = the
    // Sequence of its row in the database's Combo_Colors (WHERE Ordinal = Data_Car.Id; a player's custom paint is not
    // there). A viewer builds and dresses the car from these, without a capture.
    {
        static uint32_t named[16] = {};
        static double namedAt[16] = {};
        int slot = -1;
        for (int k = 0; k < 16; k++)
            if (named[k] == car || (named[k] == 0 && slot < 0))
            {
                slot = k;
                if (named[k] == car)
                    break;
            }
        if (slot < 0)
        {
            // (all 16 taken, by cars of earlier screens too: the menus' and Free Play's car selection fill the table
            // before the race; without this a race car could stay unnamed (Catalunya's FTO, Silverstone's B.Giese).
            // The slot named longest ago goes: a car still drawn is named again within a second.)
            slot = 0;
            for (int k = 1; k < 16; k++)
                if (namedAt[k] < namedAt[slot])
                    slot = k;
        }
        if (slot >= 0 && named[slot] != car)
        {
            // Diagnostic (FM3_LIVE_DUMP_CARS=1): the car object's first 16 KB to live_car_<addr>.bin in the working
            // folder, once per car, to find where the game keeps a car's paint colour (a player's custom paint is not
            // in the database).
            static const bool dump = [] {
                const char* v = getenv("FM3_LIVE_DUMP_CARS");
                return v && *v && *v != '0';
            }();
            if (dump)
            {
                char path[64];
                snprintf(path, sizeof(path), "live_car_%08X.bin", car);
                if (FILE* f = fopen(path, "wb"))
                {
                    fwrite(base + car, 1, 0x4000, f);
                    fclose(f);
                    Log("live: car %08X dumped to %s", car, path);
                }
            }
        }
        // The name of car nc (slot ns) to the viewer. Sent when a car first shows up, and every car in the table again once a
        // second, drawn this frame or not: a car the game stops drawing (culled, or another draw path while the world is
        // frozen) was named only before the viewer started (Catalunya's FTO, Silverstone's B.Giese: never loaded).
        auto sendName = [&](uint32_t nc, int ns) {
            const uint32_t list = Load32(base, nc + kCarSettingsList);
            if (list >= 0x40000000 && list < 0x80000000)
            {
                char text[256] = {};
                for (int i = 0; i < 255; i++)
                {
                    text[i] = char(base[list + i]);
                    if (!text[i])
                        break;
                }
                NamePacket n = {};
                memcpy(n.magic, "FM3N", 4);
                n.version = 1;
                n.car = nc;
                n.metallic = strstr(text, "\\Metallic.xml") ? 1 : 0;
                if (const char* c = strstr(text, "\\ColorShaderSettings"))
                {
                    n.color = uint32_t(atoi(c + 20));
                    // the media name: the folder before \ColorShaderSettings
                    const char* end = c;
                    const char* start = end;
                    while (start > text && start[-1] != '\\')
                        start--;
                    const size_t len = std::min<size_t>(size_t(end - start), sizeof(n.name) - 1);
                    memcpy(n.name, start, len);
                }
                else if (const char* c2 = strstr(text, "\\ShaderSettings.xml"))
                {
                    // (no colour file in the list, 1 of 8 cars on AmalfiGP's Free Play grid: the car's own folder
                    // before \ShaderSettings.xml; colour 0, the viewer's default)
                    const char* start = c2;
                    while (start > text && start[-1] != '\\')
                        start--;
                    const size_t len = std::min<size_t>(size_t(c2 - start), sizeof(n.name) - 1);
                    memcpy(n.name, start, len);
                }
                static uint32_t logged[16] = {};
                if (logged[ns] != nc)
                {
                    logged[ns] = nc;
                    Log("live: car %08X settings \"%s\" -> FM3N \"%s\" colour %u", nc, text, n.name, n.color);
                }
                n.time = p.time;
                sendto(s, reinterpret_cast<const char*>(&n), int(sizeof(n)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
                       int(sizeof(target.addr)));
            }
        };
        if (slot >= 0 && named[slot] != car)
        {
            named[slot] = car;
            namedAt[slot] = p.time;
            sendName(car, slot);
        }
        static double allAt = 0;
        if (p.time - allAt > 1.0)
        {
            allAt = p.time;
            for (int k = 0; k < 16; k++)
                if (named[k])
                {
                    namedAt[k] = p.time;
                    sendName(named[k], k);
                }
        }
    }
    // The player's engine ("FM3E"), at most every 4 ms (the car loop runs once per car per frame).
    if (const uint32_t phys = g_playerPhysics.load(); phys >= 0x40000000 && phys < 0x80000000)
    {
        static double lastSent = 0;
        if (p.time - lastSent >= 0.004)
        {
            lastSent = p.time;
            EnginePacket e = {};
            memcpy(e.magic, "FM3E", 4);
            e.version = 3;
            e.physics = phys;
            e.steer = g_steer.load(std::memory_order_relaxed);
            e.lock = g_steerLock.load(std::memory_order_relaxed);
            e.brake = g_brake.load(std::memory_order_relaxed);
            auto f = [&](uint32_t a) {
                const uint32_t b = Load32(base, a);
                float v;
                memcpy(&v, &b, 4);
                return v;
            };
            constexpr float kRpm = 9.5492966f; // 60 / 2pi
            e.rpm = f(phys + kEngineSpeed) * kRpm;
            e.redline = f(phys + kEngineRedline) * kRpm;
            e.time = p.time;
            sendto(s, reinterpret_cast<const char*>(&e), int(sizeof(e)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
                   int(sizeof(target.addr)));
        }
    }
    // (After the frame's camera: a viewer groups the car datagrams that follow an "FM3V" into that frame.)
    sendto(s, reinterpret_cast<const char*>(&p), int(sizeof(p)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
           int(sizeof(target.addr)));
    {
        WheelPacket w = {};
        memcpy(w.magic, "FM3W", 4);
        w.version = 1;
        w.seq = p.seq;
        w.car = car;
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < 16; i++)
            {
                const uint32_t bits = Load32(base, car + kCarWheels + k * 0x40 + i * 4);
                memcpy(&w.wheel[k][i], &bits, 4);
            }
        w.time = p.time;
        sendto(s, reinterpret_cast<const char*>(&w), int(sizeof(w)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
               int(sizeof(target.addr)));
    }
    static uint32_t seen[16] = {};
    for (uint32_t& c : seen)
    {
        if (c == car)
            break;
        if (c == 0)
        {
            c = car;
            Log("live: car %08X at (%.3f %.3f %.3f)", car, p.body[12], p.body[13], p.body[14]);
            break;
        }
    }
}

void LiveCameraHandOff(uint8_t* base, uint32_t view)
{
    const Target& target = CurrentTarget();
    if (!target.on || !view)
        return;
    const uint32_t director = Load32(base, view + 4);
    if (!director)
        return;
    g_lastDirector.store(director);
    const SOCKET s = Socket();
    if (s == INVALID_SOCKET)
        return;
    static uint32_t seq = 0;
    Packet p = {};
    memcpy(p.magic, "FM3C", 4);
    p.version = 1;
    p.seq = ++seq;
    p.director = director;
    for (int i = 0; i < 16; i++)
    {
        const uint32_t bits = Load32(base, director + kDirectorView + i * 4);
        memcpy(&p.view[i], &bits, 4);
    }
    {
        const uint32_t bits = Load32(base, director + kDirectorFov);
        memcpy(&p.fov, &bits, 4);
    }
    const uint32_t camera = Load32(base, director + kDirectorActive);
    p.cameraVtable = camera ? Load32(base, camera) : 0;
    p.time = Seconds();
    // (Non-blocking and fire-and-forget: nobody listening, or a full buffer, costs nothing but the send call.)
    sendto(s, reinterpret_cast<const char*>(&p), int(sizeof(p)), 0, reinterpret_cast<const sockaddr*>(&target.addr),
           int(sizeof(target.addr)));
    // Each director once in the log (the player view, the rear-view mirror, ...), with its first matrix.
    static uint32_t seen[8] = {};
    for (uint32_t& d : seen)
    {
        if (d == director)
            break;
        if (d == 0)
        {
            d = director;
            Log("live: director %08X camera vtable %08X fov %.2f deg, view row 3 (%.3f %.3f %.3f)", director, p.cameraVtable,
                p.fov * 57.29578f, p.view[12], p.view[13], p.view[14]);
            break;
        }
    }
}
