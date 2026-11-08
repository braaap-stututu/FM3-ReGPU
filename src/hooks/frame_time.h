#pragma once
#include <cstdint>

// The guest vblank rate (src/gpu_host.cpp): at rates other than 60 the main loop's frame time
// comes from the vblank clock instead of the millisecond tick (src/hooks/frame_time.cpp).
void FrameTimeSetHz(uint32_t hz);
