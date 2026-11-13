// Profile-guided optimisation of ReGPU (session 32, PERF-PLAN L23). Only in a build configured with -DFM3_PGO=gen
// (ReGPU compiled with -fprofile-instr-generate): the game may end through ExitProcess, which skips the profile
// runtime's exit handler, so the profile is written on request: `Fm3 "env FM3_PGO_WRITE 1"` (polled twice a second)
// writes it to LLVM_PROFILE_FILE (default: default.profraw in the working directory). How to record and use a
// profile: pgo/README.md.
#ifdef FM3_PGO_GEN
#include <windows.h>

#include <thread>

#include "log.h"

extern "C" int __llvm_profile_write_file(void);

namespace
{
struct PgoWriter
{
    PgoWriter()
    {
        std::thread([] {
            bool last = false;
            for (;;)
            {
                Sleep(500);
                char v[4] = {};
                const bool on = GetEnvironmentVariableA("FM3_PGO_WRITE", v, sizeof(v)) && v[0] == '1';
                if (on && !last)
                {
                    const int r = __llvm_profile_write_file();
                    Log("pgo: profile written (%d)", r);
                }
                last = on;
            }
        }).detach();
    }
} g_pgoWriter;
} // namespace
#endif
