#pragma once
// Force-included into the recompiled code (out/ppc, see CMakeLists.txt): definitions the
// patched XenonRecomp output expects from the runtime. Same contract as the FM4 port's
// ppc_hooks.h (patches/xenonrecomp-fm2 and -fm4-instructions emit these names).
#include <cstdint>

struct PPCContext;

// mtmsrd toggling MSR[EE]: clearing it masks interrupts on the console; the runtime turns it
// into the global interrupt lock (kernel, Phase 3).
void PPCMsrChanged(PPCContext& ctx, uint64_t oldMsr);
#define PPC_MSR_CHANGED(ctx, oldMsr)                 \
    do                                               \
    {                                                \
        if (((oldMsr) ^ (ctx).msr) & 0x8000)         \
            PPCMsrChanged((ctx), (oldMsr));          \
    } while (0)

// Stores XenonRecomp marks as hardware register writes (store + eieio). The runtime routes the
// APU range to the XMA decoder; anything else is an ordinary big-endian store.
void PPCMmioStore32(uint8_t* base, uint32_t address, uint32_t value);
#define PPC_MM_STORE_U32(x, y) PPCMmioStore32(base, (x), (y))

// IEEE half -> float (vupkd3d128 FLOAT16 forms).
inline float ppc_half_to_float(uint16_t h)
{
    const uint32_t sign = uint32_t(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1F, man = h & 0x3FF, bits;
    if (exp == 0)
    {
        if (man == 0)
            bits = sign;
        else
        {
            exp = 113;
            while (!(man & 0x400))
            {
                man <<= 1;
                exp--;
            }
            bits = sign | (exp << 23) | ((man & 0x3FF) << 13);
        }
    }
    else if (exp == 31)
        bits = sign | 0x7F800000 | (man << 13);
    else
        bits = sign | ((exp + 112) << 23) | (man << 13);
    float f;
    __builtin_memcpy(&f, &bits, 4);
    return f;
}

// float -> Xenos half (vpkd3d128): no inf/NaN, saturates to 0x7FFF, truncates.
inline uint16_t ppc_float_to_half(float f)
{
    uint32_t bits;
    __builtin_memcpy(&bits, &f, 4);
    const uint16_t sign = uint16_t((bits >> 16) & 0x8000);
    const uint32_t a = bits & 0x7FFFFFFF;
    if (a > 0x477FE000)
        return sign | 0x7FFF;
    if (a < 0x38800000)
    {
        const uint32_t e = a >> 23;
        if (e < 103)
            return sign;
        return sign | uint16_t(((a & 0x7FFFFF) | 0x800000) >> (126 - e));
    }
    return sign | uint16_t((a - 0x38000000) >> 13);
}

// Indirect calls (bctrl, vtables, function pointers): one table entry per guest instruction of
// the code range, filled from PPCFuncMappings at start (src/indirect.cpp, Phase 3). A miss is
// fatal with the address (Q4: a pointer-only function the config is missing), never a null call.
using PPCHostFunc = void(PPCContext&, uint8_t*);
constexpr uint32_t kIndirectLow = 0x82000000;
constexpr uint32_t kIndirectHigh = 0x83000000;
extern PPCHostFunc** g_indirectTable;
[[noreturn]] void PPCIndirectMiss(uint32_t address);
inline PPCHostFunc* PPCLookupIndirect(uint32_t address)
{
    PPCHostFunc* f = address - kIndirectLow < kIndirectHigh - kIndirectLow ? g_indirectTable[(address - kIndirectLow) >> 2] : nullptr;
    if (!f) [[unlikely]]
        PPCIndirectMiss(address);
    return f;
}
#define PPC_CALL_INDIRECT_FUNC(x) (PPCLookupIndirect(uint32_t(x)))(ctx, base)
