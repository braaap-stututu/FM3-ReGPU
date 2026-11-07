#pragma once
// FM3 guest addresses the runtime hooks replace, from FM3's own default.xex v0.0.0.8 (they differ
// from FM2's and FM4's); each is a function the recompiled image defines as sub_XXXXXXXX.
// Checked with build/tools/ppcdis.exe (FM3-QUIRKS.md Q7; NOTES.md session 6).
//
// XAPI fibers: statically linked XDK code. The title's SwitchToFiber swaps the stack pointer and
// registers by hand, which recompiled code cannot follow (hooks/fibers.cpp replaces them with
// host fibers). Same object layout as FM4: CreateFiber allocates 0xA50 bytes (li r3,0xA50 at
// 82A7B190), the current fiber lives at KTHREAD+0x164 (lwz r11,356(r31) at 82A7B0B4).
//   ConvertThreadToFiber  82A7B098
//   ConvertFiberToThread  82A7B128
//   CreateFiber           82A7B178
//   DeleteFiber           82A7B258
//   SwitchToFiber         82A82210, the register swap itself (82A82210..82A8261C, no .pdata;
//                         config/fm3.toml gives it its own function). Callers reach it through
//                         the export thunk 82A7B2B8 (b 0x82A82210), which then lands in the hook.
//                         The start trampoline 82A82200 is replaced by FiberMain.
#define FM3_XAPI_FIBER_ADDRESSES
#define FM3_XAPI_CONVERT_THREAD_TO_FIBER sub_82A7B098
#define FM3_XAPI_CONVERT_FIBER_TO_THREAD sub_82A7B128
#define FM3_XAPI_CREATE_FIBER            sub_82A7B178
#define FM3_XAPI_DELETE_FIBER            sub_82A7B258
#define FM3_XAPI_SWITCH_TO_FIBER         sub_82A82210
