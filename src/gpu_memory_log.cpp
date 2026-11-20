// GPU and memory log (session 18, RESEARCH.md "Less RAM, VRAM and memory traffic" step 1 and
// "Choosing the GPU in the launcher" step 4). Measurement only; changes nothing the game or ReGPU does.
//  - Every 30 s: this process's video memory use against the driver's budget (ReGPU's GetGpuMemory, from
//    VK_EXT_memory_budget: local = the GPU's own memory, or an iGPU's carve-out; shared = system memory the GPU
//    reads), and its RAM (working set, private bytes), each with the peak so far. The first line names the GPU
//    (ReGPU's own log lists every Vulkan device and which one it picked). FM3_NO_GPU_MEMORY_LOG=1: off.
// (Branch regpu-vulkan: was DXGI's adapter list and QueryVideoMemoryInfo with the D3D12 ReGPU.)
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include <windows.h>
#include <psapi.h>

#include "log.h"
#include "regpu/regpu.h"

namespace
{
uint64_t Mb(uint64_t bytes) { return bytes >> 20; }
}

void StartGpuMemoryLog()
{
    if (getenv("FM3_NO_GPU_MEMORY_LOG"))
        return;
    std::thread([] {
        SetThreadDescription(GetCurrentThread(), L"fm3 gpu memory log");
        uint64_t peakLocal = 0, peakShared = 0, peakPrivate = 0;
        bool named = false;
        for (;;)
        {
            Sleep(30000);
            regpu::GpuMemory g;
            if (!regpu::GetGpuMemory(g))
                continue;
            if (!named)
            {
                Log("gpu: rendering on %s (%04X:%04X)%s", g.name, g.vendorId, g.deviceId, g.integrated ? ", integrated" : "");
                named = true;
            }
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            peakLocal = std::max<uint64_t>(peakLocal, g.localUsage);
            peakShared = std::max<uint64_t>(peakShared, g.sharedUsage);
            peakPrivate = std::max<uint64_t>(peakPrivate, pmc.PrivateUsage);
            Log("gpu memory: local %llu / budget %llu MB (peak %llu), shared %llu / %llu MB (peak %llu), RAM working set %llu MB, "
                "private %llu MB (peak %llu)",
                (unsigned long long)Mb(g.localUsage), (unsigned long long)Mb(g.localBudget), (unsigned long long)Mb(peakLocal),
                (unsigned long long)Mb(g.sharedUsage), (unsigned long long)Mb(g.sharedBudget), (unsigned long long)Mb(peakShared),
                (unsigned long long)Mb(pmc.WorkingSetSize), (unsigned long long)Mb(pmc.PrivateUsage), (unsigned long long)Mb(peakPrivate));
        }
    }).detach();
}
