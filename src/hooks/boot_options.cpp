// Boot options (session 17): skip the intro, and FM3's own developer command line.
//
// 1. Intro skip (launcher/settings.ini skip_intro=1, or FM3_SKIP_INTRO=1), after the FM2 port's
//    src/hooks/skip_intro.cpp. sub_8225A870 builds the boot sequence: up to 8 entries of 240 bytes
//    at r1+80 (entry +144 type, +152 start, +160 duration in seconds as a double, +172 the file, not
//    plain text): the disclaimer texture 6 s, the ESRB texture 4 s (only when sub_8232E0D0() says so),
//    Turn 10 10 s, Microsoft Game Studios 10 s, Dolby 5 s and the Forza intro video 70 s. It hands them
//    to the sequencer's setter (sub_822BD7B8) through a virtual call ((*(0x82C2111C) + 472)->vtable[88]).
//    The sequencer moves on by time (as in FM2), so the boot sequence's durations are cut to a few
//    frames. The setter is only reached through that call: its address is read from the vtable while
//    the sequence is built (the sequencer doesn't exist when sub_8225A870 starts) and its indirect-call
//    table entry is wrapped. Other sequences (loading screens) use the same setter; the boot one is
//    recognised by its timings (10, 10, 5, 70 s in a row).
//    The game's own "disablesplashintro"/"noattractvideo" options are parsed but have no reader left
//    in the retail build.
//
// 2. Command line (FM3_CMDLINE, or settings.ini cmdline=...). The retail game still parses Turn 10's
//    developer command line (parser object: text at +4; sub_826F6690 / sub_826F66F8 / sub_826F6748
//    run the passes, the options object's handler is sub_8225EF78, options at 0x82C21C60), but
//    sub_826F5D50 empties the text before parsing. Its hook puts ours there. Tokens are separated by
//    spaces: a bare name toggles a flag ("launch"), "name=value" sets an option ("track=0", flags take
//    true/1). Race launch options (offsets in the options object): launch (+1082, start straight
//    into a race), ShowPreRaceAnd321InCmdlineLaunch (+1083), track (+1092, Tracks.id; or trackname=
//    <MediaName> with ribbon=<RibbonIndex>), car (+1160, Data_Car.Id), color (+1164), numlaps (+1168),
//    raceline, brakeassist, steeringassist, numaicars (+1184), aicar<N>, aicolor<N>, startpos (+1444),
//    aiskill (+1452), engageaionstart, freerun (+1801), exhibition (+1802), shortrace (+1516).
//    Never pass "showkeyboard" (asks for the command line through the guide keyboard).
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <cstring>
#include <string>

#include "indirect.h"
#include "kernel/imports.h"
#include "log.h"
#include "memory.h"
#include "settings.h"

extern PPCHostFunc** g_indirectTable;         // src/indirect.cpp: one entry per guest instruction from 0x82000000
void LogAllHostThreadStacks(const char* why); // src/hang.cpp

namespace
{
constexpr uint32_t kIndirectBase = 0x82000000;
constexpr uint32_t kSequencerHolder = 0x82C2111C; // pointer; its +472 is the sequencer object
constexpr uint32_t kEntrySize = 240, kEntries = 8;
constexpr uint32_t kDurationOffset = 160;
constexpr double kShort = 0.05; // seconds (FM2 uses the same)

double LoadDouble(uint32_t a)
{
    uint64_t raw;
    memcpy(&raw, GuestPtr<uint8_t>(a), 8);
    raw = __builtin_bswap64(raw);
    double d;
    memcpy(&d, &raw, 8);
    return d;
}

void StoreDouble(uint32_t a, double d)
{
    uint64_t raw;
    memcpy(&raw, &d, 8);
    raw = __builtin_bswap64(raw);
    memcpy(GuestPtr<uint8_t>(a), &raw, 8);
}

PPCHostFunc* g_setter = nullptr; // the sequencer setter's recompiled function
bool g_introDone = false;

// Wraps the sequencer's setter (this r3, entries r4): shortens the boot sequence once.
void SetSequenceHook(PPCContext& ctx, uint8_t* base)
{
    if (!g_introDone)
    {
        // The boot sequence, recognised by its timings (the file field is not plain text): Turn 10 10 s,
        // MSGS 10 s, Dolby 5 s, Forza intro 70 s in a row, after the disclaimer (6 s) and maybe ESRB (4 s).
        const uint32_t entries = ctx.r4.u32;
        auto duration = [&](uint32_t i) { return LoadDouble(entries + i * kEntrySize + kDurationOffset); };
        int last = -1;
        for (uint32_t i = 3; i < kEntries && last < 0; i++)
            if (duration(i - 3) == 10 && duration(i - 2) == 10 && duration(i - 1) == 5 && duration(i) == 70)
                last = int(i);
        if (last >= 0)
        {
            g_introDone = true;
            std::string list;
            for (int i = 0; i <= last; i++)
            {
                list += " " + std::to_string(int(duration(i))) + " s";
                StoreDouble(entries + i * kEntrySize + kDurationOffset, kShort);
            }
            Log("intro: skipped (legal text, logos, intro video):%s", list.c_str());
        }
    }
    g_setter(ctx, base);
}

// FM3_DUMP_THREADS (set live: Fm3 "env FM3_DUMP_THREADS 1"): logs every thread's host stack once per change of the
// value (a stuck load: which guest function each thread waits in). Polled by a host thread started at boot.
void LogAllHostThreadStacksOnRequest()
{
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            std::string last;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const char* v = getenv("FM3_DUMP_THREADS");
                const std::string now = v ? v : "";
                if (now != last && !now.empty())
                    LogAllHostThreadStacks("FM3_DUMP_THREADS");
                last = now;
            }
        }).detach();
    });
}

std::string CommandLine()
{
    if (const char* v = getenv("FM3_CMDLINE"))
        return v;
    return GetSettings().Extra("cmdline");
}
}

// The sequencer's setter is looked up while the boot sequence is built: sub_8225A870 copies each entry's file name
// with sub_822232C8 (dest r3, source r4, length r5). The Forza intro's name (the last entry, 0x82007BF8) is copied
// right before the sequence is handed over; the sequencer doesn't exist yet when sub_8225A870 starts (session 17).
PPC_FUNC_IMPL(__imp__sub_822232C8);
PPC_FUNC(sub_822232C8)
{
    constexpr uint32_t kForzaIntroPath = 0x82007BF8; // the Forza_Intro.bik path in Splash_Intros
    if (ctx.r4.u32 == kForzaIntroPath && !g_setter && !g_introDone && GetSettings().skipIntro)
    {
        const uint32_t holder = LoadBE32(kSequencerHolder);
        const uint32_t object = holder ? LoadBE32(holder + 472) : 0;
        const uint32_t vtable = object ? LoadBE32(object) : 0;
        const uint32_t setter = vtable ? LoadBE32(vtable + 88) : 0;
        PPCHostFunc* host = setter ? IndirectFind(setter) : nullptr;
        if (host)
        {
            g_setter = host;
            g_indirectTable[(setter - kIndirectBase) / 4] = SetSequenceHook;
            Log("intro: sequencer setter %08X wrapped", setter);
        }
        else
            Log("intro: sequencer setter not found (holder %08X object %08X vtable %08X): intro not skipped", holder, object, vtable);
    }
    __imp__sub_822232C8(ctx, base);
}

// Fills the command-line parser's text (this r3, text at +4, up to 1020 bytes) after the retail
// code has emptied it.
PPC_FUNC_IMPL(__imp__sub_826F5D50);
PPC_FUNC(sub_826F5D50)
{
    const uint32_t parser = ctx.r3.u32;
    LogAllHostThreadStacksOnRequest();
    __imp__sub_826F5D50(ctx, base);
    std::string line = CommandLine();
    for (char& c : line)
        if (c == '\t' || c == '\r' || c == '\n')
            c = ' ';
    if (line.find("showkeyboard") != std::string::npos)
    {
        Log("cmdline: \"showkeyboard\" removed (it waits for the guide keyboard)");
        line.erase(line.find("showkeyboard"), strlen("showkeyboard"));
    }
    if (line.empty())
        return;
    if (line.size() > 1019)
        line.resize(1019);
    memcpy(GuestPtr<char>(parser + 4), line.c_str(), line.size() + 1);
    Log("cmdline: \"%s\"", line.c_str());
}
