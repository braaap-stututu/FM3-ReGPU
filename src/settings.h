#pragma once
#include <cstdint>
#include <map>
#include <string>

// Player settings: Documents\fm3\settings.ini (FM3_USER_DIR overrides the folder). Environment
// variables (FM3_RESOLUTION, FM3_RENDER_SCALE, FM3_MAX_FPS, FM3_VSYNC, FM3_FULLSCREEN,
// FM3_PROFILE, FM3_SKIP_INTRO, FM3_QUICK_RACE) override the file for testing.
struct Settings
{
    std::string profile = "Player";     // the gamer profile name the game sees
    uint32_t width = 1280, height = 720; // output resolution
    uint32_t aspectW = 0, aspectH = 0;   // forced aspect ratio (0 = from the resolution)
    bool fullscreen = false;             // borderless fullscreen on the primary display
    bool vsync = true;
    uint32_t maxFps = 60;                // frame cap, 30-360 (the physics tick rate)
    bool enhance = false;                // enhanced lighting post pass (experimental)
    bool skipIntro = false;              // straight to the title screen (no legal text or splash videos)
    bool quickRace = false;              // Free Play > Quick Race by itself after boot (FM3_TOUR route; implies skipIntro)
    bool gridTest = false;               // the reference career race's grid by itself, world frozen there (hooks/grid_test.cpp; implies skipIntro)
    uint32_t language = 1;               // the console language the game sees (XGetLanguage: 1 English, 3 German, ...)
    uint32_t msaa = 1;                   // anti-aliasing samples per pixel: 1 (off), 2, 4 or 8
    // Anti-aliasing filters (session 36, ReGPU's postaa): aa = a filter on the finished 3D scene before the game's own
    // post-processing and HUD (the numbers match regpu::Config::PostAA); ssaa = supersampling, the frame drawn at this
    // multiple of the shown size and filtered down (1 off, 1.5, 2; not with an upscaler); eqaa = AMD's enhanced quality
    // AA with MSAA, where the driver offers it.
    enum class PostAA : uint32_t { Off, Fxaa, Smaa, Cmaa2, Taa };
    PostAA postAA = PostAA::Off;
    float ssaa = 1.0f;
    bool eqaa = false;
    // Upscaling: the 3D scene renders smaller and is upscaled before post-processing and
    // the HUD, which stay at full resolution.
    enum class Upscaler : uint32_t { Off, Fsr1, Fsr3, Dlss, Fsr4, Bilinear }; // (Fsr4, Bilinear last: the numbers match regpu::Config::Upscaler)
    Upscaler upscaler = Upscaler::Off;
    uint32_t quality = 1;   // 0 native (anti-aliasing only), 1 quality, 2 balanced, 3 performance, 4 ultra performance
    float sharpness = 0.5f; // 0-1
    // The scene's resolution as a fraction of the output (1 when upscaling is off).
    float SceneScale() const;

    // The size the game renders at: the resolution, narrowed or shortened to the forced
    // aspect ratio (the rest of the screen is black bars).
    void RenderSize(uint32_t& w, uint32_t& h) const;

    // Every other key of the file as it is (the launcher's own keys, the racing wheel's wheel_*
    // keys, src/input/wheel/wheel_config.h); kept when the file is saved.
    std::map<std::string, std::string> extra;
    std::string Extra(const char* key) const
    {
        auto it = extra.find(key);
        return it == extra.end() ? std::string() : it->second;
    }
};

// Loaded on first use (after RuntimeInit has set the user directory).
Settings& GetSettings();
void SaveSettings();
