// Finds every shader in the game data before the game asks for one (regpu::Host::staticShaders),
// so ReGPU compiles them all on the first boot (its progress overlay is up meanwhile) instead of
// stalling a race the first time a car or track needs one. Ported from the FM4 ReGPU port
// (src/static_shaders.cpp, MIT; NOTICE.md), session 24.
//
// FM3 keeps its shader microcode in .fxobj effect files, loose under Media/Shaders and inside the
// track and car zips (LZX, method 21), 4-byte aligned with no packet header, plus some in the
// executable. A candidate is a shader when its control flow decodes cleanly: valid opcodes up to
// an end flag, the first exec address equal to the control-flow length, and exec ranges that tile
// the instruction slots without gaps. Only the used slots are kept. The result is saved in the
// user folder (static_shaders.bin) with a stamp of the files searched, so later boots skip the
// search and a changed game folder searches again.
//
// The vertex shaders found are the copies on disc: the title patches their vertex fetches when it
// binds them to a vertex layout, so they reach the GPU under ReGPU's canonical key (fetch fields
// masked), not their own hash. Installed Disc 2 packages (<user>/dlc/<package>, those with a
// <package>.license) are searched too (session 28): their track zips hold ~240 .fxobj files, and
// installing one changes the stamp, so the next boot searches again.
#include "static_shaders.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_set>

#include <windows.h>

#include "kernel/runtime.h"
#include "log.h"
#include "memory.h"
#include "recomp_modules.h"
#include "zip.h"

namespace
{
bool IsExecOp(uint32_t op) { return (op >= 1 && op <= 6) || op == 13 || op == 14; }
bool IsEndOp(uint32_t op) { return op == 2 || op == 4 || op == 6 || op == 14; }

uint32_t Be(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// Used length in dwords of the shader at words[start] (0 if it isn't one); sets pixel.
uint32_t ShaderAt(const uint8_t* bytes, size_t words, size_t start, bool& pixel)
{
    struct Cf { uint32_t op, lo, hi; };
    Cf cfs[130];
    uint32_t ncf = 0, triples = 0;
    for (bool ended = false; !ended;)
    {
        if (triples > 64 || start + triples * 3 + 2 >= words)
            return 0;
        const uint8_t* p = bytes + (start + triples * 3) * 4;
        const uint32_t w0 = Be(p), w1 = Be(p + 4), w2 = Be(p + 8);
        const uint32_t pair[2][2] = { { w0, w1 & 0xFFFF }, { (w1 >> 16) | (w2 << 16), w2 >> 16 } };
        triples++;
        for (const auto& c : pair)
        {
            const uint32_t op = (c[1] >> 12) & 0xF;
            cfs[ncf++] = { op, c[0], c[1] };
            if (IsEndOp(op))
            {
                ended = true;
                break;
            }
        }
    }
    struct Exec { uint32_t addr, count, seq; };
    Exec execs[130];
    uint32_t nexec = 0;
    for (uint32_t i = 0; i < ncf; i++)
    {
        const Cf& c = cfs[i];
        if (c.op == 12 && ((c.lo & 0xFFFFFFF8) || (c.hi & 0x09FF)))
            return 0; // alloc with stray bits
        if (IsExecOp(c.op) && ((c.lo >> 12) & 7))
            execs[nexec++] = { c.lo & 0xFFF, (c.lo >> 12) & 7, (c.lo >> 16) & 0xFFF };
    }
    if (!nexec)
        return 0;
    std::sort(execs, execs + nexec, [](const Exec& a, const Exec& b) { return a.addr < b.addr; });
    uint32_t slot = triples;
    for (uint32_t i = 0; i < nexec; i++)
    {
        if (execs[i].addr != slot)
            return 0;
        slot += execs[i].count;
    }
    if (slot > 512 || start + slot * 3 > words)
        return 0;
    bool vertex = false;
    for (uint32_t i = 0; i < nexec && !vertex; i++)
        for (uint32_t k = 0; k < execs[i].count; k++)
            if ((execs[i].seq >> (2 * k)) & 1)
                if ((Be(bytes + (start + (execs[i].addr + k) * 3) * 4) & 0x1F) == 0)
                    vertex = true;
    bool position = false;
    for (uint32_t i = 0; i < ncf; i++)
        if (cfs[i].op == 12 && ((cfs[i].hi >> 9) & 3) == 1)
            position = true;
    pixel = !vertex && !position;
    return slot * 3;
}

uint64_t Fnv(const uint8_t* p, size_t n, uint64_t h = 0xCBF29CE484222325ull)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

// Finds that never compile and that the game never draws (session 24): left out so the log's "failed to compile"
// check after translator changes stays clean. vs 8f3951a3cb05a9ea: FXC X3504 (array index out of bounds), a
// translator limit on a shader none of our runs used.
constexpr uint64_t kSkip[] = { 0x8f3951a3cb05a9eaull };

struct Found
{
    std::mutex mutex;
    std::unordered_set<uint64_t> seen;
    std::vector<regpu::ScannedShader> shaders;
};

// Every dword is a candidate start: .fxobj files align shaders to 4 bytes only.
void ScanBytes(const uint8_t* bytes, size_t size, Found& found)
{
    const size_t words = size / 4;
    for (size_t i = 0; i + 4 <= words; i++)
    {
        const uint32_t w0 = Be(bytes + i * 4), w1 = Be(bytes + i * 4 + 4);
        const uint32_t op = (w1 >> 12) & 0xF, addr = w0 & 0xFFF;
        if (!((IsExecOp(op) && addr >= 1 && addr <= 64) || (op == 12 && !(w0 & 0xFFFFFFF8))))
            continue;
        bool pixel;
        const uint32_t used = ShaderAt(bytes, words, i, pixel);
        if (!used)
            continue;
        const uint8_t* code = bytes + i * 4;
        const uint64_t hash = Fnv(code, used * 4) ^ (pixel ? 1 : 0);
        if (std::find(std::begin(kSkip), std::end(kSkip), hash) != std::end(kSkip))
            continue;
        std::lock_guard lock(found.mutex);
        if (!found.seen.insert(hash).second)
            continue;
        regpu::ScannedShader s{ pixel, std::vector<uint32_t>(used) };
        memcpy(s.code.data(), code, used * 4);
        found.shaders.push_back(std::move(s));
    }
}

constexpr uint32_t kFileMagic = 0x53334D46; // 'FM3S'
constexpr uint32_t kFileVersion = 2; // 2: kSkip applied

std::filesystem::path SavedPath() { return UserDir() / "static_shaders.bin"; }

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](char c) { return char(tolower(c)); });
    return s;
}

// The files searched (.fxobj and .zip under the game folder and in installed Disc 2 packages) and a
// stamp of their names and sizes.
std::vector<std::filesystem::path> SearchFiles(uint32_t& stamp)
{
    std::vector<std::pair<std::string, std::filesystem::path>> named; // (stamp name, path)
    std::error_code ec;
    auto add = [&](const std::filesystem::path& root, const std::string& prefix) {
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const std::string ext = Lower(it->path().extension().string());
            if (ext == ".zip" || ext == ".fxobj")
                named.emplace_back(prefix + Lower(std::filesystem::relative(it->path(), root, ec).generic_string()), it->path());
        }
        ec.clear();
    };
    add(GameDir(), "");
    for (const auto& package : std::filesystem::directory_iterator(DlcDir(), ec))
        if (package.is_directory() && std::filesystem::exists(DlcDir() / (package.path().filename().string() + ".license"), ec))
            add(package.path(), "dlc/" + Lower(package.path().filename().string()) + "/");
    std::sort(named.begin(), named.end());
    std::vector<std::filesystem::path> files;
    uint64_t h = 0xCBF29CE484222325ull;
    for (const auto& [rel, f] : named)
    {
        const uint64_t size = std::filesystem::file_size(f, ec);
        h = Fnv(reinterpret_cast<const uint8_t*>(rel.data()), rel.size(), h);
        h = Fnv(reinterpret_cast<const uint8_t*>(&size), sizeof(size), h);
        files.push_back(f);
    }
    stamp = uint32_t(h ^ (h >> 32));
    return files;
}

bool ReadHead(std::ifstream& in, uint32_t (&head)[4])
{
    return in.read(reinterpret_cast<char*>(head), sizeof(head)) && head[0] == kFileMagic && head[1] == kFileVersion;
}

bool LoadSaved(uint32_t stamp, std::vector<regpu::ScannedShader>& out)
{
    std::ifstream in(SavedPath(), std::ios::binary);
    uint32_t head[4];
    if (!ReadHead(in, head) || head[2] != stamp)
        return false;
    out.resize(head[3]);
    for (auto& s : out)
    {
        uint32_t meta[2];
        if (!in.read(reinterpret_cast<char*>(meta), sizeof(meta)) || meta[1] > 0x2000)
            return false;
        s.pixel = meta[0] != 0;
        s.code.resize(meta[1]);
        if (!in.read(reinterpret_cast<char*>(s.code.data()), std::streamsize(meta[1] * 4)))
            return false;
    }
    return true;
}

void Save(uint32_t stamp, const std::vector<regpu::ScannedShader>& shaders)
{
    std::ofstream out(SavedPath(), std::ios::binary);
    const uint32_t head[4] = { kFileMagic, kFileVersion, stamp, uint32_t(shaders.size()) };
    out.write(reinterpret_cast<const char*>(head), sizeof(head));
    for (const auto& s : shaders)
    {
        const uint32_t meta[2] = { s.pixel ? 1u : 0u, uint32_t(s.code.size()) };
        out.write(reinterpret_cast<const char*>(meta), sizeof(meta));
        out.write(reinterpret_cast<const char*>(s.code.data()), std::streamsize(s.code.size() * 4));
    }
}

std::vector<std::filesystem::path> g_files;
uint32_t g_stamp = 0;
bool g_listed = false;

void ListOnce()
{
    if (!g_listed)
    {
        g_files = SearchFiles(g_stamp);
        g_listed = true;
    }
}
}

bool StaticShadersSaved()
{
    ListOnce();
    std::ifstream in(SavedPath(), std::ios::binary);
    uint32_t head[4];
    return ReadHead(in, head) && head[2] == g_stamp;
}

std::vector<regpu::ScannedShader> StaticShaders()
{
    std::vector<regpu::ScannedShader> result;
    if (getenv("FM3_NO_STATIC_SHADERS"))
        return result;
    const auto start = std::chrono::steady_clock::now();
    ListOnce();
    if (LoadSaved(g_stamp, result))
    {
        Log("static shaders: %zu loaded from %s", result.size(), SavedPath().filename().string().c_str());
        return result;
    }
    Found found;
    std::atomic<size_t> next{ 0 }, members{ 0 };
    std::vector<std::thread> workers;
    const unsigned n = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < n; t++)
        workers.emplace_back([&] {
            // Below normal (as FM2's scan): the search may overlap the boot's own threads.
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            for (size_t i; (i = next++) < g_files.size();)
            {
                if (Lower(g_files[i].extension().string()) == ".zip")
                {
                    ForEachZipMemberInFile(g_files[i], ".fxobj", [&](const std::string&, const std::vector<uint8_t>& member) {
                        ScanBytes(member.data(), member.size(), found);
                        members++;
                    });
                    continue;
                }
                std::ifstream in(g_files[i], std::ios::binary | std::ios::ate);
                if (!in)
                    continue;
                std::vector<uint8_t> data(size_t(in.tellg()));
                in.seekg(0);
                in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
                ScanBytes(data.data(), data.size(), found);
                members++;
            }
        });
    for (auto& w : workers)
        w.join();
    const size_t fromFiles = found.shaders.size();
    // Shaders built into the executable (it is compressed on disc; scan the loaded image).
    const RecompiledModule& title = g_recompiledModules[0];
    ScanBytes(GuestPtr<uint8_t>(title.imageBase), title.imageSize, found);
    result = std::move(found.shaders);
    Save(g_stamp, result);
    size_t pixel = 0;
    for (const auto& s : result)
        pixel += s.pixel;
    Log("static shaders: %zu found (%zu pixel; %zu in %zu .fxobj files of %zu searched, %zu in the executable) in %.1f s", result.size(), pixel,
        fromFiles, size_t(members), g_files.size(), result.size() - fromFiles,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return result;
}
