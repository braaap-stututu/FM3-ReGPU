#pragma once
#include <cstdint>

// The frame rate, the guest vblank rate and how many vblanks make a main-loop frame (1 or 2:
// mainthread30fps set) (src/gpu_host.cpp). At rates other than 60 the main loop's frame time
// comes from the vblank clock instead of the millisecond tick (src/hooks/frame_time.cpp).
void FrameTimeSetPacing(uint32_t fps, uint32_t vblankHz, uint32_t divider);
