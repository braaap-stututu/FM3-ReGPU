#pragma once
#include <cstdint>

struct PPCContext;

// Triple screen (session 39, ReGPU REGPU_TRIPLE): see triple_screen.cpp.
bool TripleOn();
// The camera hand-off (sub_821E9B50) with FM3's frustum widened to cover all three screens, and the screens' views
// handed to ReGPU. `original` is the game's own hand-off.
void TripleHandOff(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*));
