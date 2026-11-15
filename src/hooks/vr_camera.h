#pragma once

// VR head tracking in the cockpit (session 37, src/hooks/vr_camera.cpp). Called once per main-loop frame
// (src/hooks/frame_time.cpp): hands the frame's head pose (or none) to ReGPU, in frame order.
void VrCameraMainFrame();
