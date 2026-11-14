// Long-frame stacks (session 33): settings.ini long_frame_stacks=1 (or FM3_LONG_STACKS=1; =N: only frames of N ms or
// more, default 15). ReGPU's draw thread ("gpu draw worker") is sampled every millisecond (instruction pointer and 20
// callers, unwound from a copy of its stack after it runs again); when ReGPU reports a long frame
// (driver::SetLongFrameCallback, from its long-frame log), the samples inside that frame are logged as
// "long stacks:" lines: the top functions, then the most frequent call chains. Each sample stops the thread for a
// few microseconds. Symbols come from crash.cpp's SymInitialize.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include "regpu/driver.h"
#include "log.h"

namespace
{
struct LongSample
{
    int64_t ns;
    DWORD64 rip;
    DWORD64 up[20];
};
std::mutex g_longMutex;
std::deque<std::pair<int64_t, double>> g_longPending; // (frame end in ns, frame ms)

int64_t NowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// On ReGPU's draw thread, at the end of a long frame: only queue it.
void OnLongFrame(uint64_t, double ms, double)
{
    static const double minMs = [] {
        const char* e = getenv("FM3_LONG_STACKS");
        const int v = e ? atoi(e) : 0;
        return v > 1 ? double(v) : 15.0;
    }();
    if (ms < minMs)
        return;
    std::lock_guard lock(g_longMutex);
    if (g_longPending.size() < 16)
        g_longPending.push_back({ NowNs(), ms });
}

std::string LongSymbol(DWORD64 addr)
{
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), addr, &displacement, symbol))
    {
        std::string n = symbol->Name;
        return n.size() > 70 ? n.substr(0, 70) + "~" : n;
    }
    HMODULE module = nullptr;
    char path[MAX_PATH];
    char at[96] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH))
    {
        const char* file = strrchr(path, '\\');
        snprintf(at, sizeof(at), "%s+%llx", file ? file + 1 : path, (unsigned long long)(addr - uintptr_t(module)));
    }
    return at;
}

HANDLE FindDrawThread()
{
    HANDLE found = nullptr;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{ sizeof(te) };
    for (BOOL more = Thread32First(snap, &te); more && !found; more = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != GetCurrentProcessId())
            continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        PWSTR desc = nullptr;
        if (h && SUCCEEDED(GetThreadDescription(h, &desc)) && desc && wcsncmp(desc, L"gpu draw worker", 15) == 0)
        {
            found = h;
            Log("long stacks: sampling the draw thread %lu every millisecond", te.th32ThreadID);
        }
        else if (h)
            CloseHandle(h);
        if (desc)
            LocalFree(desc);
    }
    CloseHandle(snap);
    return found;
}

void DumpLongFrame(const std::deque<LongSample>& ring, int64_t end, double ms)
{
    const int64_t from = end - int64_t(ms * 1e6);
    std::map<std::string, int> chains, leaves;
    int n = 0;
    for (auto& s : ring)
    {
        if (s.ns < from || s.ns > end)
            continue;
        std::string leaf = LongSymbol(s.rip), chain = leaf;
        for (DWORD64 a : s.up)
        {
            if (!a)
                break;
            chain += " < " + LongSymbol(a);
        }
        chains[chain]++;
        leaves[leaf]++;
        n++;
    }
    Log("long stacks: %.1f ms frame, %d samples; top functions:", ms, n);
    std::vector<std::pair<int, std::string>> top;
    for (auto& [k, v] : leaves)
        top.push_back({ v, k });
    std::sort(top.rbegin(), top.rend());
    std::string line;
    for (size_t i = 0; i < top.size() && i < 10; i++)
        line += (i ? " | " : "") + top[i].second + " x" + std::to_string(top[i].first);
    Log("long stacks:   %s", line.c_str());
    top.clear();
    for (auto& [k, v] : chains)
        top.push_back({ v, k });
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 6; i++)
        Log("long stacks:   x%d %s", top[i].first, top[i].second.c_str());
}
} // namespace

void StartLongFrameSampler()
{
    const char* env = getenv("FM3_LONG_STACKS");
    if (!env || !*env || !strcmp(env, "0"))
        return;
    regpu::driver::SetLongFrameCallback(OnLongFrame);
    std::thread([] {
        HANDLE thread = nullptr;
        while (!(thread = FindDrawThread()))
            Sleep(500);
        std::vector<uint8_t> stackCopy(36 * 1024); // (4 KB past what is copied: an unwind may read a little beyond)
        std::deque<LongSample> ring;                // the last 1.5 s
        int64_t lastDump = 0;
        for (;;)
        {
            Sleep(1);
            std::pair<int64_t, double> pending{};
            {
                std::lock_guard lock(g_longMutex);
                if (!g_longPending.empty())
                {
                    pending = g_longPending.front();
                    g_longPending.pop_front();
                }
            }
            if (pending.first)
            {
                // At most five a second, except frames of 40 ms or more (loading screens bring many).
                if (pending.first - lastDump > 200000000 || pending.second >= 40)
                {
                    lastDump = pending.first;
                    DumpLongFrame(ring, pending.first, pending.second);
                }
                continue;
            }
            // One sample: the registers and a copy of the stack while it is stopped; unwound after it runs again
            // (unwinding a stopped thread can deadlock on a lock it holds).
            if (SuspendThread(thread) == DWORD(-1))
                continue;
            const int64_t ns = NowNs();
            CONTEXT c{};
            c.ContextFlags = CONTEXT_FULL;
            const bool ok = GetThreadContext(thread, &c);
            size_t copied = 0;
            if (ok)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(reinterpret_cast<void*>(c.Rsp), &mbi, sizeof(mbi)))
                {
                    const uintptr_t end = uintptr_t(mbi.BaseAddress) + mbi.RegionSize;
                    copied = std::min<size_t>(32 * 1024, end - c.Rsp);
                    memcpy(stackCopy.data(), reinterpret_cast<const void*>(c.Rsp), copied);
                }
            }
            ResumeThread(thread);
            if (!ok)
                continue;
            LongSample s{ ns, c.Rip, {} };
            if (copied)
            {
                const DWORD64 lo = c.Rsp, hi = c.Rsp + copied, delta = DWORD64(uintptr_t(stackCopy.data())) - lo;
                auto rebase = [&](DWORD64& r) {
                    if (r >= lo && r < hi)
                        r += delta;
                };
                CONTEXT u = c;
                rebase(u.Rsp);
                rebase(u.Rbp);
                const DWORD64 cLo = lo + delta, cHi = hi + delta;
                for (int k = 0; k < 20; k++)
                {
                    if (u.Rsp < cLo || u.Rsp + 8 > cHi)
                        break;
                    DWORD64 imageBase = 0;
                    auto* fn = RtlLookupFunctionEntry(u.Rip, &imageBase, nullptr);
                    if (!fn)
                    {
                        u.Rip = *reinterpret_cast<DWORD64*>(u.Rsp); // a leaf (or no unwind data: then a guess)
                        u.Rsp += 8;
                    }
                    else
                    {
                        void* handlerData = nullptr;
                        DWORD64 establisher = 0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, u.Rip, fn, &u, &handlerData, &establisher, nullptr);
                        rebase(u.Rbp); // (a frame pointer restored from the stack points into the real stack)
                    }
                    s.up[k] = u.Rip;
                    if (!u.Rip)
                        break;
                }
            }
            ring.push_back(s);
            while (!ring.empty() && ns - ring.front().ns > 1500000000)
                ring.pop_front();
        }
    }).detach();
}
