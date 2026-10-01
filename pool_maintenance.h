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
//    [[COW-EDIT 37]] An edit touches only the units it changes. A unit no
//    present reader is pinned to is written in place, with no copy. A unit
//    that readers already present are pinned to has its pre-edit content
//    copied once into a stash, those readers are pinned to the stash, and
//    the edit is written live. A stash is held for exactly the readers
//    pinned to it and its bytes are released the instant the last of them
//    leaves. There is no whole-map copy, snapshot, or freeze anywhere, and
//    no edit ever waits, checks for permission, or holds.
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
//
// 14. THE MAP IS ONE CONTIGUOUS REGION OF FIXED-SIZE UNITS. [[COW-EDIT 38]]
//    The map is the set of pools: a pool's unit holding it and the pool
//    existing are the same fact. No second store, no log, no derivation.
//    Ownership runs strictly downward: map, unit, chunk list, bytes. The map
//    never holds pool bytes, and nothing here ever copies them; a stash is
//    a copy of a unit, never of a pool's bytes.
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
// [[COW-EDIT 2]] THE MAP AND ITS READS (new section) — rewritten to the
// copy-on-write spec [[COW-EDIT 39]]
//   Every read of the map is on the screen (live_registry.h). No third path
//   to the map exists.
//
//   ARRIVE. A reader arrives on the screen and is granted a read: a live
//   binding, not a copy. Its stamp is the map clock at that instant.
//   LEAVE. The reader releases its read. Every stash it is pinned to loses
//   one reader; a stash left with none has its bytes released at once.
//   DIE. A reader that ends without leaving is left for by the system, the
//   same leave, detected without the reader's cooperation.
//   A read is not transferable, not storable, not kept beyond its reader.
//   The map cannot be taken anywhere, only read.
//
//   EDIT, per unit touched. The clock ticks. The readers pinned to live —
//   present, arrived since this unit was last forked — are counted.
//     None: the edit is written in place.
//     Some: the pre-edit content is copied once into a stash carrying that
//     count and the span of stamps it serves; those readers are now pinned
//     to it; then the edit is written live.
//   Several edits to one unit while older readers remain give several
//   stashes, each with its own count. Only the units an edit touches are
//   ever forked.
//
//   THE KEY. Before its first visit of the session, a reader asks this
//   file for the map key and keeps it. It reads the screen only through the
//   key. [[COW-EDIT 69]]
//
//   RESOLUTION, on every access, per unit: the stash this reader is pinned
//   to for that unit, if there is one; otherwise live. A reader sees live,
//   except for the units forked out from under it, which it sees as they
//   were when it arrived.
//
//   A pool's bytes are gated the same way: a reader arrives on the pool, is
//   a current reader until it leaves, and a pool flagged for destruction
//   admits no one.
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

// One pool: the whole of one unit's content. No generation marker.
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

// Where reads are granted. The screen has one; every unit has one for its
// pool's bytes. `holders` is every reader present, by stamp.
struct Gate {
    std::mutex                   m;
    std::uint64_t                clock  = 0;   // the screen's gate only
    std::multiset<std::uint64_t> holders;
    bool                         closed = false;
};

// [[COW-EDIT 40]] A stash: one unit's pre-edit content, held for the readers
// present when the edit came — those stamped in [from, to). `pinned` is how
// many of them still hold their read. At zero its bytes are released; the
// empty stash itself goes once no reader stamped before `to` remains.
struct Stash {
    Pool                content;
    bool                present = false;
    std::uint64_t       from    = 0;
    std::uint64_t       to      = 0;
    std::uint64_t       pinned  = 0;
    std::atomic<Stash*> older{nullptr};
};

// [[COW-EDIT 41]] One unit: one pool position, fixed size, in the region.
// Its live content is written in place. Readers stamped at or after
// `live_from` see live; earlier readers see their stash.
struct MapUnit {
    Pool                       live;
    std::atomic<bool>          present{false};
    std::atomic<std::uint64_t> live_from{0};
    std::atomic<Stash*>        stashes{nullptr};   // newest first
    Gate                       gate;
};

// [[COW-EDIT 7 | PROVISIONAL — unit granularity, spec default: one unit =
// one pool record + its chunk list]]. The units are the one contiguous
// region.
struct PoolMap {
    MapUnit*      units      = nullptr;
    std::uint64_t unit_count = 0;
    Gate          gate;
};

// A granted read. Not copyable to anyone else's use; it is the reader's own.
// `unit` null: a read of the map, on the screen. `granted` false: refused.
struct PoolRead {
    MapUnit*      unit    = nullptr;
    std::uint64_t stamp   = 0;
    bool          granted = false;
};

// [[COW-EDIT 64]] THE MAP KEY — how to read the screen. Handed out by Pool
// Maintenance; a map reader asks for it once, before its first visit of the
// session, and keeps it. Every position is a byte offset. A reader reads the
// map only through the key, never through the layout above, so a change to
// the layout is a change to Pool Maintenance alone.
struct MapKey {
    // the map, from the screen
    std::uint64_t units = 0, unit_count = 0, unit_size = 0;
    // one unit
    std::uint64_t unit_present = 0, unit_live_from = 0, unit_stashes = 0, unit_live = 0;
    // one stash
    std::uint64_t stash_present = 0, stash_from = 0, stash_to = 0, stash_content = 0, stash_older = 0;
    // one pool record, live or stashed
    std::uint64_t pool_id = 0, class_id = 0, turn_id = 0, prompt_ids = 0, timestamp_ns = 0,
                  byte_capacity = 0, flagged_for_destruction = 0;
};

// [[COW-EDIT 65]] One field, at the key's offset, as the type the key names it.
template <class T>
inline const T& map_field(const std::uint8_t* at, std::uint64_t offset) {
    return *reinterpret_cast<const T*>(at + offset);
}

// [[COW-EDIT 66]] The unit at position `unit`, found through the key.
inline const std::uint8_t* map_unit(const MapKey& k, const void* screen, std::uint64_t unit) {
    const std::uint8_t* units = map_field<const std::uint8_t*>(static_cast<const std::uint8_t*>(screen), k.units);
    return units + unit * k.unit_size;
}

// [[COW-EDIT 67]] The per-access resolution, through the key: the pool
// record this read sees in that unit — its pinned stash if it has one,
// otherwise live. Null when it sees no pool there.
inline const std::uint8_t* map_record(const MapKey& k, const void* screen,
                                      std::uint64_t unit, std::uint64_t stamp) {
    const std::uint8_t* u = map_unit(k, screen, unit);
    if (stamp >= map_field<std::atomic<std::uint64_t>>(u, k.unit_live_from).load(std::memory_order_acquire))
        return map_field<std::atomic<bool>>(u, k.unit_present).load(std::memory_order_acquire) ? u + k.unit_live : nullptr;
    for (const std::uint8_t* s = reinterpret_cast<const std::uint8_t*>(
             map_field<std::atomic<Stash*>>(u, k.unit_stashes).load(std::memory_order_acquire));
         s != nullptr;
         s = reinterpret_cast<const std::uint8_t*>(
             map_field<std::atomic<Stash*>>(s, k.stash_older).load(std::memory_order_acquire)))
        if (map_field<std::uint64_t>(s, k.stash_from) <= stamp && stamp < map_field<std::uint64_t>(s, k.stash_to))
            return map_field<bool>(s, k.stash_present) ? s + k.stash_content : nullptr;
    return nullptr;
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
    // Arrive on the screen (null) or on one unit's pool. Refused — not
    // granted — when that pool is flagged for destruction or gone. Leave
    // ends the read: every stash it was pinned to loses it, and the last
    // leave from a flagged pool destroys it. `died`: the system leaving for
    // a reader that ended without leaving (DIE). [[COW-EDIT 43]]
    // [[COW-EDIT 68]] The map key, whole. Asked for once per reader per
    // session, before its first visit to the screen.
    MapKey   map_key() const;

    PoolRead arrive(MapUnit* unit);
    void     leave(const PoolRead& read, bool died = false);

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

    // [[COW-EDIT 13]] The one edit, per unit: forked into a stash only where
    // present readers are pinned to live, then written live. Caller holds
    // the lock.
    void write_unit_locked(PoolMap& map, MapUnit& u, Pool next, bool present);

    // [[COW-EDIT 14]] The unit holding this pool, live and not flagged for
    // destruction; null if none. Used by create, grow, reclassify.
    static MapUnit* live_unit(PoolMap& map, const std::string& pool_id);

    // [[COW-EDIT 15]] The pool leaves the map; its chunks go back. Its gate
    // is closed and empty. Caller holds the lock. Used by destroy and leave.
    void end_pool_locked(PoolMap& map, MapUnit& u);

    // [[COW-EDIT 16]] Empty stashes no present reader can reach are dropped.
    // Caller holds the screen's gate.
    static void release_unseen(PoolMap& map);

    // This file's callers, one at a time on the map. Not the map's lock —
    // the map has none; readers never wait on an edit.
    mutable std::mutex mutex_;
};

// [[COW-EDIT 17]] The one instance (was declared only inside rules.cpp).
PoolMaintenance& pool_maintenance();

} // namespace prime
