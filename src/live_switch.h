#pragma once
// An on/off FM3_* switch that can be flipped while the game runs: the debug server's "switch" takes only
// REGPU_*/PGR4_* names, so FM3_* ones follow their environment variable (Fm3 "env FM3_NAME 1", or 0/empty
// for off; checked four times a second while REGPU_DEBUG is set). It starts from the environment variable (set = on), like the getenv() checks it
// replaces, and reading it is one relaxed atomic load. Declare it static where it's used:
//     static LiveSwitch noFoo("FM3_NO_FOO");
//     if (!noFoo) ...
// A switch registers itself the first time its line runs; a value set before that is kept for it. Some
// switches only matter at startup or feed caches, so flipping them shows from the next frame or not at all.
#include <atomic>
#include <string>
#include <utility>
#include <vector>

class LiveSwitch
{
public:
    explicit LiveSwitch(const char* name);
    LiveSwitch(const LiveSwitch&) = delete;
    LiveSwitch& operator=(const LiveSwitch&) = delete;
    operator bool() const { return value_.load(std::memory_order_relaxed); }
    const char* Name() const { return name_; }

    // The debug server's side: false if the name was never a switch (it's then kept for when one registers).
    static bool Set(const std::string& name, bool on);
    static std::vector<std::pair<std::string, bool>> List(); // the switches registered so far
    void SetFromWatch(bool on); // (live_switch.cpp's environment watch: FM3_* names, Fm3 "env NAME 1")

private:
    const char* name_;
    std::atomic<bool> value_;
};
