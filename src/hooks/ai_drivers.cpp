// Crash C1 (OPEN-ISSUES.md, sessions 14-15): some race loads threw std::out_of_range while setting up
// AI drivers.
//
// How FM3 sets up an AI driver (sub_822A1150: this, source, driver id; 0 = a generic pick):
// - For an opponent car the id is the car's own driver (sub_822E0960: "SELECT Data_Car.AIPlayerID FROM
//   Data_Car WHERE Data_Car.id="). With 0, sub_8229F848 runs "SELECT Id FROM AIPlayers WHERE
//   DriverTypeID = 3 AND isUsed = 0" on the game database (db = *(this + 16), db->vtable[52](&rowset,
//   db, sql)), takes the row count (rowset->vtable[144]) and reads the Id of row rand() % count.
// - sub_822A1150 reads the driver's Name ("SELECT Name FROM AIPlayers WHERE Id="), a string key like
//   "_&2149404878", parsed by sub_828AFD40 (.substr(2), sscanf "%u"; it parses other keys too), then
//   marks the driver used ("UPDATE AIPlayers SET isUsed = 1 WHERE Id=").
// - Drivers are released one by one (sub_822A0EB8, only when sub_82490ED8() says so) or all at once
//   ("UPDATE AIPlayers SET isUsed = 0", sub_8225ACE0 and sub_824854D0).
//
// What crashed (session 15): driver 125 (C. Reed; Name _&2149404878 in gamedb.slt) read back an
// empty Name on several loads, also when read again here, and .substr(2) threw. Its row in the
// in-memory database (one buffer, 1 KB pages) was intact at the menu and between the loads checked, so
// the cause is not known yet (LogDriverRow logs what SQLite holds when it happens).
//
// The hooks: (1) sub_822A1150: a car's driver with no row or an empty Name gets a generic driver
// instead (FM3_NO_AI_DRIVER_CHECK=1: off). (2) sub_828AFD40: an empty driver Name that still gets there
// (a generic pick of such a driver) is replaced by the re-read Name or the last good driver Name
// (FM3_NO_AI_NAME_FIX=1: off). (3) sub_8229F848: with no unused generic driver left (count 0: the
// console traps there, our code skips traps and divides by zero as 0, so the row index would be a raw
// rand()) the game's own reset runs first (FM3_NO_AI_POOL_RESET=1: only log). FM3_AI_POOL_LOG=1: log
// every driver and pick. All switches are read each time.
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "kernel/imports.h"
#include "kernel/thread.h"
#include "log.h"

namespace
{
constexpr uint32_t kSelectUnused = 0x82010848; // "SELECT Id FROM AIPlayers WHERE DriverTypeID = 3 AND isUsed = 0"
constexpr uint32_t kResetAll = 0x82007DA0;     // "UPDATE AIPlayers SET isUsed = 0"

// Runs `sql` on the database `db` (db->vtable[52]); returns the result object (0 none). The caller
// releases it (Release). Uses a frame below the current stack pointer (minus `reserve` bytes the
// caller keeps there, e.g. the SQL text).
uint32_t Query(PPCContext& ctx, uint32_t db, uint32_t sql, uint32_t reserve = 0)
{
    const uint32_t sp = ctx.r1.u32;
    ctx.r1.u32 = sp - reserve - 128;
    const uint32_t slot = ctx.r1.u32 + 80;
    StoreBE32(slot, 0);
    ctx.r3.u64 = slot;
    ctx.r4.u64 = db;
    ctx.r5.u64 = sql;
    GuestCall(ctx, LoadBE32(LoadBE32(db) + 52));
    const uint32_t result = LoadBE32(slot);
    ctx.r1.u32 = sp;
    return result;
}

void Release(PPCContext& ctx, uint32_t object)
{
    if (!object)
        return;
    ctx.r3.u64 = object;
    GuestCall(ctx, LoadBE32(LoadBE32(object) + 8));
}

int32_t CountRows(PPCContext& ctx, uint32_t db, uint32_t sql, uint32_t reserve = 0)
{
    const uint32_t rows = Query(ctx, db, sql, reserve);
    if (!rows)
        return -1;
    ctx.r3.u64 = rows;
    GuestCall(ctx, LoadBE32(LoadBE32(rows) + 144));
    const int32_t count = ctx.r3.s32;
    Release(ctx, rows);
    return count;
}

int32_t CountUnused(PPCContext& ctx, uint32_t db)
{
    return CountRows(ctx, db, kSelectUnused);
}

// A guest MSVC std::string: text inline at +4 while the capacity (+24) is below 16, else a pointer
// there; size at +20.
std::string ReadString(uint32_t s)
{
    const uint32_t size = LoadBE32(s + 20), cap = LoadBE32(s + 24);
    if (size > 4096)
        return "<bad size>";
    const uint32_t text = cap < 16 ? s + 4 : LoadBE32(s + 4);
    return std::string(GuestPtr<char>(text), size);
}

// Writes an inline (short) guest std::string with `text` (at most 15 characters) at `s`.
void WriteShortString(uint32_t s, const std::string& text)
{
    memset(GuestPtr<char>(s), 0, 28);
    memcpy(GuestPtr<char>(s + 4), text.data(), text.size());
    StoreBE32(s + 20, uint32_t(text.size()));
    StoreBE32(s + 24, 15);
}

// Runs `sql` (text, at most 160 characters) and reads the first row's `columns` (each at most 15
// characters: result->vtable[108](result, std::string* column) returns a std::string*, as sub_82777AF8
// does it). `rows`: the row count (-1: the query failed). Empty strings when anything fails.
std::vector<std::string> ReadColumns(PPCContext& ctx, uint32_t db, const std::string& sql, const std::vector<std::string>& columns, int32_t& rows)
{
    rows = -1;
    std::vector<std::string> values(columns.size());
    const uint32_t sp = ctx.r1.u32;
    const uint32_t text = (sp - 192) & ~15u, column = text - 32;
    memcpy(GuestPtr<char>(text), sql.c_str(), std::min<size_t>(sql.size(), 160) + 1);
    GuestPtr<char>(text)[std::min<size_t>(sql.size(), 160)] = 0;
    const uint32_t result = Query(ctx, db, text, 192 + 48);
    if (!result)
        return values;
    ctx.r3.u64 = result;
    GuestCall(ctx, LoadBE32(LoadBE32(result) + 144));
    rows = ctx.r3.s32;
    for (size_t i = 0; rows > 0 && i < columns.size(); i++)
    {
        WriteShortString(column, columns[i].substr(0, 15));
        ctx.r1.u32 = (column - 128) & ~15u;
        ctx.r3.u64 = result;
        ctx.r4.u64 = column;
        GuestCall(ctx, LoadBE32(LoadBE32(result) + 108));
        if (ctx.r3.u32)
            values[i] = ReadString(ctx.r3.u32);
        ctx.r1.u32 = sp;
    }
    Release(ctx, result);
    return values;
}

// The Name of driver `id`, read again here ("SELECT Name FROM AIPlayers WHERE Id=<id>").
std::string RereadName(PPCContext& ctx, uint32_t db, uint32_t id, int32_t& rows)
{
    return ReadColumns(ctx, db, "SELECT Name FROM AIPlayers WHERE Id=" + std::to_string(id), { "Name" }, rows)[0];
}

// Diagnostics for an empty Name (crash C1): what SQLite holds for that row at that moment (session 15
// found driver 125's database page intact between loads, so this catches the moment it reads back empty).
void LogDriverRow(PPCContext& ctx, uint32_t db, uint32_t id)
{
    // One query per value: session 16's single query with all of them failed (-1 rows: the game's SQLite may lack one
    // of these functions). c: Name with a marker appended (tells an empty SQLite value from one the result object
    // loses); rowid: the cell's key on its page.
    const std::string where = " FROM AIPlayers WHERE Id=" + std::to_string(id);
    const char* exprs[] = { "typeof(Name)", "length(Name)", "hex(Name)", "Name || '|'", "rowid", "LastName", "isUsed", "quote(Name)" };
    std::string line;
    for (const char* e : exprs)
    {
        int32_t rows = -1;
        const std::string v = ReadColumns(ctx, db, std::string("SELECT ") + e + " AS v" + where, { "v" }, rows)[0];
        line += std::string(", ") + e + (rows < 0 ? " <query failed>" : rows == 0 ? " <no row>" : " \"" + v + "\"");
    }
    Log("ai drivers: driver %u row%s", id, line.c_str());
}
// The driver sub_822A1150 is setting up on this thread (for sub_828AFD40's check).
thread_local uint32_t t_db = 0, t_id = 0;
thread_local bool t_inDriver = false; // inside sub_822A1150 (sub_828AFD40 parses other string keys too)
// The last driver Name that parsed (a stand-in for an empty one). Only driver names: session 15's
// first version also kept other keys (a car's history text) and gave that to a driver.
std::string g_lastGoodName;
}

// The AI driver setup (sub_822A1150: this r3, source r4, driver id r5; 0 = pick a generic driver via
// r4->vtable[12]). For an opponent car the id is the car's own driver (sub_822E0960: "SELECT
// Data_Car.AIPlayerID FROM Data_Car WHERE Data_Car.id="). An id with no AIPlayers row or an
// empty Name (crash C1) is replaced by 0: the game picks a generic driver for that car instead. FM3_NO_AI_DRIVER_CHECK=1 (read each time): off.
PPC_FUNC_IMPL(__imp__sub_822A1150);
PPC_FUNC(sub_822A1150)
{
    const uint32_t id = ctx.r5.u32;
    const uint32_t source = ctx.r4.u32;
    if (id && source && !getenv("FM3_NO_AI_DRIVER_CHECK"))
    {
        const uint32_t db = LoadBE32(source + 16); // (as sub_822A1150 reaches it)
        if (db)
        {
            // Session 15: driver 125 (C. Reed, Name _&2149404878 on disc) read back an empty Name on every load
            // (the in-memory database differs from the file there): treated like a missing driver.
            const PPCContext saved = ctx;
            int32_t rows = -1;
            const std::string name = RereadName(ctx, db, id, rows);
            ctx = saved;
            if (rows == 0 || (rows > 0 && name.size() < 2))
            {
                Log("ai drivers: car's driver id %u has %s (lr %08llX): a generic driver instead", id,
                    rows == 0 ? "no AIPlayers row" : "an empty Name in the in-memory database", saved.lr);
                if (rows > 0)
                {
                    LogDriverRow(ctx, db, id);
                    ctx = saved;
                }
                ctx.r5.u64 = 0;
            }
            else if (getenv("FM3_AI_POOL_LOG"))
                Log("ai drivers: car's driver id %u (%d rows, name \"%s\")", id, rows, name.c_str());
        }
    }
    t_db = source ? LoadBE32(source + 16) : 0;
    t_id = ctx.r5.u32; // (0: the generic pick inside; its id is not known here)
    t_inDriver = true;
    __imp__sub_822A1150(ctx, base);
    t_db = t_id = 0;
    t_inDriver = false;
}

PPC_FUNC_IMPL(__imp__sub_8229F848);
PPC_FUNC(sub_8229F848)
{
    const uint32_t db = LoadBE32(ctx.r3.u32 + 16);
    if (db)
    {
        const PPCContext saved = ctx;
        const int32_t count = CountUnused(ctx, db);
        static uint32_t picks = 0;
        picks++;
        if (count <= 0)
        {
            const bool reset = !getenv("FM3_NO_AI_POOL_RESET");
            Log("ai drivers: no unused generic AI driver left (count %d, pick %u, lr %08llX)%s", count, picks, saved.lr,
                reset ? ": resetting isUsed (the game's own UPDATE AIPlayers SET isUsed = 0)" : " (FM3_NO_AI_POOL_RESET: not reset)");
            if (reset)
            {
                Release(ctx, Query(ctx, db, kResetAll));
                Log("ai drivers: %d unused after the reset", CountUnused(ctx, db));
            }
        }
        else if (count <= 8 || getenv("FM3_AI_POOL_LOG"))
            Log("ai drivers: %d unused generic AI drivers before pick %u", count, picks);
        ctx = saved;
    }
    __imp__sub_8229F848(ctx, base);
}

// The parse that threw (sub_828AFD40: r3 out, r4 the Name string; .substr(2), sscanf "%u"). Session 15:
// driver 125 had its AIPlayers row, yet the game's own SELECT Name read back an empty string (cause
// not known yet). A Name shorter than its 2-character prefix is logged, read again here for the log,
// and replaced (on the guest stack) by the reread Name if that one is good, else by the last good Name,
// so the car gets a driver name instead of the crash. FM3_NO_AI_NAME_FIX=1 (read each time): off.
PPC_FUNC_IMPL(__imp__sub_828AFD40);
PPC_FUNC(sub_828AFD40)
{
    const uint32_t s = ctx.r4.u32;
    if (!s)
    {
        __imp__sub_828AFD40(ctx, base);
        return;
    }
    const std::string name = ReadString(s);
    if (name.size() >= 2)
    {
        if (!t_inDriver)
        {
            __imp__sub_828AFD40(ctx, base);
            return;
        }
        if (name.size() <= 15)
            g_lastGoodName = name;
        if (getenv("FM3_AI_POOL_LOG"))
            Log("ai drivers: driver %u name \"%s\"", t_id, name.c_str());
        __imp__sub_828AFD40(ctx, base);
        return;
    }
    std::string reread;
    int32_t rows = -1;
    if (t_db && t_id)
    {
        const PPCContext saved = ctx;
        reread = RereadName(ctx, t_db, t_id, rows);
        ctx = saved;
    }
    const bool fix = !getenv("FM3_NO_AI_NAME_FIX");
    std::string stand = reread.size() >= 2 && reread.size() <= 15 ? reread : g_lastGoodName;
    Log("ai drivers: driver %u name of %zu characters (lr %08llX); read again: %d rows, name \"%s\"; %s", t_id, name.size(), ctx.lr, rows,
        reread.c_str(), !fix ? "FM3_NO_AI_NAME_FIX: the parse will throw" : stand.empty() ? "no stand-in, the parse will throw" : ("using \"" + stand + "\"").c_str());
    if (!fix || stand.empty())
    {
        __imp__sub_828AFD40(ctx, base);
        return;
    }
    // The stand-in string below the stack pointer, the callee's frame below it.
    const uint32_t sp = ctx.r1.u32;
    const uint32_t str = (sp - 64) & ~15u;
    WriteShortString(str, stand);
    ctx.r1.u32 = (str - 16) & ~15u;
    ctx.r4.u64 = str;
    __imp__sub_828AFD40(ctx, base);
    ctx.r1.u32 = sp;
}
