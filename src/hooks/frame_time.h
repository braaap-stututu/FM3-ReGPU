#pragma once
#include <cstdint>

// The frame rate, the guest vblank rate and how many vblanks make a main-loop frame (1 or 2:
// mainthread30fps set) (src/gpu_host.cpp). At rates other than 60 the main loop's frame time
// comes from the vblank clock instead of the millisecond tick (src/hooks/frame_time.cpp).
void FrameTimeSetPacing(uint32_t fps, uint32_t vblankHz, uint32_t divider);

// Session 23: stop the world (main-loop dt 0; drawing goes on) from code, as FM3_FREEZE_TIME=1 does live
// (the test-grid route, src/hooks/grid_test.cpp). Main-loop frames counted so far, and the last one that
// took 250 ms or more (a load).
void FrameTimeFreeze(bool on);
uint64_t FrameTimeMainFrames();
uint64_t FrameTimeLastLongFrame();
