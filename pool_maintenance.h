// pool_maintenance.h — THE POOL'S MAINTAINER. One file: a pool comes into
// existence here and goes out of existence here. Nowhere else. It lives on
// the pool map, on LiveRegistry; this file holds nothing.
//
// ===========================================================================
// POOL MAINTENANCE RULES — FIXED POINTS. A change that would break one of
// these is wrong by definition and is raised with the user instead of made.
// This is not a description of how the file runs; it is the boundary any
// edit keeps to so the file does not drift for convenience.
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
//    on LiveRegistry; this file reaches its future layer through the OS,
//    edits it, and mounts it. It offers no lookup, no find, no enumeration
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
//    substituted for a missing one. The one case where something is left
//    standing is a mount that fails after a chunk was taken: the pool stays
//    where it is, with its chunk, and the flag says so. Nothing is ever
//    undone to tidy up.
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
// 11. EVERY EDIT IS ON THE FUTURE LAYER, AND EVERY ACTION MOUNTS.
//    Slow work — taking a chunk — first; then the edit, on the future layer
//    alone; then the mount, as the last step of the same action. The layer
//    being read is never edited. Destroy removes the entry, mounts, and only
//    then returns the chunks.
// ===========================================================================
//
// WHAT A POOL IS
//   One entry on the pool map (live_registry.h). Its identity, its Class
//   ID, its immunity, and its chunks are fields on that one entry — not a
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
// WELLNESS
//   Bare booleans, named for what they answer, set at the instant they are
//   answered, never read again here. Wellness sees them because they exist.
//
// OS_OWES
//   The marker word (live_registry.h). The OS supplies the future layer and
//   the mount, and the chunks; each is declared where it is called.

#pragma once

#include "live_registry.h"

#include <cstdint>
#include <mutex>
#include <optional>
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
// POOL MAINTENANCE
// ---------------------------------------------------------------------------
class PoolMaintenance {
public:
    PoolMaintenance() = default;

    PoolMaintenance(const PoolMaintenance&)            = delete;
    PoolMaintenance& operator=(const PoolMaintenance&) = delete;

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
    // continues_from names a pool that does not exist, or the first chunk
    // cannot be taken. A mount that fails after the chunk was taken leaves
    // the pool in the future layer, with its chunk, and posts that.
    void create(const ClassRef&    cls,
                const std::string& turn_id,
                const std::string& continues_from = std::string(),
                std::string*       pool_id_out    = nullptr);

    // ---- grow ---------------------------------------------------------------
    // One more chunk. False when the pool does not exist, the OS has no chunk
    // to give, or the mount did not happen — in which last case the chunk is
    // on the entry in the future layer, and Wellness is told.
    bool grow(const std::string& pool_id);

    // ---- destroy / flag / unflag — one filter, one source ----------------
    // Each returns how many pools it acted on — on the map. A mount that did
    // not happen is zero acted on, and Wellness is told.
    std::uint64_t destroy(const PoolFilter& filter, const std::string& source);
    std::uint64_t flag   (const PoolFilter& filter, const std::string& source);
    std::uint64_t unflag (const PoolFilter& filter, const std::string& source);

    // ---- reclassify --------------------------------------------------------
    enum class Reclassify { Done, NotFound, NotMounted };

    // The new class, as the caller has it. Changes Class ID on the entry and
    // nothing else. NotFound when the pool does not exist or the class
    // resolves to nothing. NotMounted when the edit was made and the mount
    // did not happen.
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

    // This file's callers, one at a time on the future layer. Not the map's
    // lock — the map has none; readers never wait.
    mutable std::mutex mutex_;
};

} // namespace prime
