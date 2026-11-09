// GPU adapter and memory log (session 18, RESEARCH.md "Less RAM, VRAM and memory traffic" step 1 and
// "Choosing the GPU in the launcher" step 4). Measurement only; changes nothing the game or ReGPU does.
//  - At start: every DXGI adapter (name, PCI ids, dedicated/shared memory) and which one ReGPU renders on
//    (it creates its device on the default adapter, D3D12CreateDevice(nullptr): adapter 0 here).
//  - Every 30 s: this process's video memory use against the OS budget (QueryVideoMemoryInfo: local = the
//    GPU's own memory, or an iGPU's carve-out; non-local = shared system memory), and its RAM (working
//    set, private bytes), each with the peak so far. FM3_NO_GPU_MEMORY_LOG=1: off.
#include <cstdint>
#include <thread>

#include <windows.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <wrl/client.h>

#include "log.h"

#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
uint64_t Mb(uint64_t bytes) { return bytes >> 20; }

void LogAdapters(IDXGIFactory1* factory)
{
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        char name[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
        Log("gpu: adapter %u%s: %s (%04X:%04X), dedicated %llu MB, shared %llu MB%s", i, i == 0 ? " (ReGPU renders here)" : "", name,
            d.VendorId, d.DeviceId, (unsigned long long)Mb(d.DedicatedVideoMemory), (unsigned long long)Mb(d.SharedSystemMemory),
            (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? ", software" : "");
    }
}
}

void StartGpuMemoryLog()
{
    if (getenv("FM3_NO_GPU_MEMORY_LOG"))
        return;
    std::thread([] {
        SetThreadDescription(GetCurrentThread(), L"fm3 gpu memory log");
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return;
        LogAdapters(factory.Get());
        ComPtr<IDXGIAdapter1> adapter1;
        ComPtr<IDXGIAdapter3> adapter;
        if (FAILED(factory->EnumAdapters1(0, &adapter1)) || FAILED(adapter1.As(&adapter)))
            return;
        uint64_t peakLocal = 0, peakShared = 0, peakPrivate = 0;
        for (;;)
        {
            Sleep(30000);
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
            adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
            adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            peakLocal = std::max<uint64_t>(peakLocal, local.CurrentUsage);
            peakShared = std::max<uint64_t>(peakShared, shared.CurrentUsage);
            peakPrivate = std::max<uint64_t>(peakPrivate, pmc.PrivateUsage);
            Log("gpu memory: local %llu / budget %llu MB (peak %llu), shared %llu / %llu MB (peak %llu), RAM working set %llu MB, "
                "private %llu MB (peak %llu)",
                (unsigned long long)Mb(local.CurrentUsage), (unsigned long long)Mb(local.Budget), (unsigned long long)Mb(peakLocal),
                (unsigned long long)Mb(shared.CurrentUsage), (unsigned long long)Mb(shared.Budget), (unsigned long long)Mb(peakShared),
                (unsigned long long)Mb(pmc.WorkingSetSize), (unsigned long long)Mb(pmc.PrivateUsage), (unsigned long long)Mb(peakPrivate));
        }
    }).detach();
}
