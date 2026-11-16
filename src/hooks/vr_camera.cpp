// VR head tracking in the cockpit (session 37; the scheme Forza Horizon 1: ReImagined describes for its engine, FM3's
// successor; our own code and FM3's own addresses).
//
// FM3's player view has a camera director (found by the CCamDriver object it creates in sub_8246DF78, read from guest
// memory on the race grid; FH1's layout with every field 32 bytes earlier):
//   +0x08 the view the player picked (0-4; RB held cycles them)
//   +0x0C the active camera (Camera::ICamera; CCamDriver vtable 0x8200206C = the in-car view, CCamHood 0x82001FF4, ...);
//         +0x10/+0x14 earlier ones (session 37 took +0x14 for the active one: on the grid it is the chase camera
//         either way; session 38 found +0x0C by cycling the views in a running race)
//   +0x18 ... the player's cameras
//   +0x90 camera position in the world (x, y, z)    +0xB0 forward    +0xC0 up
//   +0xD0 the view matrix: 4x4 floats, world to camera, row vectors, left-handed (x right, y up, z forward)
//   +0x110 vertical field of view, radians (the whole angle)
// The matrix is read through one accessor (sub_821E3990: r3 + 208), called only by sub_821E9B50, which hands the matrix
// and the field of view (sub_821ED758: +272) to the view's presentation camera (view + 4 = director, + 36 = camera).
// So for that call, with an in-car camera active, the head pose goes into the matrix and the headset's field of view
// into +0x110, and both are put back after: the renderer draws from the head, the camera's own state stays as it is.
//
// Settings: vr=cockpit (or FM3_VR / REGPU_VR) with ReGPU's VR on; vr=stereo the same, and ReGPU draws the main view
// once more for the second eye (session 38, ReGPU patch regpu-vr-stereo; eye sign: src/gpu_host.cpp). FM3_VR_FAKE="yaw pitch roll x y z" (degrees, cm;
// read twice a second) poses the camera without a headset, to check the transform on screenshots.
// FM3_VR_FOV_MARGIN (degrees, default 5) widens the rendered field of view past the headset's.
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "hooks/vr_camera.h"
#include "kernel/runtime.h"
#include "log.h"
#include "regpu/regpu.h"

#include <ppc_context.h>

namespace
{
constexpr uint32_t kDirectorView = 0xD0, kDirectorFov = 0x110, kDirectorActive = 0x0C;
// First-person cameras (vtables by RTTI name, tools/analysis/rtti.py CCam): the in-car view CCamDriver, CCamCockpit,
// the hood view CCamHood and the two bumper views.
constexpr uint32_t kFirstPerson[] = { 0x8200206C, 0x82034FC4, 0x82001FF4, 0x82001F7C, 0x820020E4 };

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

// Seat adjustment (session 39): the eye point moved from where FM3 puts the driver's eyes, in centimetres in the
// headset's recentred space (x right, y up, z back as OpenXR has it; kept as forward here). Keys in the game window
// (numeric keypad, not used by the game's keyboard controls): 8/2 forward/back, 9/3 up/down, 4/6 left/right, 5 back
// to zero; 1 cm a press, repeating while held. Saved in <user>\vr\seat.txt ("x y forward", cm) and read at start.
struct Seat
{
    float x = 0, y = 0, forward = 0;
};
std::filesystem::path SeatFile() { return UserDir() / "vr" / "seat.txt"; }
Seat LoadSeat()
{
    Seat s;
    if (FILE* f = _wfopen(SeatFile().c_str(), L"r"))
    {
        if (fscanf_s(f, "%f %f %f", &s.x, &s.y, &s.forward) != 3)
            s = {};
        fclose(f);
    }
    return s;
}
void SaveSeat(const Seat& s)
{
    std::error_code ec;
    std::filesystem::create_directories(SeatFile().parent_path(), ec);
    if (FILE* f = _wfopen(SeatFile().c_str(), L"w"))
    {
        fprintf(f, "%.0f %.0f %.0f\n", s.x, s.y, s.forward);
        fclose(f);
    }
}
// The seat offset now (metres, OpenXR axes), after this frame's key presses.
void SeatOffset(float out[3])
{
    static Seat seat = [] {
        const Seat s = LoadSeat();
        if (s.x || s.y || s.forward)
            Log("vr: seat offset %.0f cm right, %.0f cm up, %.0f cm forward (%s)", s.x, s.y, s.forward, SeatFile().string().c_str());
        return s;
    }();
    static DWORD nextRepeat[7] = {};
    static bool wasDown[7] = {};
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId())
    {
        static const int keys[7] = { VK_NUMPAD8, VK_NUMPAD2, VK_NUMPAD9, VK_NUMPAD3, VK_NUMPAD6, VK_NUMPAD4, VK_NUMPAD5 };
        bool changed = false;
        const DWORD now = GetTickCount();
        for (int i = 0; i < 7; i++)
        {
            const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
            // A press acts at once, then every 100 ms after the first 400 while held.
            const bool act = down && (!wasDown[i] || int(now - nextRepeat[i]) >= 0);
            if (down && !wasDown[i])
                nextRepeat[i] = now + 400;
            else if (act)
                nextRepeat[i] = now + 100;
            wasDown[i] = down;
            if (!act)
                continue;
            changed = true;
            switch (i)
            {
            case 0: seat.forward += 1; break;
            case 1: seat.forward -= 1; break;
            case 2: seat.y += 1; break;
            case 3: seat.y -= 1; break;
            case 4: seat.x += 1; break;
            case 5: seat.x -= 1; break;
            default: seat = {}; break;
            }
            for (float* c : { &seat.x, &seat.y, &seat.forward })
                *c = std::fmax(-50.0f, std::fmin(50.0f, *c));
        }
        if (changed)
        {
            SaveSeat(seat);
            Log("vr: seat %.0f cm right, %.0f cm up, %.0f cm forward", seat.x, seat.y, seat.forward);
        }
    }
    out[0] = seat.x * 0.01f, out[1] = seat.y * 0.01f, out[2] = -seat.forward * 0.01f;
}

// The pose the current main frame's view was rendered with (handed to ReGPU at the next main frame).
regpu::vr::HeadPose g_framePose;
float g_frameFov = 0;
bool g_framePosed = false;
ULONGLONG g_lastPosedMs = 0; // when a head-tracked camera hand-off last ran
} // namespace

// Triple screen (session 39; ReGPU REGPU_TRIPLE: the main view drawn three times, turned per screen). The camera's
// field of view is widened for the hand-off so FM3's frustum (and its culling) covers all three screens; ReGPU narrows
// each pass back to one screen's field. Settings (src/gpu_host.cpp): FM3_TRIPLE_FOV = each screen's vertical field of
// view in degrees (0 = the camera's own, made smaller where three screens wouldn't fit one frustum: the frustum must
// stay under ~166 degrees across), FM3_TRIPLE_ANGLE = degrees between the centre screen's axis and a side screen's
// (0 = one screen's width: the pictures meet edge to edge), FM3_TRIPLE_BEZEL = degrees added for the bezels.
PPC_FUNC_IMPL(__imp__sub_821E9B50);

bool TripleOn()
{
    static const bool on = [] {
        const char* v = getenv("REGPU_TRIPLE");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return on;
}

void TripleHandOff(PPCContext& ctx, uint8_t* base)
{
    const uint32_t view = ctx.r3.u32;
    const uint32_t director = view ? Load32(base, view + 4) : 0;
    if (!director)
    {
        __imp__sub_821E9B50(ctx, base);
        return;
    }
    static const float d2r = 3.14159265f / 180.0f;
    static const float wantFov = float(atof(getenv("FM3_TRIPLE_FOV") ? getenv("FM3_TRIPLE_FOV") : "0")) * d2r;
    static const float wantAngle = float(atof(getenv("FM3_TRIPLE_ANGLE") ? getenv("FM3_TRIPLE_ANGLE") : "0")) * d2r;
    static const float bezel = float(atof(getenv("FM3_TRIPLE_BEZEL") ? getenv("FM3_TRIPLE_BEZEL") : "0")) * d2r;
    constexpr float aspect = 16.0f / 9.0f, maxHalf = 83.0f * 3.14159265f / 180.0f;
    const float fovSaved = LoadF(base, director + kDirectorFov);
    if (!(fovSaved > 0.05f && fovSaved < 3.0f))
    {
        __imp__sub_821E9B50(ctx, base);
        return;
    }
    // Each screen's vertical field (vs) and horizontal (hs); the turn to a side screen; the frustum's half width.
    float vs = wantFov > 0.05f ? wantFov : fovSaved;
    auto hFov = [&](float v) { return 2.0f * atanf(tanf(v * 0.5f) * aspect); };
    auto turnFor = [&](float v) { return (wantAngle > 0.0f ? wantAngle : hFov(v)) + bezel; };
    // Too wide for one frustum: the screens' field made smaller until the outer edges fit (bisection).
    if (turnFor(vs) + hFov(vs) * 0.5f > maxHalf)
    {
        float lo = 0.05f, hi = vs;
        for (int i = 0; i < 30; i++)
        {
            const float mid = 0.5f * (lo + hi);
            (turnFor(mid) + hFov(mid) * 0.5f > maxHalf ? hi : lo) = mid;
        }
        vs = lo;
    }
    const float angle = std::fmin(turnFor(vs), maxHalf - hFov(vs) * 0.5f);
    const float half = angle + hFov(vs) * 0.5f;
    const float gameFov = 2.0f * atanf(tanf(half) / aspect);
    StoreF(base, director + kDirectorFov, gameFov);
    __imp__sub_821E9B50(ctx, base);
    StoreF(base, director + kDirectorFov, fovSaved);
    regpu::triple::SubmitFrame(gameFov, vs, angle);
    static float logged = 0;
    if (std::fabs(logged - vs) > 0.01f)
    {
        logged = vs;
        Log("triple: screens %.1f deg high, %.1f deg wide, side screens turned %.1f deg (camera %.1f deg -> frustum %.1f deg high, %.1f deg "
            "across)",
            vs / d2r, hFov(vs) / d2r, angle / d2r, fovSaved / d2r, gameFov / d2r, 2.0f * half / d2r);
    }
}

void VrCameraMainFrame()
{
    if (!Enabled())
        return;
    // Head-tracked while a posed hand-off ran in the last 100 ms: FM3's logic runs ~1.7 times per drawn frame and not
    // every step hands the camera off, so "none this step" made every few frames flat (session 39: the view flickered
    // between stereo and the flat tracked picture).
    const bool posed = g_framePosed || GetTickCount64() - g_lastPosedMs < 100;
    regpu::vr::SubmitFramePose(posed ? &g_framePose : nullptr, g_frameFov);
    g_framePosed = false;
}

PPC_FUNC_IMPL(__imp__sub_821E9B50);
PPC_FUNC(sub_821E9B50)
{
    if (!Enabled())
    {
        if (TripleOn())
            TripleHandOff(ctx, base);
        else
            __imp__sub_821E9B50(ctx, base);
        return;
    }
    const uint32_t view = ctx.r3.u32;
    const uint32_t director = view ? Load32(base, view + 4) : 0;
    const uint32_t camera = director ? Load32(base, director + kDirectorActive) : 0;
    const uint32_t vtable = camera ? Load32(base, camera) : 0;
    {
        // Which camera each view shows, logged when it changes (to find the views' cameras: CCam* vtables by RTTI name,
        // tools/analysis/rtti.py CCam).
        static uint32_t lastVtable[4] = {}, lastDirector[4] = {};
        static int views = 0;
        int slot = 0;
        while (slot < views && lastDirector[slot] != director)
            slot++;
        if (slot == views && views < 4)
            lastDirector[views++] = director;
        if (slot < 4 && lastVtable[slot] != vtable)
        {
            lastVtable[slot] = vtable;
            Log("vr: view %d (director %08X) camera %08X vtable %08X", slot, director, camera, vtable);
        }
    }
    regpu::vr::HeadPose pose;
    bool inCar = false;
    for (uint32_t v : kFirstPerson)
        inCar |= vtable == v;
    if (!inCar || !(FakePose(pose) || regpu::vr::GetHeadPose(pose)))
    {
        __imp__sub_821E9B50(ctx, base);
        return;
    }
    // The eye stays within a box round the seat (FM3_VR_EYE_LIMIT, cm each way, default 30; 0 = no limit): leaning
    // further doesn't put the camera through the car's body. The frame keeps the limited pose (it is what was drawn).
    static const float limit = [] {
        const char* v = getenv("FM3_VR_EYE_LIMIT");
        return (v && *v ? float(atof(v)) : 30.0f) * 0.01f;
    }();
    if (limit > 0)
        for (float& c : pose.position)
            c = std::fmax(-limit, std::fmin(limit, c));
    // The seat adjustment moves the camera, not the head: the frame is drawn from head + seat, and handed to the headset
    // with the head's own pose (the runtime compares it with where the head is when shown).
    regpu::vr::HeadPose drawn = pose;
    {
        float seat[3];
        SeatOffset(seat);
        for (int i = 0; i < 3; i++)
            drawn.position[i] += seat[i];
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
    PoseView(viewM, drawn, posed);
    // The frame's pose tag (session 39): FM3's logic runs on the main thread about twice per drawn frame and the render
    // thread draws whichever is newest, so poses can't be paired with frames by counting (that showed as judder when
    // turning the head). The tag goes into the view matrix's unused element m03 (0 in a view matrix; a denormal there
    // changes nothing drawn), which FM3 uploads raw as vertex constants; ReGPU reads it back on the draw thread and
    // hands the headset each frame with the pose it was drawn with (regpu::vr::SubmitTaggedPose).
    static uint32_t tagCounter = 0;
    tagCounter = (tagCounter % 0xFFFF) + 1;
    {
        const uint32_t bits = regpu::vr::kPoseTagBits | tagCounter;
        memcpy(&posed[3], &bits, 4);
    }
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
    g_lastPosedMs = GetTickCount64();
    regpu::vr::SubmitTaggedPose(tagCounter, pose, fov);
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        Log("vr: cockpit head tracking on (director %08X, camera vtable %08X, fov %.1f deg -> %.1f deg)", director, vtable,
            fovSaved * 57.2958f, fov * 57.2958f);
    }
}
