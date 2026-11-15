// VR head tracking in the cockpit (session 37; the scheme Forza Horizon 1: ReImagined describes for its engine, FM3's
// successor; our own code and FM3's own addresses).
//
// FM3's player view has a camera director (found by the CCamDriver object it creates in sub_8246DF78, read from guest
// memory on the race grid; FH1's layout with every field 32 bytes earlier):
//   +0x14 the active camera (Camera::ICamera; CCamDriver vtable 0x8200206C, CCamCockpit 0x82034FC4)
//   +0x18 ... the player's cameras
//   +0x90 camera position in the world (x, y, z)    +0xB0 forward    +0xC0 up
//   +0xD0 the view matrix: 4x4 floats, world to camera, row vectors, left-handed (x right, y up, z forward)
//   +0x110 vertical field of view, radians (the whole angle)
// The matrix is read through one accessor (sub_821E3990: r3 + 208), called only by sub_821E9B50, which hands the matrix
// and the field of view (sub_821ED758: +272) to the view's presentation camera (view + 4 = director, + 36 = camera).
// So for that call, with an in-car camera active, the head pose goes into the matrix and the headset's field of view
// into +0x110, and both are put back after: the renderer draws from the head, the camera's own state stays as it is.
//
// Settings: vr=cockpit (or FM3_VR / REGPU_VR) with ReGPU's VR on. FM3_VR_FAKE="yaw pitch roll x y z" (degrees, cm;
// read twice a second) poses the camera without a headset, to check the transform on screenshots.
// FM3_VR_FOV_MARGIN (degrees, default 5) widens the rendered field of view past the headset's.
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hooks/vr_camera.h"
#include "log.h"
#include "regpu/regpu.h"

#include <ppc_context.h>

namespace
{
constexpr uint32_t kDirectorView = 0xD0, kDirectorFov = 0x110, kDirectorActive = 0x14;
constexpr uint32_t kCamDriverVtable = 0x8200206C, kCamCockpitVtable = 0x82034FC4;

bool Enabled()
{
    static const bool on = [] {
        const char* v = getenv("REGPU_VR");
        return v && (strcmp(v, "cockpit") == 0 || strcmp(v, "stereo") == 0);
    }();
    return on;
}

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

// FM3_VR_FAKE: a fixed head pose (no headset needed), as OpenXR would give it.
bool FakePose(regpu::vr::HeadPose& out)
{
    static float v[6] = {};
    static bool have = false;
    static DWORD lastRead = 0;
    if (GetTickCount() - lastRead > 500)
    {
        lastRead = GetTickCount();
        char buf[128] = {};
        have = GetEnvironmentVariableA("FM3_VR_FAKE", buf, sizeof(buf)) > 0 &&
               sscanf_s(buf, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) >= 1;
    }
    if (!have)
        return false;
    // Yaw about +y (turning left is positive in OpenXR's right-handed space), then pitch about x, then roll about z.
    const float d = 3.14159265f / 180.0f;
    const float cy = cosf(v[0] * d * 0.5f), sy = sinf(v[0] * d * 0.5f), cp = cosf(v[1] * d * 0.5f), sp = sinf(v[1] * d * 0.5f);
    const float cr = cosf(v[2] * d * 0.5f), sr = sinf(v[2] * d * 0.5f);
    // q = qYaw * qPitch * qRoll
    const float qy[4] = { 0, sy, 0, cy }, qp[4] = { sp, 0, 0, cp }, qr[4] = { 0, 0, sr, cr };
    auto mul = [](const float a[4], const float b[4], float o[4]) {
        o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    };
    float t[4];
    mul(qy, qp, t);
    mul(t, qr, out.orientation);
    out.position[0] = v[3] * 0.01f, out.position[1] = v[4] * 0.01f, out.position[2] = v[5] * 0.01f;
    out.fovLeft = -0.9f, out.fovRight = 0.9f, out.fovUp = 0.9f, out.fovDown = -0.9f;
    return true;
}

// World -> head for row vectors: p_cam = p_world * V, p_cam = p_head * Rlh^T + t (Rlh's columns are the head's axes
// in the camera's space), so p_head = (p_cam - t) * Rlh and V' = V * T(-t) * Rlh. OpenXR is right-handed with -z
// forward, the game camera left-handed with +z forward: Rlh = S Rxr S, t = S txr, S = diag(1, 1, -1).
void PoseView(const float v[16], const regpu::vr::HeadPose& p, float out[16])
{
    const float x = p.orientation[0], y = p.orientation[1], z = p.orientation[2], w = p.orientation[3];
    const float r[3][3] = {
        { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
        { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
        { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) },
    };
    float m[3][3]; // Rlh
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] = r[i][j] * ((i == 2) != (j == 2) ? -1.0f : 1.0f);
    const float t[3] = { p.position[0], p.position[1], -p.position[2] };
    // A = V * T(-t): V's last column is (0, 0, 0, 1), so only row 3 moves, by -t.
    float a[16];
    memcpy(a, v, sizeof(a));
    for (int j = 0; j < 3; j++)
        a[12 + j] = v[12 + j] - t[j];
    // out = A * [Rlh 0; 0 1]
    for (int row = 0; row < 4; row++)
    {
        for (int j = 0; j < 3; j++)
            out[row * 4 + j] = a[row * 4 + 0] * m[0][j] + a[row * 4 + 1] * m[1][j] + a[row * 4 + 2] * m[2][j];
        out[row * 4 + 3] = a[row * 4 + 3];
    }
}

// The pose the current main frame's view was rendered with (handed to ReGPU at the next main frame).
regpu::vr::HeadPose g_framePose;
float g_frameFov = 0;
bool g_framePosed = false;
} // namespace

void VrCameraMainFrame()
{
    if (!Enabled())
        return;
    regpu::vr::SubmitFramePose(g_framePosed ? &g_framePose : nullptr, g_frameFov);
    g_framePosed = false;
}

PPC_FUNC_IMPL(__imp__sub_821E9B50);
PPC_FUNC(sub_821E9B50)
{
    if (!Enabled())
    {
        __imp__sub_821E9B50(ctx, base);
        return;
    }
    const uint32_t view = ctx.r3.u32;
    const uint32_t director = view ? Load32(base, view + 4) : 0;
    const uint32_t camera = director ? Load32(base, director + kDirectorActive) : 0;
    const uint32_t vtable = camera ? Load32(base, camera) : 0;
    regpu::vr::HeadPose pose;
    const bool inCar = vtable == kCamDriverVtable || vtable == kCamCockpitVtable;
    if (!inCar || !(FakePose(pose) || regpu::vr::GetHeadPose(pose)))
    {
        __imp__sub_821E9B50(ctx, base);
        return;
    }
    // (Two player views: only the first director's view is posed; the rear-view mirror has its own camera.)
    float viewM[16], posed[16];
    uint32_t saved[16];
    for (int i = 0; i < 16; i++)
    {
        saved[i] = Load32(base, director + kDirectorView + i * 4);
        viewM[i] = LoadF(base, director + kDirectorView + i * 4);
    }
    const float fovSaved = LoadF(base, director + kDirectorFov);
    PoseView(viewM, pose, posed);
    static const float margin = [] {
        const char* m = getenv("FM3_VR_FOV_MARGIN");
        return (m && *m ? float(atof(m)) : 5.0f) * 3.14159265f / 180.0f;
    }();
    const float half = std::fmax(pose.fovUp, -pose.fovDown);
    const float fov = half > 0.1f ? 2.0f * half + margin : fovSaved;
    for (int i = 0; i < 16; i++)
        StoreF(base, director + kDirectorView + i * 4, posed[i]);
    StoreF(base, director + kDirectorFov, fov);
    __imp__sub_821E9B50(ctx, base);
    for (int i = 0; i < 16; i++)
    {
        const uint32_t be = _byteswap_ulong(saved[i]);
        memcpy(base + director + kDirectorView + i * 4, &be, 4);
    }
    StoreF(base, director + kDirectorFov, fovSaved);
    g_framePose = pose;
    g_frameFov = fov;
    g_framePosed = true;
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        Log("vr: cockpit head tracking on (director %08X, camera vtable %08X, fov %.1f deg -> %.1f deg)", director, vtable,
            fovSaved * 57.2958f, fov * 57.2958f);
    }
}
