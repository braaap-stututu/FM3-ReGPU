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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

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

void LiveCameraHandOff(uint8_t* base, uint32_t view)
{
    const Target& target = CurrentTarget();
    if (!target.on || !view)
        return;
    const uint32_t director = Load32(base, view + 4);
    if (!director)
        return;
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
