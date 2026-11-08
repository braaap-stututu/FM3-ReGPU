#include <cstring>
#include <filesystem>
#include <thread>

#include <windows.h>

#include "input/wheel/wheel_system.h"
#include "kernel/runtime.h"
#include "kernel/thread.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "regpu/regpu.h"
#include <ppc_config.h>

void CrashHandlerInit();
void D3DDebugLog();

int main(int argc, char** argv)
{
    // The launcher's Wheel stuff tab: racing wheels, pedals, shifters and handbrakes as the game sees
    // them (src/input/wheel/wheel_probe.cpp). Nothing of the game is loaded.
    if (argc > 1 && strcmp(argv[1], "--wheel-probe") == 0)
        return wheel::ProbeMain();
    CrashHandlerInit();
    std::filesystem::path gameDir = argc > 1 ? argv[1] : "game";
    Log("fm3: game folder %s", std::filesystem::absolute(gameDir).string().c_str());

    MemoryInit();
    LoadTitle(gameDir / "default.xex");
    if (const char* dump = getenv("FM3_DUMP_IMAGE")) // writes the loaded executable image (PPC_IMAGE_BASE..+SIZE)
    {
        if (FILE* f = fopen(dump, "wb"))
        {
            fwrite(GuestPtr(uint32_t(PPC_IMAGE_BASE)), 1, size_t(PPC_IMAGE_SIZE), f);
            fclose(f);
            Log("wrote the loaded image to %s", dump);
        }
    }
    RuntimeInit(gameDir);
    ThreadSystemInit(g_title.tlsTemplate, g_title.tlsDataSize, g_title.tlsSlotCount);

    void StartThreadProfiler();
    StartThreadProfiler();
    void StartSpikeRecorder();
    StartSpikeRecorder();
    void StartWatch();
    StartWatch();
    auto main = CreateGuestThread(g_title.entryPoint, 0, 0, g_title.stackSize, false, "main");
    char watchdogMs[16] = {};
    const DWORD watchdog = GetEnvironmentVariableA("FM3_WATCHDOG", watchdogMs, 16) ? (atoi(watchdogMs) > 1 ? std::max(50, atoi(watchdogMs)) : 3000) : INFINITE;
    if (getenv("FM3_WATCHW"))
    {
        std::thread([] {
            void ArmWatchpointAll();
            for (;;)
            {
                Sleep(10);
                ArmWatchpointAll();
            }
        }).detach();
    }
    while (WaitForSingleObject(main->thread->hostHandle, watchdog) == WAIT_TIMEOUT)
    {
        Log("watchdog:");
        LogGuestThreads();
        regpu::LogState();
        D3DDebugLog();
        void LogFileStats();
        LogFileStats();
    }
    Log("fm3: main thread exited with %08X", main->exitCode);
    return 0;
}
