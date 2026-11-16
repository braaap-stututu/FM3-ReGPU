#pragma once
#include <cstdint>
#include <vector>

// The first-boot shader compile screen (regpu::Host::bootScreen): a 1280x720 RGBA8 picture for the phase `title` with
// `done` of `total` shaders (total 0 while they are still being found). See boot_screen.cpp.
bool BootScreen(const wchar_t* title, uint32_t done, uint32_t total, std::vector<uint8_t>& rgba);
// FM3_BOOT_SCREEN_TEST=<folder>: the screen at a few progress points written there as PNGs.
void BootScreenTest();
