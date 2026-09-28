// pool_maintenance.h — THE POOL'S MAINTAINER. One file: a pool comes into
// existence here and goes out of existence here. Nowhere else. It lives on
// the pool map, on LiveRegistry; this file holds nothing.
//
// ###########################################################################
// [[COW-EDIT]] — EVERY CHANGE FOR THE COPY-ON-WRITE MAP IS MARKED
// [[COW-EDIT n]] IN THIS FILE, pool_maintenance.cpp, live_registry.h AND
// watcher.cpp. Search "[[COW-EDIT" to find them all. The markers are review
// notes and are stripped once reviewed; they are not part of the header.
// ###########################################################################
//
// ===========================================================================
// [[COW-EDIT 1]] Preamble replaced with the standard one. Rules 8 and 11
// rewritten for copy-on-write; rules 12 and 13 added.
//
// OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by
// definition. It is raised with the user, never made.
//
// 1. A POOL IS BYTES. NEVER TOKENS.
//    No field, function, or comment on this file ever expresses size,
//    capacity, or content in tokens. Multiple agents with different
//    tokenisers read the same pool; there is no shared token unit to size
//    against.
//
// 2. A POOL HAS NO NAME. POOL ID IS THE ONLY IDENTITY.
//    No human-readable label is ever added to a pool, and nothing is ever
//    keyed by one.
//
// 3. CLASS ID IS READ, NEVER HELD, NEVER WRITTEN BACK.
//    This file never hardcodes a Class ID as a literal, never re-derives
//    one, and never writes to the class table. A caller supplies the number
//    or the name; a name is resolved by a direct read at that moment, every
//    time.
//
// 4. THE STAMP IS FIXED AT MINT.
//    Pool ID, Turn ID, Prompt ID(s), timestamp never change after creation.
//    Class ID is the only field Reclassify may change, and Reclassify
//    changes nothing else — no cascading writes, no side effects, no reach
//    into any other file's state.
//
// 5. THIS FILE HOLDS NOTHING.
//    No map, no list, no record of any pool, no copy of the map. The map is
//    on LiveRegistry, read through the screen; this file states its layout
//    and edits it there. It
//    offers no lookup, no find, no enumeration of pools by any criterion,
//    to any caller.
//
// 6. ONE FILTER, NO VARIANTS.
//    Destroy, Flag, and Unflag share the one filter shape — Class ID, Turn
//    ID, Prompt ID, Pool ID, AND'd, with exclusions. A new combination a
//    caller needs is expressed through that filter. It is never given its
//    own function.
//
// 7. IMMUNITY IS SCOPED, NEVER BLANKET.
//    A flag protects a pool from exactly the source named and no other.
//    Destroy checks this as part of its own execution; no caller ever
//    performs that check itself.
//
// 8. NO REFUSAL IS SILENT. NOTHING IS REVERTED.
//    A create, grow, or reclassify that cannot complete returns nothing
//    usable and leaves nothing standing in place of what was asked for. A
//    wellness flag reports the true outcome; no stand-in value is ever
//    substituted for a missing one. Nothing is ever undone to tidy up.
//
// 9. THIS FILE IS MECHANICAL.
//    It never decides whether a pool should be destroyed, flagged, or kept.
//    It executes exactly what a caller, holding that authority, tells it to
//    do.
//
// 10. CHUNK SIZE IS THE OS'S FACT, NOT THIS FILE'S FIGURE.
//    One chunk is one unit as the OS reports it, uniform across every pool,
//    and never written onto an entry. No byte count is ever invented or
//    hardcoded here in its place.
//
// 11. EVERY EDIT IS COPY-ON-WRITE, PER UNIT, AND NEVER WAITS ON A READER.
//    An edit writes a new version of exactly the units it touches and
//    places it live. A version a present reader can still see is kept, for
//    that reader alone, until no reader that arrived before it was replaced
//    remains. No edit ever alters a version a reader can see. There is no
//    whole-map copy or freeze anywhere.
//
// 12. THIS FILE GRANTS EVERY READ — OF THE MAP AND OF A POOL'S BYTES.
//    Arrive and Leave are the only way onto the map or into a pool, and a
//    read is exactly the span between them. A read cannot be taken anywhere,
//    passed on, or kept beyond its reader. Nothing reaches the map or a
//    pool's bytes by any other path.
//
// 13. DESTROY ALONE ENDS A POOL, AND ONLY ONCE ITS LAST READER HAS LEFT.
//    Destroy flags the pool for destruction and refuses every new arrival
//    on it. Readers already in it carry on unrestricted. When the last one
//    leaves, the pool leaves the map and its chunks are released. Nothing
//    else pre-empts or brings forward a destruction.
// ===========================================================================
//
// WHAT A POOL IS
//   One unit on the pool map. Its identity, its Class ID, its immunity, its
//   destruction flag and its chunks are fields on that one unit — not a
//   table beside it, not a record in this file, not a lookup anywhere.
//   Anything needing a fact about a pool reads the map. This file is the
//   only thing that CREATES, GROWS, RECLASSIFIES, FLAGS, or DESTROYS a pool.
//   Content is written directly by whatever is generating; it is not this
//   file's.
//
// THE STAMP
//   Pool ID (minted by IdGeneration), Class ID, Turn ID, Prompt ID(s),
//   timestamp: set at the instant of creation. Turn ID may be empty (minted
//   before any turn exists). Prompt ID(s) may be empty (start of a chain, or
//   no prompt). Empty is a fact, not a branch. Prompt ID(s) are copied off
//   the ONE pool this one continues — read directly off that pool's entry.
//
// SIZING
//   A pool is bytes, held as chunks of one uniform size. Mint takes one
//   chunk. Every grow takes one more. Whatever is writing decides WHEN to
//   grow, by reading capacity off the entry and calling grow. This file
//   never watches for that.
//
// CREATE AND ITS CALLER
//   The caller fires and moves on. Create hands nothing back to it — the
//   outcome goes to Wellness. The one exception is the Pool ID, and only when
//   the caller asked for it. Each create is one pool: a refusal touches that
//   pool alone, undoes nothing already standing, and stops nothing else a
//   caller is minting.
//
// [[COW-EDIT 2]] THE MAP AND ITS READS (new section)
//   Every read of the map is on the screen (live_registry.h). The map is
//   one reserved region of units, one unit per pool position.
//   Each unit carries its versions, newest first, each stamped with the
//   map clock tick at which it went live, and the gate onto that pool's
//   bytes. The map carries its own gate. A gate is where this file grants
//   arrivals and knows every reader present.
//   Arriving on the map stamps the reader with the clock. Every access
//   resolves each unit separately: the newest version that went live no
//   later than the reader's stamp. A reader therefore sees live, except for
//   units edited since it arrived, where it sees what was there when it
//   arrived.
//   A pool's gate is the same gate. Destroy closes it; a closed gate refuses
//   every arrival.
//
// WELLNESS
//   Bare booleans, named for what they answer, set at the instant they are
//   answered, never read again here. Wellness sees them because they exist.
//
// OS_OWES
//   The marker word (live_registry.h). The OS supplies the chunks; each is
//   declared where it is called. [[COW-EDIT 3]]

#pragma once

#include "live_registry.h"

#include <atomic>     // [[COW-EDIT 4]]
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>        // [[COW-EDIT 4]]
#include <string>
#include <variant>
#include <vector>

namespace prime {

// ---------------------------------------------------------------------------
// A CLASS, AS THE CALLER HAS IT — the number, or the declared name.
// ---------------------------------------------------------------------------
using ClassRef = std::variant<std::uint64_t, std::string>;

// ---------------------------------------------------------------------------
// THE FILTER — how destroy, flag and unflag select pools. AND across every
// criterion supplied; `exclude` skips those Pool IDs even when they match.
// ---------------------------------------------------------------------------
struct PoolFilter {
    std::optional<std::uint64_t> class_id;
    std::optional<std::string>   turn_id;
    std::optional<std::string>   prompt_id;   // matches if the pool carries it
    std::optional<std::string>   pool_id;
    std::vector<std::string>     exclude;
};

// ===========================================================================
// [[COW-EDIT 5]] THE POOL MAP — LAYOUT (new). Previously referenced as
// Pool / PoolMap / PoolMapImage but defined nowhere; defined here.
// ===========================================================================

// One pool: the whole of one unit's content.
struct Pool {
    std::string                pool_id;
    std::uint64_t              class_id     = 0;
    std::string                turn_id;
    std::set<std::string>      prompt_ids;
    std::uint64_t              timestamp_ns = 0;
    std::vector<std::uint8_t*> chunks;
    std::uint64_t              byte_capacity = 0;
    std::vector<std::string>   immune_from;
    bool                       flagged_for_destruction = false;   // [[COW-EDIT 6]]
};

// Where reads are granted. The map has one; every unit has one for its
// pool's bytes. `holders` is every reader present, by stamp.
struct Gate {
    std::mutex                   m;
    std::uint64_t                clock  = 0;   // the map's gate only
    std::multiset<std::uint64_t> holders;
    bool                         closed = false;
};

// One version of one unit. Never altered once live.
struct PoolVersion {
    Pool          pool;
    bool          present = false;   // false: no pool in this unit
    std::uint64_t born    = 0;       // clock tick it went live
    std::uint64_t retired = 0;       // clock tick it was replaced
    PoolVersion*  older   = nullptr;
};

// One unit: one pool position.
struct MapUnit {
    std::atomic<PoolVersion*> head{nullptr};
    Gate                      gate;
};

// [[COW-EDIT 7 | PROVISIONAL — unit granularity, spec default: one unit =
// one pool record + its chunk list]]. The units are the one contiguous
// region; versions are held apart from it.
struct PoolMap {
    MapUnit*                  units      = nullptr;
    std::uint64_t             unit_count = 0;
    Gate                      gate;
    std::vector<PoolVersion*> retired;   // replaced, still visible to a reader
};

// A granted read. Not copyable to anyone else's use; it is the reader's own.
// `unit` null: a read of the map. `granted` false: refused.
struct PoolRead {
    MapUnit*      unit    = nullptr;
    std::uint64_t stamp   = 0;
    bool          granted = false;
};

// The per-access resolution: this unit as this map read sees it. Null when
// the reader sees no pool there.
inline const Pool* resolve(const MapUnit& u, const PoolRead& r) {
    const PoolVersion* v = u.head.load(std::memory_order_acquire);
    while (v != nullptr && v->born > r.stamp) v = v->older;
    return v != nullptr && v->present ? &v->pool : nullptr;
}

// ---------------------------------------------------------------------------
// POOL MAINTENANCE
// ---------------------------------------------------------------------------
class PoolMaintenance {
public:
    PoolMaintenance() = default;

    PoolMaintenance(const PoolMaintenance&)            = delete;
    PoolMaintenance& operator=(const PoolMaintenance&) = delete;

    // ---- [[COW-EDIT 8]] arrive / leave — every read, map or pool ----------
    // Arrive on the screen (null) or on one unit's pool. Refused — not granted —
    // when that pool is flagged for destruction or gone. Leave ends the
    // read; the last leave from a flagged pool destroys it.
    PoolRead arrive(MapUnit* unit);
    void     leave(const PoolRead& read);

    // ---- create: the moment of need --------------------------------------
    // Mint a pool NOW, with its first chunk. One call for every pool.
    //
    //   cls            — the Class ID number, or the declared name; a name is
    //                    read off LiveRegistry here, at this moment.
    //   turn_id        — the turn this pool belongs to; empty before any turn.
    //   continues_from — the ONE pool this continues; its prompt id(s) are
    //                    copied off that pool's entry. Empty at the start of
    //                    a chain.
    //   pool_id_out    — the caller's request for the new Pool ID back. Most
    //                    callers have no use for it and pass nothing; one that
    //                    keeps its own record (ProjectIngest) passes where it
    //                    wants it written. Written only when the pool stands
    //                    on the map.
    //
    // Nothing is returned. Whether the pool stands is posted to Wellness.
    // Refused — no pool left standing — when the class resolves to nothing,
    // continues_from names a pool that does not exist or is flagged for
    // destruction, the first chunk cannot be taken, or the map has no free
    // unit. [[COW-EDIT 9]]
    void create(const ClassRef&    cls,
                const std::string& turn_id,
                const std::string& continues_from = std::string(),
                std::string*       pool_id_out    = nullptr);

    // ---- grow ---------------------------------------------------------------
    // One more chunk. False when the pool does not exist, is flagged for
    // destruction, or the OS has no chunk to give. [[COW-EDIT 10]]
    bool grow(const std::string& pool_id);

    // ---- destroy / flag / unflag — one filter, one source ----------------
    // Each returns how many pools it acted on. [[COW-EDIT 11]] Destroy flags
    // for destruction and closes the pool's gate; the pool leaves the map
    // when its last reader leaves, at once if it has none. A pool already
    // flagged for destruction is not acted on again, by any of the three.
    std::uint64_t destroy(const PoolFilter& filter, const std::string& source);
    std::uint64_t flag   (const PoolFilter& filter, const std::string& source);
    std::uint64_t unflag (const PoolFilter& filter, const std::string& source);

    // ---- reclassify --------------------------------------------------------
    enum class Reclassify { Done, NotFound };   // [[COW-EDIT 12]]

    // The new class, as the caller has it. Changes Class ID on the entry and
    // nothing else. NotFound when the pool does not exist, is flagged for
    // destruction, or the class resolves to nothing.
    Reclassify reclassify(const std::string& pool_id, const ClassRef& cls);

private:
    // The number the caller gave, or the number LiveRegistry holds for the
    // name it gave. Zero is "no such class". Used by create and reclassify.
    static std::uint64_t resolve_class(const ClassRef& cls);

    // Every criterion supplied must match; exclusions win. Used by destroy,
    // flag and unflag — the one matcher.
    static bool matches(const Pool& p, const PoolFilter& f);

    // One chunk from the OS onto the end of the pool. Caller holds the lock.
    bool take_chunk_locked(Pool& p);

    // [[COW-EDIT 13]] The one edit: a new version of one unit goes live.
    // Caller holds the lock.
    void write_unit_locked(PoolMap& map, MapUnit& u, Pool next, bool present);

    // [[COW-EDIT 14]] The unit holding this pool, live and not flagged for
    // destruction; null if none. Used by create, grow, reclassify.
    static MapUnit* live_unit(PoolMap& map, const std::string& pool_id);

    // [[COW-EDIT 15]] The pool leaves the map; its chunks go back. Its gate
    // is closed and empty. Caller holds the lock. Used by destroy and leave.
    void end_pool_locked(PoolMap& map, MapUnit& u);

    // [[COW-EDIT 16]] Versions no present map reader can see are released.
    // Caller holds the map gate.
    static void release_unseen(PoolMap& map);

    // This file's callers, one at a time on the map. Not the map's lock —
    // the map has none; readers never wait on an edit.
    mutable std::mutex mutex_;
};

// [[COW-EDIT 17]] The one instance (was declared only inside rules.cpp).
PoolMaintenance& pool_maintenance();

} // namespace prime
