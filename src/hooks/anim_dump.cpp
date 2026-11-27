// The crowd's animations, decoded by the game itself (2026-11-27, research for the Godot viewer's animated crowd:
// ..\Godot\frame-ir PLAN-NEXT.md item 2). Media\Spectators\anims\*.anim.bin are 'QUAT_BITSTREAM' animations; the
// codec's "sample at a time" entry is sub_82AABED0 (codec table after the string at image offset 0xC1F380), which
// takes the animation (r3), the time (f1) and pointers in r5-r10. Rather than port the bitstream decoder, the game is
// asked to sample: FM3_ANIM_LOG=1 first logs, for the first animations it is called with, the arguments and 64 bytes
// at each pointer before and after the call (which one is the output, and its layout).
#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "kernel/imports.h"
#include "log.h"

PPC_FUNC_IMPL(__imp__sub_82AABED0);

namespace
{
bool AnimLogOn()
{
    static const bool on = [] {
        const char* v = getenv("FM3_ANIM_LOG");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return on;
}

void HexLine(char* out, size_t n, uint32_t a)
{
    out[0] = 0;
    if (a < 0x40000000 || a >= 0x7FFFF000)
        return;
    size_t k = 0;
    for (int i = 0; i < 16 && k + 10 < n; i++)
        k += snprintf(out + k, n - k, "%08X ", LoadBE32(a + i * 4));
}
}

// FM3_ANIM_DUMP=1: what the log showed (2026-11-27): r3 = an animation instance (its +0x0C / +0x10 point at the shared
// animation data), f1 = the time, r10 = the track count (13 for the crowd), r6 = the output, r10 transforms of 48 bytes
// (rotation quaternion x y z w, translation x y z -, scale x y z -), r7 = a small per-track word. The first time an
// animation's data is seen, it is sampled at 0 .. 6 s in 1/30 s steps by calling the decoder itself (the arguments as
// the game passed them, the output read after each call), and written to out/run/anim_dump/<data>.bin: 'FM3A', the
// track count, the sample count, the instance's first 64 bytes, 256 bytes at each of its +0x0C / +0x10 pointers (to
// match the dump to its .anim.bin), then per sample: the time (f32) and the transforms (big-endian floats as in memory).
// The game's own call runs last, so its output is as it would have been.
namespace
{
bool AnimDumpOn()
{
    static const bool on = [] {
        const char* v = getenv("FM3_ANIM_DUMP");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return on;
}

void AnimDump(PPCContext& ctx, uint8_t* base)
{
    const uint32_t inst = ctx.r3.u32;
    // (the animation's own data: +0x00 for the crowd's 17-bone instances, which share +0x0C and live in the A0000000
    // mirror of physical memory; +0x0C for the drivers')
    const uint32_t data0 = LoadBE32(inst + 0x00);
    const uint32_t data = (data0 >= 0xA0000000u && data0 < 0xC0000000u) ? data0 : LoadBE32(inst + 0x0C);
    const uint32_t tracks = ctx.r10.u32;
    if (tracks == 0 || tracks > 128 || data < 0x40000000)
        return;
    static uint32_t done[128] = {};
    for (uint32_t& d : done)
    {
        if (d == data)
            return;
        if (!d)
        {
            d = data;
            break;
        }
    }
    CreateDirectoryA("anim_dump", nullptr);
    char path[96];
    snprintf(path, sizeof(path), "anim_dump/%08X.bin", data);
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    // (181 samples (0-6 s at 30 a second), or the animation's own length when its record says it: frames at +34)
    int kSamples = 181;
    if (memcmp(base + data, "animation", 9) == 0)
    {
        const uint32_t frames = (uint32_t(base[data + 34]) << 8) | base[data + 35];
        if (frames > 0 && frames < 2000)
            kSamples = int(frames) + 1;
    }
    const uint32_t header[4] = { 0x41334D46u /* 'FM3A' little-endian */, tracks, uint32_t(kSamples), data };
    fwrite(header, 4, 4, f);
    fwrite(base + inst, 1, 64, f);
    fwrite(base + data, 1, 256, f);
    const uint32_t data2 = LoadBE32(inst + 0x10);
    if (data2 >= 0x40000000 && data2 < 0x7FFFF000)
        fwrite(base + data2, 1, 256, f);
    else
    {
        static const uint8_t zero[256] = {};
        fwrite(zero, 1, 256, f);
    }
    const PPCContext saved = ctx;
    const uint32_t out = ctx.r6.u32;
    for (int i = 0; i < kSamples; i++)
    {
        ctx = saved;
        // (the codec's time is normalized, 0 .. 1 over the animation: the race load samples at 0 and 1, and past 1 it
        // extrapolates; the samples are evenly spaced over it, the file's t = their place 0 .. 1)
        ctx.f1.f64 = double(i) / double(kSamples - 1);
        __imp__sub_82AABED0(ctx, base);
        const float t = float(i) / float(kSamples - 1);
        fwrite(&t, 4, 1, f);
        fwrite(base + out, 1, size_t(tracks) * 48, f);
    }
    fclose(f);
    ctx = saved;
    Log("anim: dumped animation data %08X (%u tracks, %d samples) to %s", data, tracks, kSamples, path);
}
}

PPC_FUNC(sub_82AABED0)
{
    if (AnimDumpOn())
        AnimDump(ctx, base);
    if (!AnimLogOn())
    {
        __imp__sub_82AABED0(ctx, base);
        return;
    }
    static uint32_t seen[128] = {};
    static int calls = 0;
    const uint32_t anim = ctx.r3.u32;
    bool fresh = false;
    for (uint32_t& s : seen)
    {
        if (s == anim)
            break;
        if (!s)
        {
            s = anim;
            fresh = true;
            break;
        }
    }
    const uint32_t regs[8] = { ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32 };
    const double t = ctx.f1.f64;
    char before[8][200];
    if (fresh)
        for (int r = 0; r < 8; r++)
            HexLine(before[r], sizeof(before[r]), regs[r]);
    __imp__sub_82AABED0(ctx, base);
    if (!fresh)
    {
        calls++;
        return;
    }
    Log("anim: sample anim %08X at t %.4f (lr %08llX; %d calls before): r3 %08X r4 %08X r5 %08X r6 %08X r7 %08X r8 %08X r9 %08X r10 %08X",
        anim, t, (unsigned long long)ctx.lr, calls, regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]);
    for (int r = 0; r < 8; r++)
    {
        char after[200];
        HexLine(after, sizeof(after), regs[r]);
        if (before[r][0])
            Log("anim:   r%d %08X before %s", r + 3, regs[r], before[r]);
        if (after[0] && strcmp(before[r], after) != 0)
            Log("anim:   r%d %08X after  %s  (changed)", r + 3, regs[r], after);
    }
}

// The codec's other two entries (setup / decode with r5 = 0 / 1, the table after the 'QUAT_BITSTREAM' string): logged
// with FM3_ANIM_LOG=1 too, the first few distinct r3 each (the crowd's 17-bone animations did not come through
// sub_82AABED0 on the grid race).
namespace
{
void LogEntry(const char* name, PPCContext& ctx)
{
    static uint32_t seen[2][6] = {};
    const int which = name[0] == 'E' ? 0 : 1;
    for (uint32_t& s : seen[which])
    {
        if (s == ctx.r3.u32)
            return;
        if (!s)
        {
            s = ctx.r3.u32;
            Log("anim: %s r3 %08X r4 %08X r5 %08X r6 %08X r7 %08X r8 %08X r9 %08X r10 %08X f1 %.4f (lr %08llX)", name, ctx.r3.u32, ctx.r4.u32,
                ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, ctx.f1.f64, (unsigned long long)ctx.lr);
            return;
        }
    }
}
}

PPC_FUNC_IMPL(__imp__sub_82AABE78);
PPC_FUNC(sub_82AABE78)
{
    if (AnimLogOn())
        LogEntry("E78", ctx);
    __imp__sub_82AABE78(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_82AABEB0);
PPC_FUNC(sub_82AABEB0)
{
    if (AnimLogOn())
        LogEntry("EB0", ctx);
    __imp__sub_82AABEB0(ctx, base);
}
