// pool_maintenance.h — THE POOL'S MAINTAINER. One file: a pool comes into
// existence here and goes out of existence here. Nowhere else. It lives on
// the pool map, which this file owns.
//
// ===========================================================================
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
// 5. THIS FILE OWNS THE MAP, PRIVATELY.
//    The map is the set of pools: a pool's unit holding it and the pool
//    existing are the same fact. No second store, no log, no derivation.
//    The map is held here and nowhere else. No copy of it, or of any part
//    of it, leaves this file. It offers no lookup, no find, no enumeration
//    of pools by any criterion, to any caller.
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
// 11. EVERY EDIT BUILDS, THEN SWAPS, AND NEVER WAITS.
//    An edit touches only the units it changes. Each unit's new content is
//    built complete in memory no reader can reach, then published by one
//    pointer-width store to that unit's screen slot. Content once published
//    is never written again. Superseded content is kept, unmodified, where
//    it is — never copied — and released only once no read that began
//    before the swap can still be on it. There is no whole-map copy,
//    snapshot, or freeze anywhere, and no edit ever waits, checks for
//    permission, or holds.
//
// 12. A READ OF THE MAP IS NEVER GRANTED, COUNTED, OR SEEN HERE.
//    A reader reads a unit's screen slot, then the content at that address,
//    on every access. It never calls, asks, or tells this file anything to
//    do so. A read of a pool's bytes is the opposite: this file grants
//    every one. Arrive and Leave are the only way into a pool, and that read
//    is exactly the span between them.
//
// 13. DESTROY ALONE ENDS A POOL, AND ONLY ONCE ITS LAST READER HAS LEFT.
//    Destroy flags the pool for destruction and refuses every new arrival
//    on it. Readers already in it carry on unrestricted. When the last one
//    leaves, the pool leaves the map and its chunks are released. Nothing
//    else pre-empts or brings forward a destruction.
//
// 14. THE SCREEN IS THE ONLY WAY TO THE MAP, AND THIS FILE ITS ONLY WRITER.
//    A screen slot holds one address and nothing else: no count, no flag,
//    no version. This file writes a slot once per edit per unit, and that
//    store is the whole of what it ever tells the screen. Ownership runs
//    strictly downward: map, unit content, chunk list, bytes. The map never
//    holds pool bytes, and nothing here ever copies them.
// ===========================================================================
//
// WHAT A POOL IS
//   One unit on the pool map. Its identity, its Class ID, its immunity, its
//   destruction flag and its chunks are fields on that unit's content — not
//   a table beside it, not a lookup anywhere. Anything needing a fact about
//   a pool reads the map through the screen. This file is the only thing
//   that CREATES, GROWS, RECLASSIFIES, FLAGS, or DESTROYS a pool. Content is
//   written directly by whatever is generating; it is not this file's.
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
// THE MAP AND THE SCREEN
//   The map is held here. The screen, on LiveRegistry, is one slot per unit,
//   each holding the address of that unit's current content; a null slot is
//   a unit with no pool.
//
//   READ, every access, per unit: load the slot; read the content at that
//   address. Nothing else. A reader never holds a slot across accesses and
//   has no arrival, no departure, no session.
//
//   EDIT, per unit touched, one edit at a time:
//     BUILD. The complete new content is written into fresh memory. The
//     live content and the slot are untouched.
//     SWAP. One store puts the new address in the slot. A reader loads
//     either the old address or the new, whole, and reads complete content
//     either way.
//     RETIRE. The superseded content stays where it is, unmodified, and is
//     released once no read that began before the swap can still be on it.
//
//   THE KEY. Before its first visit of the session, a reader asks this
//   file for the map key and keeps it. It reads unit content only through
//   the key.
//
//   A pool's bytes are gated: a reader arrives on the pool's unit, is a
//   current reader until it leaves, and a pool flagged for destruction
//   admits no one. A reader that ends without leaving is left for by the
//   system, the same leave.
//
// WELLNESS
//   Bare booleans, named for what they answer, set at the instant they are
//   answered, never read again here. Wellness sees them because they exist.
//
// OS_OWES
//   Each is declared where it is called.
//
//   OS BUILD OUTLINE — TO BE REMOVED ONCE THE OS IS BUILT.
//   1. THE MAP. Memory for the map's units, reserved to this file at all
//      times.
//   2. THE SCREEN. One slot per unit on LiveRegistry, pointer-width and
//      naturally aligned, so a load or store of it is indivisible. Writable
//      by this file alone, readable by every reader.
//   3. UNIT CONTENT. Fresh memory for each new content, taken at build.
//   4. RETIRE. Superseded content handed over at the swap is released once
//      no read that began before that swap can still be on it. Knowing when
//      is the OS's.
//   5. CHUNKS. One uniform size, taken and returned.

#pragma once

#include "live_registry.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
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

// ---------------------------------------------------------------------------
// One unit's content: one pool. Never written once published.
// ---------------------------------------------------------------------------
struct Pool {
    std::string                pool_id;
    std::uint64_t              class_id     = 0;
    std::string                turn_id;
    std::set<std::string>      prompt_ids;
    std::uint64_t              timestamp_ns = 0;
    std::vector<std::uint8_t*> chunks;
    std::uint64_t              byte_capacity = 0;
    std::vector<std::string>   immune_from;
    bool                       flagged_for_destruction = false;
};

// One screen slot: the address of a unit's current content. Null: no pool.
using ScreenSlot = std::atomic<const Pool*>;

// A granted read of one pool's bytes. The reader's own; not passed on.
struct PoolRead {
    std::uint64_t unit    = 0;
    bool          granted = false;
};

// THE MAP KEY — how to read unit content. Handed out by Pool Maintenance; a
// map reader asks for it once, before its first visit of the session, and
// keeps it. Every position is a byte offset into one unit's content.
struct MapKey {
    std::uint64_t unit_count = 0;
    std::uint64_t pool_id = 0, class_id = 0, turn_id = 0, prompt_ids = 0, timestamp_ns = 0,
                  byte_capacity = 0, flagged_for_destruction = 0;
};

// One field, at the key's offset, as the type the key names it.
template <class T>
inline const T& map_field(const std::uint8_t* at, std::uint64_t offset) {
    return *reinterpret_cast<const T*>(at + offset);
}

// ---------------------------------------------------------------------------
// POOL MAINTENANCE
// ---------------------------------------------------------------------------
class PoolMaintenance {
public:
    PoolMaintenance() = default;

    PoolMaintenance(const PoolMaintenance&)            = delete;
    PoolMaintenance& operator=(const PoolMaintenance&) = delete;

    // ---- the map key --------------------------------------------------------
    // Asked for once per reader per session, before its first visit to the
    // screen.
    MapKey   map_key() const;

    // ---- arrive / leave — a pool's bytes ----------------------------------
    // Arrive on one unit's pool. Refused — not granted — when that pool is
    // flagged for destruction or gone. The last leave from a flagged pool
    // ends it.
    PoolRead arrive(std::uint64_t unit);
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
    // unit.
    void create(const ClassRef&    cls,
                const std::string& turn_id,
                const std::string& continues_from = std::string(),
                std::string*       pool_id_out    = nullptr);

    // ---- grow ---------------------------------------------------------------
    // One more chunk. False when the pool does not exist, is flagged for
    // destruction, or the OS has no chunk to give.
    bool grow(const std::string& pool_id);

    // ---- destroy / flag / unflag — one filter, one source ----------------
    // Each returns how many pools it acted on. Destroy flags for destruction
    // and closes the pool's gate; the pool leaves the map when its last
    // reader leaves, at once if it has none. A pool already flagged for
    // destruction is not acted on again, by any of the three.
    std::uint64_t destroy(const PoolFilter& filter, const std::string& source);
    std::uint64_t flag   (const PoolFilter& filter, const std::string& source);
    std::uint64_t unflag (const PoolFilter& filter, const std::string& source);

    // ---- reclassify --------------------------------------------------------
    enum class Reclassify { Done, NotFound };

    // The new class, as the caller has it. Changes Class ID on the entry and
    // nothing else. NotFound when the pool does not exist, is flagged for
    // destruction, or the class resolves to nothing.
    Reclassify reclassify(const std::string& pool_id, const ClassRef& cls);

private:
    // Where a pool's bytes are read: one per unit, held here, never on the
    // content.
    struct Gate {
        std::mutex    m;
        std::uint64_t holders = 0;
        bool          closed  = false;
    };

    // The map's units. Unit i's content is reached through screen slot i.
    struct PoolMap {
        Gate*         gates      = nullptr;
        std::uint64_t unit_count = 0;
    };

    // The number the caller gave, or the number LiveRegistry holds for the
    // name it gave. Zero is "no such class". Used by create and reclassify.
    static std::uint64_t resolve_class(const ClassRef& cls);

    // Every criterion supplied must match; exclusions win. Used by destroy,
    // flag and unflag — the one matcher.
    static bool matches(const Pool& p, const PoolFilter& f);

    // One chunk from the OS onto the end of the pool. Caller holds the lock.
    bool take_chunk_locked(Pool& p);

    // The one edit, per unit: swap the slot to `next` (built complete, or
    // null for no pool) and retire what it held. Caller holds the lock.
    void swap_locked(std::uint64_t unit, const Pool* next);

    // The unit holding this pool, not flagged for destruction; unit_count if
    // none. Used by create, grow, reclassify.
    std::uint64_t live_unit(const std::string& pool_id) const;

    // The pool leaves the map; its chunks go back. Caller holds the lock.
    // Used by destroy and leave.
    void end_pool_locked(std::uint64_t unit);

    PoolMap map_;

    // One edit at a time. Not a lock on reading — readers never wait.
    mutable std::mutex mutex_;
};

// The one instance.
PoolMaintenance& pool_maintenance();

} // namespace prime
