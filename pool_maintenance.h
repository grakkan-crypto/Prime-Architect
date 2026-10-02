// pool_maintenance.h — POOL MAINTENANCE. AN OS LAYER OF THE ONE SYSTEM.
//
// FUNCTION
//   Owns the pool memory: the RAM it claims from the RAM Manager at boot, at
//   least 80 GB, held for pools and for every model's KV cache, the
//   tokenised form of a pool's contents. It is the only place a pool comes
//   into existence and goes out of existence. It lives on the pool map,
//   which this file holds.
//
// STANDING
//   An OS layer holds defined authorities within one system working in
//   unison. It is not independent of that system and it takes instruction
//   from it. RAM held here belongs wholly to this file; what happens to
//   those bytes is its concern alone.
//
// AUTHORITIES
//   - Pool-level access over every pool and every model's KV cache.
//   - Who holds the pool map screen on LiveRegistry.
//   - Which bytes go back when RAM is reclaimed.
//   - When a destruction it is instructed to carry out is safe, and carrying
//     it out then.
//
// NOT HELD
//   Whether a pool exists or is destroyed. That arrives as an instruction
//   from the rest of the system.
//
// THE RAM MANAGER
//   Co-dependent. The RAM Manager supplies the RAM; this file decides how it
//   is used and what is returned. Neither sits above the other.
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
// 5. THIS FILE HOLDS THE MAP, AND IT NEVER LEAVES.
//    The map is held here and nowhere else. No copy of it, or of any part of
//    it, is held anywhere. The map appears on the screen, on LiveRegistry:
//    this file's own memory, shown there, not a copy. This file alone
//    changes what the screen shows. This file states the map's layout. It
//    offers no lookup, no find, no enumeration of pools by any criterion, to
//    any caller.
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
// 11. AN EDIT IS MADE ELSEWHERE AND SWITCHED IN ONLY ONCE THE LIVE MAP HAS
//    NO READER.
//    The live map's bytes never change under a reader. From the moment an
//    edit begins, arrivals are shown a static image of the map as it stood;
//    the edit is built elsewhere and switched into the live map when its
//    last reader leaves. Nothing waits for that: the last leave is the
//    switch. The image is a working copy for its readers alone, never kept
//    in step, and released when its last reader leaves.
//
// 12. EVERY READ OF THE SCREEN PASSES THE BARRIER, AND IS ALWAYS PERMITTED.
//    The barrier is how this file knows who is reading which version, and
//    what it chooses to show each arrival. It never refuses a reader of the
//    screen. A read of a pool's bytes is granted at the pool's own barrier:
//    Arrive and Leave are the only way into a pool, and that read is
//    exactly the span between them. A read cannot be taken anywhere, passed
//    on, or kept beyond its reader.
//
// 13. DESTROY ALONE ENDS A POOL, AND ONLY ONCE ITS LAST READER HAS LEFT.
//    Destroy flags the pool for destruction and refuses every new arrival
//    on it. Readers already in it carry on unrestricted. When the last one
//    leaves, the pool leaves the map and its chunks are released. Nothing
//    else pre-empts or brings forward a destruction.
//
// 14. THE MAP IS FIXED-SIZE UNITS.
//    The map is the set of pools: a pool's unit holding it and the pool
//    existing are the same fact. No second store, no log, no derivation.
//    Ownership runs strictly downward: map, unit, chunk list, bytes. The map
//    never holds pool bytes, and nothing here ever copies them.
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
// BOOT
//   The claim of one continuous block of 80 GiB from the RAM Manager, and
//   the claim of the screen's bytes on LiveRegistry. The map is an element
//   of that block. Whether the block stands is posted to Wellness.
//
// RAM
//   The RAM held is the boot block and every stretch received since. Free
//   is what is held less what pools are using. Pool Maintenance asks the
//   RAM Manager for more when, at its current rate of growth, its free space
//   would run out before an exchange could complete; it asks for what that
//   rate consumes over one exchange. The RAM Manager asks for RAM back by
//   the same rule on its side; Pool Maintenance chooses which bytes go and
//   never gives up bytes in use. Every exchange is posted to Wellness by
//   both sides, and the exchange still happens. Continuity is required of
//   the boot block alone.
//
// THE MAP AND ITS READS
//   The map is held here, and appears on the screen, on LiveRegistry: the
//   live map as the same memory, not a copy. Every read of the map is on the screen. No
//   other path to the map exists. The map cannot be taken anywhere, only
//   read.
//
//   READ. Every arrival at the screen passes its barrier, is always
//   permitted, and is shown either the live map or a static image of it.
//   It is a reader of that version until it leaves.
//
//   NO POOLS, NO MAP. The map is the pools. With no pool standing the screen
//   shows nothing, and that is correct: it is not a failure, not an empty
//   state to repair, and nothing is placed there to fill it.
//
//   EDIT:
//     IMAGE. When an edit begins and no image is showing, a static image of
//     the map is taken and shown to every new arrival. Readers already on
//     the live map carry on reading it.
//     BUILD. The edit is made elsewhere. The live map is not touched.
//     SWITCH. When the live map's last reader leaves, or at once if it has
//     none, every edit built is switched into the live map and the screen
//     shows the live map again.
//     RELEASE. An image's readers keep it until they leave; it is released
//     when its last reader leaves.
//
//   THE KEY. Before its first visit of the session, a reader asks this
//   file for the map key and keeps it. It reads the screen only through the
//   key.
//
//   A pool's bytes are gated: a reader arrives on the pool, is a current
//   reader until it leaves, and a pool flagged for destruction admits no
//   one. A reader that ends without leaving is left for by the system, the
//   same leave, detected without the reader's cooperation.
//
// WELLNESS
//   Bare booleans, named for what they answer, set at the instant they are
//   answered, never read again here. Wellness sees them because they exist.
//
// OS_OWES
//   The marker word (live_registry.h). The OS supplies the chunks; each is
//   declared where it is called.

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

// ===========================================================================
// THE POOL MAP — LAYOUT
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
    bool                       flagged_for_destruction = false;
};

// Where a pool's bytes are read. Every unit has one. `holders` is every
// reader present.
struct Gate {
    std::mutex                   m;
    std::multiset<std::uint64_t> holders;
    bool                         closed = false;
};

// One unit: one pool position. Its record is `records[i]` on the live map;
// `present` false: no pool. `staged`: an edit built for it in `next[i]`,
// `staged_present` whether a pool stands once it is switched in.
struct MapUnit {
    Gate gate;
    bool present = false, staged = false, staged_present = false;
};

// Held by Pool Maintenance alone. `records` is the live map; `next` is
// where edits are built.
struct PoolMap {
    Pool*         records    = nullptr;
    Pool*         next       = nullptr;
    MapUnit*      units      = nullptr;
    std::uint64_t unit_count = 0;
};

// A static image of the map, for the readers shown it.
struct ScreenImage {
    Pool*         records = nullptr;
    std::uint64_t readers = 0;
};

// One read of the screen. `bytes` is what this reader was shown; `image`
// null: the live map.
struct ScreenRead {
    const void*  bytes = nullptr;
    ScreenImage* image = nullptr;
};

// A stretch of RAM, held or handed back.
struct Stretch {
    std::uint8_t* at    = nullptr;
    std::uint64_t bytes = 0;
};

// A granted read of a pool's bytes. Not copyable to anyone else's use; it is
// the reader's own. `granted` false: refused.
struct PoolRead {
    MapUnit*      unit    = nullptr;
    std::uint64_t stamp   = 0;
    bool          granted = false;
};

// THE MAP KEY — how to read the screen. Handed out by Pool
// Maintenance; a map reader asks for it once, before its first visit of the
// session, and keeps it. Every position is a byte offset. A reader reads the
// map only through the key, never through the layout above, so a change to
// the layout is a change to Pool Maintenance alone.
struct MapKey {
    // the screen
    std::uint64_t unit_count = 0, unit_size = 0;
    // one pool record
    std::uint64_t pool_id = 0, class_id = 0, turn_id = 0, prompt_ids = 0, timestamp_ns = 0,
                  byte_capacity = 0, flagged_for_destruction = 0;
};

// One field, at the key's offset, as the type the key names it.
template <class T>
inline const T& map_field(const std::uint8_t* at, std::uint64_t offset) {
    return *reinterpret_cast<const T*>(at + offset);
}

// One access to one unit: the pool record where it appears
// on the screen, found through the key.
inline const std::uint8_t* map_record(const MapKey& k, const void* screen, std::uint64_t unit) {
    return static_cast<const std::uint8_t*>(screen) + unit * k.unit_size;
}

// ---------------------------------------------------------------------------
// POOL MAINTENANCE
// ---------------------------------------------------------------------------
class PoolMaintenance {
public:
    PoolMaintenance() = default;

    PoolMaintenance(const PoolMaintenance&)            = delete;
    PoolMaintenance& operator=(const PoolMaintenance&) = delete;

    // ---- arrive / leave — a pool's bytes ---------------------------------
    // Arrive on one unit's pool. Refused — not granted — when that pool is
    // flagged for destruction or gone. Leave ends the read, and the last
    // leave from a flagged pool destroys it. `died`: the system leaving for
    // a reader that ended without leaving.
    // The map key, whole. Asked for once per reader per
    // session, before its first visit to the screen.
    MapKey   map_key() const;

    // ---- boot ----------------------------------------------------------------
    // One continuous block of 80 GiB claimed from the RAM Manager, and the
    // screen's bytes claimed on LiveRegistry. Nothing is returned; whether
    // the block stands is posted to Wellness.
    void boot();

    // ---- the screen ----------------------------------------------------------
    // Always permitted. The reader is shown the live map, or the static image
    // while an edit is pending, and reads only what it was shown until it
    // leaves.
    ScreenRead arrive_screen();
    void       leave_screen(const ScreenRead& read);

    // ---- RAM back to the RAM Manager -----------------------------------------
    // The RAM Manager asking for `bytes`. Pool Maintenance chooses which
    // bytes go, never bytes in use, and hands them over.
    std::vector<Stretch> give_back(std::uint64_t bytes);

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
    // Each returns how many pools it acted on. Destroy flags
    // for destruction and closes the pool's gate; the pool leaves the map
    // when its last reader leaves, at once if it has none. A pool already
    // flagged for destruction is not acted on again, by any of the three.
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
    // The number the caller gave, or the number LiveRegistry holds for the
    // name it gave. Zero is "no such class". Used by create and reclassify.
    static std::uint64_t resolve_class(const ClassRef& cls);

    // Every criterion supplied must match; exclusions win. Used by destroy,
    // flag and unflag — the one matcher.
    static bool matches(const Pool& p, const PoolFilter& f);

    // One chunk from the OS onto the end of the pool. Caller holds the lock.
    bool take_chunk_locked(Pool& p);

    // The one edit, per unit: built in `next`, an image shown to arrivals
    // if none is, switched in once the live map has no reader. Caller holds
    // the lock.
    void write_unit_locked(PoolMap& map, MapUnit& u, Pool next, bool present);

    // Every built edit into the live map; the screen back on it; images
    // with no reader released. Caller holds both locks.
    void switch_in_locked(PoolMap& map);

    // A unit's pool as this file has it: the built edit if there is one,
    // else the live record. Null: no pool.
    static const Pool* view(const PoolMap& map, const MapUnit& u);

    // The unit holding this pool, live and not flagged for
    // destruction; null if none. Used by create, grow, reclassify.
    static MapUnit* live_unit(PoolMap& map, const std::string& pool_id);

    // More RAM from the RAM Manager: `bytes`, the exchange timed. Caller
    // holds the lock.
    void ask_locked(std::uint64_t bytes);

    // The pool leaves the map; its chunks go back. Its gate
    // is closed and empty. Caller holds the lock. Used by destroy and leave.
    void end_pool_locked(PoolMap& map, MapUnit& u);

    // The RAM held: the boot block first, every stretch received after it.
    // Wholly this file's.
    std::vector<Stretch> held_;
    std::uint64_t        held_bytes_ = 0, used_bytes_ = 0;
    // The last exchange with the RAM Manager, and the last chunk taken.
    std::uint64_t        exchange_ns_ = 0, last_take_ns_ = 0;

    // The map. Held here alone.
    PoolMap map_;

    // The screen's barrier: readers on the live map, the image shown to new
    // arrivals (null: the live map), every image still read.
    std::uint64_t             live_readers_ = 0;
    ScreenImage*              showing_      = nullptr;
    std::vector<ScreenImage*> images_;
    std::mutex                screen_mutex_;

    // This file's callers, one at a time on the map. Taken before the
    // screen's barrier, never after.
    mutable std::mutex mutex_;
};

// The one instance.
PoolMaintenance& pool_maintenance();

} // namespace prime
