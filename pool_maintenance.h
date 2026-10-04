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
//   A pool is bytes. Mint takes 1 MiB of the VRAM part. Every grow takes one
//   more chunk. Whatever is writing decides WHEN to
//   grow, by reading capacity off the entry and calling grow. This file
//   never watches for that.
//
// THE BARRIER
//   Every pool has a barrier, and the same barrier stands around every KV
//   section that represents it.
//
// CREATE AND ITS CALLER
//   The caller fires and moves on. Create hands nothing back to it — the
//   outcome goes to Wellness. The one exception is the Pool ID, and only when
//   the caller asked for it. Each create is one pool: a refusal touches that
//   pool alone, undoes nothing already standing, and stops nothing else a
//   caller is minting.
//
// BOOT
//   The claim of one continuous block of 80 GiB from the RAM Manager, 1 GiB
//   of it designated RAM and the rest VRAM, stated in the one request. The
//   RAM part holds the map and every preserved section. The VRAM part is
//   pool memory. Whether the block stands is posted to Wellness.
//
// RAM
//   Pool memory held is the VRAM part of the boot block and every stretch
//   received since. Free is what is held less what pools are using. On a
//   spawn or grow that free space cannot cover, the shortfall is asked for
//   and the spawn or grow goes ahead. A RAM part with no room for a
//   preserved section asks for that room, and the edit goes ahead. The RAM
//   Manager asks for RAM back when it cannot cover what it needs; Pool
//   Maintenance chooses which bytes go and never gives up bytes in use or its
//   RAM part. Every exchange is posted to Wellness by both sides, and the
//   exchange still happens. Continuity is required of the boot block alone.
//
// THE SCREEN
//   When LiveRegistry starts up, whenever that is, it tells Pool
//   Maintenance, and Pool Maintenance claims the screen there: the
//   reflection of the map, naming the map's real memory here. The map never
//   leaves Pool Maintenance and nothing is copied to LiveRegistry.
//
//   READ. A reader holds the screen and resolves each section through the
//   reflection, reading the bytes it lands on: the map's real bytes, or a
//   preserved section while that section is being edited. Holding the
//   screen is the arrival and letting go is the leave; nothing is asked or
//   told.
//
//   EDIT, per section (one pool's entry):
//     PRESERVE. The section's pre-edit contents are put into a separate
//     piece of the RAM part, untouched.
//     REDIRECT. For as long as the edit is in progress, the section resolves
//     to the preserved bytes, for every reader.
//     EDIT. The real section is edited in place, at full pace.
//     LIFT. The instant the edit is finished the redirect is removed; the
//     section resolves straight to the real bytes again.
//     FREE. The preserved section is freed once the last reader that landed
//     on it lets go.
//   No reader sees an edit in progress. No reader waits and no edit waits.
//
//   NO POOLS, NO MAP. The map is the pools. With no pool standing the screen
//   shows nothing, and that is correct: it is not a failure, not an empty
//   state to repair, and nothing is placed there to fill it.
//
//   THE KEY. Before its first visit of the session, a reader asks this
//   file for the map key and keeps it. It reads a section only through the
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
//   The marker word. The OS supplies the chunks; each is declared where it
//   is called.

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
    std::uint8_t*              section      = nullptr;   // its own 1 MiB of the VRAM part
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

// A section's pre-edit contents, preserved in the RAM part for the readers
// that land on it. `next_free`: the RAM part's free preserved sections.
struct Preserved {
    Pool          entry;
    bool          present   = false;
    std::uint64_t readers   = 0;
    bool          lifted    = false;
    Preserved*    next_free = nullptr;
};

struct MapUnit;

// One KV section representing a pool. Its barrier is that pool's barrier:
// entry is arrive and leave on `pool`.
struct KVSection {
    MapUnit*      pool   = nullptr;
    std::uint8_t* bytes  = nullptr;
    std::uint64_t length = 0;
};

// One section: one pool's entry, in place in the RAM part. `present` false:
// no pool. `redirect` non-null: the section resolves to the preserved bytes.
struct MapUnit {
    Pool                    record;
    bool                    present = false;
    std::atomic<Preserved*> redirect{nullptr};
    Gate                    gate;
};

// Held by Pool Maintenance alone, in its RAM part. Sections sit where they
// are, from the start of the RAM part.
struct PoolMap {
    MapUnit*                   units = nullptr;
    std::atomic<std::uint64_t> unit_count{0};
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

// THE MAP KEY — how to read a section. Handed out by Pool Maintenance; a map
// reader asks for it once, before its first visit of the session, and keeps
// it. Every position is a byte offset into a section.
struct MapKey {
    std::uint64_t pool_id = 0, class_id = 0, turn_id = 0, prompt_ids = 0, timestamp_ns = 0,
                  byte_capacity = 0, flagged_for_destruction = 0;
};

// One field, at the key's offset, as the type the key names it.
template <class T>
inline const T& map_field(const std::uint8_t* at, std::uint64_t offset) {
    return *reinterpret_cast<const T*>(at + offset);
}

// THE SCREEN, HELD. Holding it is the arrival; its end is the leave. Each
// section resolves through the reflection to the bytes it lands on; null:
// no pool there. Not copyable, not movable: the reader's own.
class ScreenRead {
public:
    explicit ScreenRead(const PoolMap* reflection);
    ~ScreenRead();
    ScreenRead(const ScreenRead&)            = delete;
    ScreenRead& operator=(const ScreenRead&) = delete;

    std::uint64_t       unit_count() const;
    const std::uint8_t* unit(std::uint64_t i);

private:
    const PoolMap*          map_;
    std::vector<Preserved*> landed_;
};

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
    // One continuous block of 80 GiB claimed from the RAM Manager, 1 GiB
    // designated RAM and the rest VRAM. Nothing is returned; whether the
    // block stands is posted to Wellness.
    void boot();

    // ---- the screen ----------------------------------------------------------
    // LiveRegistry, starting up, tells Pool Maintenance; the reflection of
    // the map is claimed there.
    void claim_screen(LiveRegistry& registry);

    // ---- RAM back to the RAM Manager -----------------------------------------
    // The RAM Manager asking for `bytes`. Pool Maintenance chooses which
    // bytes go, never bytes in use and never its RAM part, and hands them
    // over.
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
    friend class ScreenRead;

    // The number the caller gave, or the number LiveRegistry holds for the
    // name it gave. Zero is "no such class". Used by create and reclassify.
    static std::uint64_t resolve_class(const ClassRef& cls);

    // Every criterion supplied must match; exclusions win. Used by destroy,
    // flag and unflag — the one matcher.
    static bool matches(const Pool& p, const PoolFilter& f);

    // One chunk from the OS onto the end of the pool; free space that cannot
    // cover it is asked for. Caller holds the lock.
    bool take_chunk_locked(Pool& p);

    // The one edit, per section: preserved, redirected, edited in place,
    // lifted. A RAM part with no room asks the RAM Manager for it. Caller
    // holds the lock.
    bool write_unit_locked(MapUnit& u, Pool next, bool present);

    // A preserved section with no reader and no redirect goes back to the
    // RAM part. Caller holds the screen's lock.
    void free_preserved_locked(Preserved* p);

    // The unit holding this pool, live and not flagged for
    // destruction; null if none. Used by create, grow, reclassify.
    MapUnit* live_unit(const std::string& pool_id);

    // The pool leaves the map; its chunks go back. Its gate
    // is closed and empty. Caller holds the lock. Used by destroy and leave.
    void end_pool_locked(MapUnit& u);

    // The RAM part: the map from its start, preserved sections from its end.
    std::uint8_t* ram_        = nullptr;
    std::uint64_t ram_bytes_  = 0;
    std::uint8_t* preserved_low_ = nullptr;
    Preserved*    free_preserved_ = nullptr;

    // Pool memory held: the VRAM part first, every stretch received after it.
    // Wholly this file's.
    std::vector<Stretch> held_;
    std::uint64_t        held_bytes_ = 0, used_bytes_ = 0;
    // The boot block's VRAM part: taken up to `vram_next_`, held up to
    // `vram_end_`.
    std::uint8_t*        vram_next_ = nullptr, *vram_end_ = nullptr;

    // The map. Held here alone.
    PoolMap map_;

    // Landing on and leaving a preserved section, and its redirect.
    std::mutex screen_mutex_;

    // This file's callers, one at a time on the map. Taken before the
    // screen's lock, never after.
    mutable std::mutex mutex_;
};

// The one instance.
PoolMaintenance& pool_maintenance();

} // namespace prime
