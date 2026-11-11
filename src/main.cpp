#include <cstring>
#include <filesystem>
#include <thread>

#include <windows.h>
#include <timeapi.h>

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

// Laptops with an integrated and a dedicated GPU (session 23, PERF-PLAN L15): ReGPU makes its device on the
// default adapter (D3D12CreateDevice(nullptr)), which on such a laptop is often the integrated one. These two
// exports ask NVIDIA Optimus and AMD switchable graphics to make the dedicated GPU the default for this exe.
// Nothing changes on a desktop with one GPU; Windows' own per-app GPU choice still wins over them.
extern "C"
{
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

int main(int argc, char** argv)
{
    // The launcher's Wheel stuff tab: racing wheels, pedals, shifters and handbrakes as the game sees
    // them (src/input/wheel/wheel_probe.cpp). Nothing of the game is loaded.
    if (argc > 1 && strcmp(argv[1], "--wheel-probe") == 0)
        return wheel::ProbeMain();
    CrashHandlerInit();
    // Timer resolution while the window is hidden (session 26, PERF-PLAN L38): Windows 11 drops a process's
    // timeBeginPeriod(1) request while it has no visible window (minimized or covered), so Sleep(1) and
    // sleep_until then take a 15.6 ms tick. Opting out of that throttling keeps 1 ms ticks minimized, as visible.
    // (Our frame limiter, vblanks and ring waits use high-resolution waitable timers, which this does not touch;
    // the audio loop and plain Sleep(1) waits do.) FM3_NO_TIMER_FIX=1: as before (start time).
    if (!getenv("FM3_NO_TIMER_FIX"))
    {
        PROCESS_POWER_THROTTLING_STATE state{};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        state.StateMask = 0; // (in the mask, state off: never ignore this process's timer resolution request)
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
        timeBeginPeriod(1);
    }
    // FM3_TRACE_SLEEP=1: logs how long Sleep(1) takes, 20 s after start (minimized by then on test boots).
    if (getenv("FM3_TRACE_SLEEP"))
        std::thread([] {
            for (int round = 0; round < 3; round++)
            {
                Sleep(20000);
                LARGE_INTEGER f, a, b;
                QueryPerformanceFrequency(&f);
                QueryPerformanceCounter(&a);
                for (int i = 0; i < 50; i++)
                    Sleep(1);
                QueryPerformanceCounter(&b);
                Log("timer: Sleep(1) takes %.2f ms", double(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 50);
            }
        }).detach();
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
