#pragma once

#include <cstdint>

// Forza "Data Out" telemetry over UDP (FM3_DATA_OUT / settings.ini data_out=; src/hooks/data_out.cpp): the player's
// car in Forza Motorsport 7's (or Horizon 4/5's) packet layout, for SimHub, dashboards and motion rigs.

// The player's car-physics object (the HUD's redline getter, src/hooks/mirror.cpp).
void DataOutPlayerPhysics(uint32_t physics);
// Player 1's steering input, -1..1 (src/kernel/imports_xam.cpp ReadPad).
void DataOutSteerInput(float steer);
// Each car the main view draws (src/hooks/mirror.cpp, the car loop): sends a packet when it is the player's.
void DataOutCarDrawn(uint8_t* base, uint32_t car);
