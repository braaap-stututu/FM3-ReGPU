// The first-boot shader compile screen (session 39, user's design): a picture as the background and a rev counter in
// the style of the first-generation Audi R8's (black face, chrome ring, 0-9 x1000/min, red from 8,250) whose needle is
// the compile's progress, climbing to the rev limiter, with the compiled-shader count inside the dial. Our own drawing
// (GDI+, built into Windows); no maker's logo or typeface.
//
// The background is loaded from <user>\boot\background.png (or .jpg: the player's own picture), else from boot\ beside
// fm3.exe (the one the installer ships); without one the counter is drawn on black. The background.txt beside it: <user>\boot\background.txt "logo x0 y0 x1 y1" moves that
// part of the picture so it is centred over the dial; "clear x0 y0 x1 y1" paints a part black; "plate ..." draws a
// number plate (DrawPlate).
// FM3_BOOT_SCREEN_TEST=<folder>: at start, the screen at 0, 45 and 100 % written there as PNGs (to look at it without a
// first boot).
#include "boot_screen.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include <objidl.h>
namespace Gdiplus
{
using std::max;
using std::min;
}
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "kernel/runtime.h"
#include "log.h"

namespace
{
using namespace Gdiplus;
constexpr int kW = 1280, kH = 720;
constexpr float kPi = 3.14159265f;
constexpr float kRedline = 8.25f, kMaxRpm = 9.0f;

struct Screen
{
    bool ok = false;
    std::unique_ptr<Bitmap> base; // background + the dial's fixed parts
    float cx = 0, cy = 0, r = 0;
    float shown = 0; // the needle's value (x1000/min), eased toward the progress
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};
std::mutex g_mutex;
Screen g_screen;

// Clockwise from 0 at the lower left to 9 at the lower right (270 degrees), GDI+ degrees (0 = +x, clockwise).
float AngleDeg(float rpm) { return 135.0f + rpm / kMaxRpm * 270.0f; }
PointF Polar(float cx, float cy, float radius, float deg)
{
    const float a = deg * kPi / 180.0f;
    return PointF(cx + radius * cosf(a), cy + radius * sinf(a));
}

void DrawCentred(Graphics& g, const wchar_t* text, const Font& font, const Brush& brush, float x, float y)
{
    StringFormat f;
    f.SetAlignment(StringAlignmentCenter);
    f.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text, -1, &font, PointF(x, y), &f, &brush);
}

// The folder the picture comes from: the user folder's boot\ when it has one, else boot\ beside fm3.exe (shipped by
// the installer).
std::filesystem::path BootDir()
{
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        for (const char* name : { "background.png", "background.jpg", "background.jpeg" })
            if (std::filesystem::exists(UserDir() / "boot" / name, ec))
                return UserDir() / "boot";
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        return std::filesystem::path(exe).parent_path() / "boot";
    }();
    return dir;
}

std::unique_ptr<Bitmap> LoadBackground()
{
    for (const char* name : { "background.png", "background.jpg", "background.jpeg" })
    {
        const std::filesystem::path p = BootDir() / name;
        std::error_code ec;
        if (!std::filesystem::exists(p, ec))
            continue;
        auto b = std::make_unique<Bitmap>(p.c_str());
        if (b->GetLastStatus() == Ok && b->GetWidth() > 0)
        {
            Log("boot screen: background %s (%ux%u)", p.string().c_str(), b->GetWidth(), b->GetHeight());
            return b;
        }
    }
    return nullptr;
}

const wchar_t* FontFamilyName()
{
    static const wchar_t* name = [] {
        FontFamily f(L"Bahnschrift");
        return f.IsAvailable() ? L"Bahnschrift" : L"Segoe UI";
    }();
    return name;
}

// A German number plate (EU size, 520 x 110 mm) with `text`, drawn flat and mapped onto the parallelogram `to` (top
// left, top right, bottom left), lit like the car around it. After a photo of a 2007 R8's plate (session 39): white,
// a thin black border inset from the edge, the blue band (12 stars, "D"), narrow DIN-style letters (Bahnschrift
// Condensed = DIN 1451, the face German plates used before FE-Schrift) pressed in, the plate lamps from above.
void DrawPlate(Graphics& g, const PointF (&to)[3], const char* text)
{
    constexpr int pw = 1040, ph = 220; // 2 px a millimetre
    Bitmap plate(pw, ph, PixelFormat32bppARGB);
    {
        Graphics pg(&plate);
        pg.SetSmoothingMode(SmoothingModeAntiAlias);
        pg.SetTextRenderingHint(TextRenderingHintAntiAlias);
        pg.Clear(Color(0, 0, 0, 0));
        auto rounded = [](GraphicsPath& path, float x, float y, float w, float h, float rad) {
            path.AddArc(x, y, rad * 2, rad * 2, 180, 90);
            path.AddArc(x + w - rad * 2, y, rad * 2, rad * 2, 270, 90);
            path.AddArc(x + w - rad * 2, y + h - rad * 2, rad * 2, rad * 2, 0, 90);
            path.AddArc(x, y + h - rad * 2, rad * 2, rad * 2, 90, 90);
            path.CloseFigure();
        };
        // The plate: white aluminium, a slight curve of light from the lamps above.
        GraphicsPath body;
        rounded(body, 1, 1, pw - 2, ph - 2, 10);
        LinearGradientBrush white(PointF(0, 0), PointF(0, float(ph)), Color(255, 252, 252, 250), Color(255, 226, 226, 222));
        pg.FillPath(&white, &body);
        // The blue band, inside the border, its left corners rounded with it.
        const float inset = 10, bandW = 92;
        GraphicsPath band;
        band.AddArc(inset + 2, inset + 2, 16.0f, 16.0f, 180.0f, 90.0f);
        band.AddLine(inset + 10, inset + 2, inset + bandW, inset + 2);
        band.AddLine(inset + bandW, inset + 2, inset + bandW, ph - inset - 2);
        band.AddLine(inset + bandW, ph - inset - 2, inset + 10, ph - inset - 2);
        band.AddArc(inset + 2, float(ph) - inset - 18, 16.0f, 16.0f, 90.0f, 90.0f);
        band.CloseFigure();
        LinearGradientBrush blue(PointF(0, inset), PointF(0, ph - inset), Color(255, 20, 64, 168), Color(255, 10, 44, 132));
        pg.FillPath(&blue, &band);
        const float bcx = inset + 2 + (bandW - 2) * 0.5f, bcy = 70;
        SolidBrush star(Color(255, 255, 206, 0));
        for (int i = 0; i < 12; i++)
        {
            const float a = i * 2 * kPi / 12;
            const float x = bcx + 25 * cosf(a), y = bcy + 25 * sinf(a);
            PointF pts[10];
            for (int k = 0; k < 10; k++)
            {
                const float rr = k % 2 ? 2.4f : 5.6f, aa = -kPi / 2 + k * kPi / 5;
                pts[k] = PointF(x + rr * cosf(aa), y + rr * sinf(aa));
            }
            pg.FillPolygon(&star, pts, 10);
        }
        StringFormat centre;
        centre.SetAlignment(StringAlignmentCenter);
        centre.SetLineAlignment(StringAlignmentCenter);
        FontFamily cond(L"Bahnschrift Condensed");
        FontFamily& face = cond.IsAvailable() ? cond : *new FontFamily(L"Arial Narrow");
        Font code(&face, 58, FontStyleRegular, UnitPixel);
        SolidBrush whiteText(Color(255, 255, 255, 255));
        pg.DrawString(L"D", -1, &code, PointF(bcx, 160), &centre, &whiteText);
        // The border: a thin black line inset from the edge, rounded.
        GraphicsPath border;
        rounded(border, inset, inset, pw - inset * 2, ph - inset * 2, 8);
        Pen edge(Color(255, 20, 20, 22), 5.0f);
        pg.DrawPath(&edge, &border);
        // The letters: tall and narrow, pressed into the plate (light above, shadow below).
        wchar_t wide[64] = {};
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 63);
        Font letters(&face, 180, FontStyleRegular, UnitPixel);
        const float tx = inset + bandW + (pw - inset * 2 - bandW) * 0.5f, ty = ph * 0.52f;
        SolidBrush lift(Color(170, 255, 255, 255)), drop(Color(90, 0, 0, 0)), ink(Color(255, 18, 18, 20));
        pg.DrawString(wide, -1, &letters, PointF(tx - 1.5f, ty - 2.5f), &centre, &lift);
        pg.DrawString(wide, -1, &letters, PointF(tx + 2.0f, ty + 3.0f), &centre, &drop);
        pg.DrawString(wide, -1, &letters, PointF(tx, ty), &centre, &ink);
        // A faint sheen across the top half.
        LinearGradientBrush sheen(PointF(0, 0), PointF(0, ph * 0.55f), Color(46, 255, 255, 255), Color(0, 255, 255, 255));
        pg.FillRectangle(&sheen, RectF(0, 0, float(pw), ph * 0.55f));
        if (&face != &cond)
            delete &face;
    }
    // Onto the car, then the light of the recess it sits in: the lamps above it, the shadow below.
    g.DrawImage(&plate, to, 3);
    const PointF br(to[1].X + to[2].X - to[0].X, to[1].Y + to[2].Y - to[0].Y);
    PointF quad[4] = { to[0], to[1], br, to[2] };
    LinearGradientBrush shade(to[0], to[2], Color(40, 12, 10, 8), Color(120, 6, 6, 8));
    g.FillPolygon(&shade, quad, 4);
    // Its own shadow on the panel just below, and a thin highlight on its top edge.
    Pen under(Color(110, 0, 0, 0), 2.0f);
    g.DrawLine(&under, PointF(to[2].X + 1, to[2].Y + 1.5f), PointF(br.X + 1, br.Y + 1.5f));
    Pen top(Color(90, 255, 255, 255), 1.0f);
    g.DrawLine(&top, to[0], to[1]);
}

// The background and the dial's fixed parts (ring, face, ticks, numerals, red zone), drawn once.
void BuildBase(Screen& s)
{
    s.base = std::make_unique<Bitmap>(kW, kH, PixelFormat32bppARGB);
    Graphics g(s.base.get());
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Color(255, 0, 0, 0));
    std::unique_ptr<Bitmap> bg = LoadBackground();
    if (bg)
    {
        // Cover the screen, centred.
        const float sx = float(kW) / float(bg->GetWidth()), sy = float(kH) / float(bg->GetHeight()), k = std::max(sx, sy);
        const float w = float(bg->GetWidth()) * k, h = float(bg->GetHeight()) * k;
        const float ox = (kW - w) * 0.5f, oy = (kH - h) * 0.5f;
        g.DrawImage(bg.get(), RectF(ox, oy, w, h));
        // The dial in the dark area below the title (the key art's right side).
        s.cx = 1030, s.cy = 500, s.r = 172;
        // <user>\boot\background.txt "logo x0 y0 x1 y1" (picture pixels): that part of the picture (a title or logo)
        // moved sideways so it is centred over the dial; its old place is filled with black.
        // "plate x0 y0 x1 y1 x2 y2 TEXT" (picture pixels: the plate's top left, top right, bottom left corners): a number
        // plate with TEXT drawn there, in the picture's light (session 39: "ReGPU" on the key art's R8).
        if (FILE* f = _wfopen((BootDir() / "background.txt").c_str(), L"r"))
        {
            char line[256];
            while (fgets(line, sizeof(line), f))
            {
                float q[6];
                char text[64] = {};
                // "clear x0 y0 x1 y1" (picture pixels): that part painted black (e.g. a picture that already shows a
                // dial where the live one goes).
                if (sscanf_s(line, " clear %f %f %f %f", &q[0], &q[1], &q[2], &q[3]) == 4 && q[2] > q[0] && q[3] > q[1])
                {
                    SolidBrush black(Color(255, 0, 0, 0));
                    g.FillRectangle(&black, RectF(ox + q[0] * k, oy + q[1] * k, (q[2] - q[0]) * k, (q[3] - q[1]) * k));
                    continue;
                }
                if (sscanf_s(line, " plate %f %f %f %f %f %f %63s", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], text, unsigned(sizeof(text))) == 7)
                {
                    const PointF to[3] = { PointF(ox + q[0] * k, oy + q[1] * k), PointF(ox + q[2] * k, oy + q[3] * k), PointF(ox + q[4] * k, oy + q[5] * k) };
                    DrawPlate(g, to, text);
                }
            }
            fclose(f);
        }
        if (FILE* f = _wfopen((BootDir() / "background.txt").c_str(), L"r"))
        {
            float lx0, ly0, lx1, ly1;
            if (fscanf_s(f, " logo %f %f %f %f", &lx0, &ly0, &lx1, &ly1) == 4 && lx1 > lx0 && ly1 > ly0)
            {
                const float sx0 = ox + lx0 * k, sy0 = oy + ly0 * k, sw = (lx1 - lx0) * k, sh = (ly1 - ly0) * k;
                const float dx = s.cx - (sx0 + sw * 0.5f);
                SolidBrush black(Color(255, 0, 0, 0));
                g.FillRectangle(&black, RectF(sx0, sy0, sw, sh));
                g.DrawImage(bg.get(), RectF(sx0 + dx, sy0, sw, sh), lx0, ly0, lx1 - lx0, ly1 - ly0, UnitPixel);
                Log("boot screen: picture area %.0f,%.0f-%.0f,%.0f moved %.0f px to centre it over the dial", lx0, ly0, lx1, ly1, dx);
            }
            fclose(f);
        }
    }
    else
        s.cx = kW * 0.5f, s.cy = kH * 0.5f, s.r = 230;
    const float cx = s.cx, cy = s.cy, r = s.r;
    // Shadow, then the chrome ring (light from above), then the face.
    {
        SolidBrush shadow(Color(110, 0, 0, 0));
        g.FillEllipse(&shadow, cx - r - 22, cy - r - 14, (r + 22) * 2, (r + 22) * 2 + 16);
        LinearGradientBrush chrome(PointF(cx, cy - r - 16), PointF(cx, cy + r + 16), Color(255, 235, 238, 242), Color(255, 70, 72, 76));
        const Color stops[] = { Color(255, 245, 247, 250), Color(255, 150, 153, 158), Color(255, 220, 223, 228), Color(255, 60, 62, 66) };
        const REAL pos[] = { 0.0f, 0.45f, 0.62f, 1.0f };
        chrome.SetInterpolationColors(stops, pos, 4);
        g.FillEllipse(&chrome, cx - r - 16, cy - r - 16, (r + 16) * 2, (r + 16) * 2);
        SolidBrush rim(Color(255, 18, 18, 20));
        g.FillEllipse(&rim, cx - r - 3, cy - r - 3, (r + 3) * 2, (r + 3) * 2);
        GraphicsPath face;
        face.AddEllipse(cx - r, cy - r, r * 2, r * 2);
        PathGradientBrush faceBrush(&face);
        faceBrush.SetCenterPoint(PointF(cx, cy - r * 0.35f));
        faceBrush.SetCenterColor(Color(255, 34, 35, 38));
        Color edge(255, 6, 6, 7);
        INT n = 1;
        faceBrush.SetSurroundColors(&edge, &n);
        g.FillPath(&faceBrush, &face);
    }
    // The red zone, 8,250 to 9,000.
    {
        Pen red(Color(255, 214, 26, 22), r * 0.075f);
        const float rr = r * 0.90f;
        g.DrawArc(&red, cx - rr, cy - rr, rr * 2, rr * 2, AngleDeg(kRedline), AngleDeg(kMaxRpm) - AngleDeg(kRedline));
    }
    // Ticks: every 250 (short), 500 (medium), 1,000 (long, numbered).
    {
        Pen minor(Color(255, 225, 225, 225), 1.6f), medium(Color(255, 235, 235, 235), 2.4f), major(Color(255, 250, 250, 250), 4.0f);
        for (int i = 0; i <= 36; i++)
        {
            const float rpm = i * 0.25f, a = AngleDeg(rpm);
            const bool isMajor = i % 4 == 0, isMedium = i % 2 == 0;
            const float outer = r * 0.955f, inner = outer - r * (isMajor ? 0.13f : isMedium ? 0.085f : 0.055f);
            Pen& p = isMajor ? major : isMedium ? medium : minor;
            if (rpm >= kRedline)
            {
                Pen hot(Color(255, 255, 90, 80), p.GetWidth());
                g.DrawLine(&hot, Polar(cx, cy, inner, a), Polar(cx, cy, outer, a));
            }
            else
                g.DrawLine(&p, Polar(cx, cy, inner, a), Polar(cx, cy, outer, a));
        }
        FontFamily family(FontFamilyName());
        Font numerals(&family, r * 0.155f, FontStyleBold, UnitPixel);
        SolidBrush white(Color(255, 245, 245, 245)), hot(Color(255, 255, 80, 70));
        for (int v = 0; v <= 9; v++)
        {
            wchar_t t[4];
            swprintf(t, 4, L"%d", v);
            const PointF p = Polar(cx, cy, r * 0.68f, AngleDeg(float(v)));
            DrawCentred(g, t, numerals, v >= 9 ? static_cast<Brush&>(hot) : static_cast<Brush&>(white), p.X, p.Y);
        }
        Font unit(&family, r * 0.062f, FontStyleRegular, UnitPixel);
        SolidBrush grey(Color(255, 150, 150, 150));
        DrawCentred(g, L"1/min x1000", unit, grey, cx, cy - r * 0.36f);
    }
}

bool EnsureGdiplus()
{
    static const bool ok = [] {
        GdiplusStartupInput in;
        ULONG_PTR token = 0;
        return GdiplusStartup(&token, &in, nullptr) == Ok;
    }();
    return ok;
}

// The whole screen for this moment into a 1280x720 RGBA8 buffer.
bool Compose(const wchar_t* title, uint32_t done, uint32_t total, std::vector<uint8_t>& rgba, bool animate)
{
    std::lock_guard lock(g_mutex);
    Screen& s = g_screen;
    if (!EnsureGdiplus())
        return false;
    if (!s.base)
        BuildBase(s);
    const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - s.start).count();
    // The needle: the progress up to the redline; idling (a slight wobble) while the shaders are still being found;
    // bouncing off the limiter once done.
    const float p = total ? std::min(1.0f, float(done) / float(total)) : 0.0f;
    float target = total ? p * kRedline : 0.85f + 0.05f * sinf(t * 7.0f);
    if (total && done >= total)
        target = kRedline - 0.18f * fabsf(sinf(t * 28.0f));
    s.shown = animate ? s.shown + (target - s.shown) * 0.3f : target;
    const float cx = s.cx, cy = s.cy, r = s.r;

    Bitmap frame(kW, kH, PixelFormat32bppARGB);
    {
        Graphics g(&frame);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.DrawImage(s.base.get(), 0, 0, kW, kH);
        FontFamily family(FontFamilyName());
        // Shift light at the top of the dial: on near the limiter.
        {
            const bool on = s.shown >= kRedline * 0.97f;
            SolidBrush led(on ? Color(255, 255, 40, 30) : Color(255, 60, 14, 12));
            const float lr = r * 0.045f, ly = cy - r * 0.52f;
            if (on)
            {
                SolidBrush glow(Color(90, 255, 40, 30));
                g.FillEllipse(&glow, cx - lr * 2.6f, ly - lr * 2.6f, lr * 5.2f, lr * 5.2f);
            }
            g.FillEllipse(&led, cx - lr, ly - lr, lr * 2, lr * 2);
        }
        // The count inside the dial, and what is going on.
        {
            Font big(&family, r * 0.25f, FontStyleBold, UnitPixel);
            Font label(&family, r * 0.075f, FontStyleRegular, UnitPixel);
            SolidBrush white(Color(255, 250, 250, 250)), grey(Color(255, 165, 165, 165));
            wchar_t count[32], of[48];
            swprintf(count, 32, L"%u", done);
            if (total)
                swprintf(of, 48, L"of %u shaders", total);
            else
                swprintf(of, 48, L"finding shaders");
            DrawCentred(g, count, big, white, cx, cy + r * 0.40f);
            DrawCentred(g, of, label, grey, cx, cy + r * 0.62f);
            DrawCentred(g, title, label, grey, cx, cy + r * 0.80f);
        }
        // The needle: tapered, red, with a soft glow; the hub over it.
        {
            const float a = AngleDeg(s.shown), len = r * 0.93f, back = r * 0.16f;
            const float rad = a * kPi / 180.0f, nx = cosf(rad), ny = sinf(rad), px = -ny, py = nx;
            const PointF tip(cx + nx * len, cy + ny * len), tail(cx - nx * back, cy - ny * back);
            PointF needle[4] = { PointF(tail.X + px * r * 0.032f, tail.Y + py * r * 0.032f), PointF(tip.X + px * 1.2f, tip.Y + py * 1.2f),
                                 PointF(tip.X - px * 1.2f, tip.Y - py * 1.2f), PointF(tail.X - px * r * 0.032f, tail.Y - py * r * 0.032f) };
            Pen glow(Color(70, 255, 60, 40), r * 0.05f);
            glow.SetStartCap(LineCapRound);
            glow.SetEndCap(LineCapRound);
            g.DrawLine(&glow, tail, tip);
            SolidBrush red(Color(255, 236, 40, 28));
            g.FillPolygon(&red, needle, 4);
            LinearGradientBrush hub(PointF(cx, cy - r * 0.11f), PointF(cx, cy + r * 0.11f), Color(255, 200, 202, 206), Color(255, 40, 41, 44));
            g.FillEllipse(&hub, cx - r * 0.11f, cy - r * 0.11f, r * 0.22f, r * 0.22f);
            SolidBrush cap(Color(255, 14, 14, 15));
            g.FillEllipse(&cap, cx - r * 0.08f, cy - r * 0.08f, r * 0.16f, r * 0.16f);
        }
    }
    BitmapData data{};
    Rect all(0, 0, kW, kH);
    if (frame.LockBits(&all, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok)
        return false;
    rgba.resize(size_t(kW) * kH * 4);
    for (int y = 0; y < kH; y++)
    {
        const uint8_t* src = static_cast<const uint8_t*>(data.Scan0) + size_t(y) * data.Stride;
        uint8_t* dst = rgba.data() + size_t(y) * kW * 4;
        for (int x = 0; x < kW; x++)
        {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    frame.UnlockBits(&data);
    return true;
}

bool SavePng(const std::vector<uint8_t>& rgba, const std::filesystem::path& path)
{
    UINT n = 0, size = 0;
    GetImageEncodersSize(&n, &size);
    if (!size)
        return false;
    std::vector<uint8_t> buf(size);
    auto* codecs = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(n, size, codecs);
    const CLSID* png = nullptr;
    for (UINT i = 0; i < n; i++)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
            png = &codecs[i].Clsid;
    if (!png)
        return false;
    Bitmap b(kW, kH, PixelFormat32bppARGB);
    BitmapData data{};
    Rect all(0, 0, kW, kH);
    b.LockBits(&all, ImageLockModeWrite, PixelFormat32bppARGB, &data);
    for (int y = 0; y < kH; y++)
    {
        uint8_t* dst = static_cast<uint8_t*>(data.Scan0) + size_t(y) * data.Stride;
        const uint8_t* src = rgba.data() + size_t(y) * kW * 4;
        for (int x = 0; x < kW; x++)
        {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    b.UnlockBits(&data);
    return b.Save(path.c_str(), png) == Ok;
}
} // namespace

bool BootScreen(const wchar_t* title, uint32_t done, uint32_t total, std::vector<uint8_t>& rgba)
{
    static std::wstring lastTitle;
    if (lastTitle != title)
    {
        lastTitle = title;
        char t[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, title, -1, t, sizeof(t) - 1, nullptr, nullptr);
        Log("boot screen: %s (%u of %u)", t, done, total);
    }
    return Compose(title, done, total, rgba, true);
}

void BootScreenTest()
{
    const char* dir = getenv("FM3_BOOT_SCREEN_TEST");
    if (!dir || !*dir)
        return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::vector<uint8_t> rgba;
    const struct
    {
        const char* name;
        uint32_t done, total;
    } shots[] = { { "boot_0.png", 0, 0 }, { "boot_45.png", 1473, 3274 }, { "boot_100.png", 3274, 3274 } };
    for (const auto& s : shots)
        if (Compose(L"COMPILING SHADERS", s.done, s.total, rgba, false))
            Log("boot screen test: %s %s", s.name, SavePng(rgba, std::filesystem::path(dir) / s.name) ? "written" : "failed");
}
