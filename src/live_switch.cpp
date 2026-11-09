#include "live_switch.h"

#include <cstdlib>
#include <map>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

#include <windows.h>

#include "log.h"

namespace
{
struct Registry
{
    std::mutex mutex;
    std::vector<LiveSwitch*> switches;
    std::map<std::string, bool> pending; // set before the switch first ran
};

Registry& Get()
{
    static Registry r;
    return r;
}

void SetEnv(const std::string& name, bool on)
{
    // getenv() readers of the same name (startup code, other copies) see it too.
    _putenv_s(name.c_str(), on ? "1" : "");
    SetEnvironmentVariableA(name.c_str(), on ? "1" : nullptr);
}

// The debug server's "switch" command only takes REGPU_*/PGR4_* names (ReGPU's debug.cpp), so FM3_* switches
// follow the environment instead: with the debug server on (REGPU_DEBUG), a thread re-reads every registered
// switch's variable four times a second, and Fm3 "env FM3_NAME 1" (or 0, or empty) flips it live.
void StartEnvWatch()
{
    if (!getenv("REGPU_DEBUG"))
        return;
    std::thread([] {
        SetThreadDescription(GetCurrentThread(), L"fm3 switch env watch");
        for (;;)
        {
            Sleep(250);
            Registry& r = Get();
            std::lock_guard lock(r.mutex);
            std::set<std::string> changed; // (several statics can share a name: log it once)
            for (LiveSwitch* s : r.switches)
            {
                char v[16] = {};
                const DWORD n = GetEnvironmentVariableA(s->Name(), v, sizeof(v));
                const bool on = n != 0 && !(n == 1 && v[0] == '0');
                if (on != bool(*s))
                {
                    s->SetFromWatch(on);
                    if (changed.insert(s->Name()).second)
                        Log("switch: %s %s (environment)", s->Name(), on ? "on" : "off");
                }
            }
        }
    }).detach();
}
}

void LiveSwitch::SetFromWatch(bool on)
{
    value_.store(on, std::memory_order_relaxed);
}

LiveSwitch::LiveSwitch(const char* name) : name_(name), value_(false)
{
    static std::once_flag watch;
    std::call_once(watch, StartEnvWatch);
    Registry& r = Get();
    std::lock_guard lock(r.mutex);
    const char* v = getenv(name);
    // (An empty value is how the debug server clears one; "0" is off too, as for the environment watch.)
    bool on = v != nullptr && *v != 0 && strcmp(v, "0") != 0;
    if (auto it = r.pending.find(name); it != r.pending.end())
        on = it->second;
    value_.store(on, std::memory_order_relaxed);
    r.switches.push_back(this);
}

bool LiveSwitch::Set(const std::string& name, bool on)
{
    Registry& r = Get();
    std::lock_guard lock(r.mutex);
    SetEnv(name, on);
    bool found = false;
    for (LiveSwitch* s : r.switches)
        if (name == s->name_)
        {
            s->value_.store(on, std::memory_order_relaxed);
            found = true;
        }
    if (!found)
        r.pending[name] = on;
    Log("switch: %s %s%s", name.c_str(), on ? "on" : "off", found ? "" : " (not reached yet: kept for when it is)");
    return found;
}

std::vector<std::pair<std::string, bool>> LiveSwitch::List()
{
    Registry& r = Get();
    std::lock_guard lock(r.mutex);
    std::vector<std::pair<std::string, bool>> out;
    for (LiveSwitch* s : r.switches)
        out.emplace_back(s->name_, bool(*s));
    return out;
}
