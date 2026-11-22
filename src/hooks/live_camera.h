#pragma once

#include <cstdint>

// Live camera stream for the renderer replacement project (2026-11-23, stage A "live mirror"; Godot\frame-ir
// ARCHITECTURE.md M5, LIVE.md there). Called at every camera hand-off (sub_821E9B50, src/hooks/vr_camera.cpp) with
// the presentation view (r3): sends that view's director camera (view matrix +0xD0, field of view +0x110, active
// camera) as one UDP packet when FM3_LIVE is set. Packet layout: src/hooks/live_camera.cpp.
void LiveCameraHandOff(uint8_t* base, uint32_t view);
