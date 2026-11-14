// The player's car driven by FM3's own race AI (session 33, for automated benchmarks; RESEARCH "Benchmark tools").
// FM3_AI_PLAYER=1 (read when a race is built; tools/benchmark.ps1 sets it): the player's car is built exactly as an
// opponent's, so the race AI drives it for the whole race, while the HUD, camera, position and results still follow it
// as the player's. FM3_AI_PLAYER_LOG=1 logs each car built and every control-state change.
//
// How FM3 gives a car its driver (classes found with tools/analysis/rtti.py: RacingAI::CAICar (vtable 0x82001470),
// CHumanDriver, CAIDriver, CMetaAIDriver, CAIDrivatar, CCrashRecoveryDriver): sub_828F8858(car, params) runs once per
// car as a race is built, car = the CAICar, params = its race entry (params[0]: 3 the player, 2 an opponent).
// - car+104 == 0 (the player's car): a CHumanDriver (28 bytes, ctor sub_82904A08) is made and installed as the active
//   driver (sub_828F8178: car+2280). If the byte at [0x82C21130]+8 is set (it is in career races), an AI driver is made
//   too and kept in reserve (sub_828F81C8: car+2284; for the AI's drive after the finish, presumably).
// - Opponents (car+104 == 2): four calls on the car's physics (car+2064, CCarDynamics: vtable +612 (1), +620
//   (params+1621), +700 (0), +624 (1)), then an AI driver from the factory sub_828F7D68(car, params) (meta AI /
//   Drivatar / crash recovery by params[0]), its +104 set to (params[0] == 3), installed through its second base.
// What did not work (session 33): installing an AI driver from the factory as the player's active driver after the
// game built the car (built, but the car did not move: the pad still drove it); also with the four physics calls; the
// car built with +104 = 1 and params[0] = 3 (the AI revved on the line and never launched: the factory makes another,
// half set-up driver for type 3). Built with both as an opponent's (+104 = 2, params[0] = 2) the AI races normally.
// The race's start sends no control-state change (CAICar slot 4, sub_828F8280: car+2288) to any car.
// Other facts on the way: each physics step copies 28 bytes of controls (sub_822163A8: memcpy(obj+5144, obj+5808));
// in the player's CCarDynamics +2744 = throttle, +2748 = brake (0..1).
#include <cstdlib>
#include <cstring>

#include "kernel/imports.h"
#include "kernel/thread.h"
#include "log.h"

namespace
{
bool AiPlayerOn()
{
    const char* v = getenv("FM3_AI_PLAYER");
    return v && *v && strcmp(v, "0") != 0;
}
} // namespace

// CAICar's control-state switch (logged only).
PPC_FUNC_IMPL(__imp__sub_828F8280);
PPC_FUNC(sub_828F8280)
{
    const uint32_t car = ctx.r3.u32, state = ctx.r4.u32, before = LoadBE32(car + 2288);
    __imp__sub_828F8280(ctx, base);
    if (getenv("FM3_AI_PLAYER_LOG"))
        Log("ai player: car %08X state %u -> asked %u, now %u, result %u", car, before, state, LoadBE32(car + 2288), ctx.r3.u32 & 0xFF);
}

PPC_FUNC_IMPL(__imp__sub_828F8858);
PPC_FUNC(sub_828F8858)
{
    const uint32_t car = ctx.r3.u32, params = ctx.r4.u32;
    const uint32_t flag = LoadBE32(car + 104), type = LoadBE32(params);
    if (getenv("FM3_AI_PLAYER_LOG"))
        Log("ai player: building car %08X: +104 %u, type %u", car, flag, type);
    if (flag != 0 || !AiPlayerOn())
    {
        __imp__sub_828F8858(ctx, base);
        return;
    }
    // Built as an opponent's car, then marked the player's again.
    StoreBE32(car + 104, 2);
    StoreBE32(params, 2);
    __imp__sub_828F8858(ctx, base);
    StoreBE32(params, type);
    StoreBE32(car + 104, 0);
    Log("ai player: the player's car %08X is driven by the race AI (driver %08X)", car, LoadBE32(car + 2280));
}
