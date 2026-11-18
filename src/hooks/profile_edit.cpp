// Profile editing (session 44, user request: credits to buy cars for vinyl tests).
//
// FM3's saves are encrypted ("cmss" containers), so we edit the profile in memory through the game's own code and let
// the game save it. The profile keeps its values in a property tree ("Main/Credits", "Main/TotalWinnings", ...,
// CVariant values):
//   sub_82261010(profile)          GetCredits ("Main/Credits" as an integer)
//   sub_82263568(profile, amount)  SetCredits (caps at 999,999,999, writes "Main/Credits", then sub_82262DC8 tells
//                                  the game: menus and subscribers see the new amount at once)
//   sub_822686B8(profile)          a profile check (from sub_82269790, not on every load) that caps credits above
//                                  20,000,000 down to 20,000,000 through SetCredits
//
// The request: <user>\profiles\<profile>\profile-edit.ini (the launcher's Profile page writes it):
//   credits=N       set the credits to N
//   add_credits=N   add N (negative takes away)
// Applied the first time the game reads the credits (GetCredits hooked), then renamed to profile-edit.applied.ini. Amounts are kept
// within 0..20,000,000 (the game's load cap: more would be cut back at the next load).
// The game writes the new amount into the save at its next save (after a race, a purchase, ...); quitting from the
// menus without such a save keeps the old amount on disk, and the edit is lost.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "kernel/imports.h"
#include "kernel/runtime.h"
#include "log.h"

PPC_FUNC_IMPL(__imp__sub_82261010);
PPC_EXTERN_FUNC(sub_82263568);

namespace
{
constexpr int64_t kMaxCredits = 20000000; // sub_822686B8's load cap

struct Edit
{
    bool set = false, add = false;
    int64_t credits = 0, addCredits = 0;
};

std::filesystem::path RequestFile()
{
    return ContentDir().parent_path() / "profile-edit.ini";
}

bool ReadRequest(Edit& e)
{
    std::ifstream in(RequestFile());
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        const auto cut = line.find_first_of(";#");
        if (cut != std::string::npos)
            line.resize(cut);
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        auto trim = [](std::string s) {
            s.erase(0, s.find_first_not_of(" \t\r"));
            s.erase(s.find_last_not_of(" \t\r") + 1);
            return s;
        };
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (value.empty())
            continue;
        if (key == "credits")
            e.set = true, e.credits = _strtoi64(value.c_str(), nullptr, 10);
        else if (key == "add_credits")
            e.add = true, e.addCredits = _strtoi64(value.c_str(), nullptr, 10);
    }
    return e.set || e.add;
}

uint32_t GetCredits(PPCContext& ctx, uint8_t* base, uint32_t profile)
{
    ctx.r3.u64 = profile;
    __imp__sub_82261010(ctx, base);
    return ctx.r3.u32;
}

void SetCredits(PPCContext& ctx, uint8_t* base, uint32_t profile, uint32_t amount)
{
    ctx.r3.u64 = profile;
    ctx.r4.u64 = amount;
    sub_82263568(ctx, base);
}
} // namespace

// GetCredits (r3 = the profile): the request is applied the first time the game reads the credits after the file
// appears (checked at most once a second, so a request written while the game runs applies at the next read too).
PPC_FUNC(sub_82261010)
{
    static thread_local bool busy = false;
    static ULONGLONG nextCheck = 0;
    const uint32_t profile = ctx.r3.u32;
    const ULONGLONG now = GetTickCount64();
    Edit e;
    if (busy || !profile || now < nextCheck || (nextCheck = now + 1000, !ReadRequest(e)))
    {
        __imp__sub_82261010(ctx, base);
        return;
    }
    busy = true;
    const PPCContext saved = ctx;
    const uint32_t before = GetCredits(ctx, base, profile);
    int64_t want = e.set ? e.credits : before;
    if (e.add)
        want += e.addCredits;
    want = std::clamp<int64_t>(want, 0, kMaxCredits);
    SetCredits(ctx, base, profile, uint32_t(want));
    const uint32_t after = GetCredits(ctx, base, profile);
    ctx = saved;
    busy = false;
    std::error_code ec;
    const auto done = RequestFile().parent_path() / "profile-edit.applied.ini";
    std::filesystem::remove(done, ec);
    std::filesystem::rename(RequestFile(), done, ec);
    Log("profile edit: credits %u -> %u (%s%s); saved by the game at its next save (after a race or a purchase)", before,
        after, e.set ? "set" : "", e.add ? (e.set ? " + add" : "add") : "");
    ctx.r3.u64 = after;
}
