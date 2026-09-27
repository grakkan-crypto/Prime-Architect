// pool_maintenance.h — THE POOL. One file: a pool comes into existence here,
// lives here, and goes out of existence here. Nowhere else.
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
//    one, and never writes anything to LiveRegistry. A caller supplies the
//    number or the name; a name is resolved by a direct read at that moment,
//    every time.
//
// 4. THE STAMP IS FIXED AT MINT.
//    Pool ID, Turn ID, Prompt ID(s), timestamp never change after creation.
//    Class ID is the only field Reclassify may change, and Reclassify
//    changes nothing else — no cascading writes, no side effects, no reach
//    into any other file's state.
//
// 5. THIS FILE IS NOT A DIRECTORY.
//    It offers no lookup, no find, no enumeration of pools by any criterion,
//    to any caller. Its internal record of which blocks belong to which pool
//    exists solely so this file can manage that memory — it is never exposed
//    as a way for another file to locate a pool.
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
// 8. NO REFUSAL IS SILENT.
//    A create, grow, or reclassify that cannot complete returns nothing
//    usable and leaves nothing standing in place of what was asked for. A
//    wellness flag reports the true outcome; no stand-in value is ever
//    substituted for a missing one.
//
// 9. THIS FILE IS MECHANICAL.
//    It never decides whether a pool should be destroyed, flagged, or kept.
//    It executes exactly what a caller, holding that authority, tells it to
//    do.
//
// 10. BLOCK SIZE IS THE ALLOCATOR'S FACT, NOT THIS FILE'S FIGURE.
//    One block is one allocation unit as MemoryAllocator reports it. No byte
//    count is ever invented or hardcoded here in its place.
// ===========================================================================
//
// WHAT A POOL IS
//   One object. Its identity, its classification, its immunity, and its bytes
//   are fields on that one object — not a table beside it, not a record in
//   another file, not a lookup anywhere. Anything needing a fact about a pool
//   reads the pool. This file is the only thing that CREATES, RESIZES,
//   RECLASSIFIES, FLAGS, or DESTROYS a pool. Content and the tail marker are
//   written directly by whatever is generating; they are not this file's.
//
// THE STAMP
//   Pool ID (minted by IdGeneration), Class ID, Turn ID, Prompt ID(s),
//   timestamp: set at the instant of creation. Turn ID may be empty (minted
//   before any turn exists). Prompt ID(s) may be empty (start of a chain).
//   Empty is a fact, not a branch. Prompt ID(s) are copied off the ONE pool
//   this one continues — read directly off that pool.
//
// SIZING
//   A pool is bytes, held as blocks. Mint takes one block. Every grow takes
//   one more. Whatever is writing decides WHEN to grow, by reading
//   byte_capacity off the pool and calling grow. This file never watches for
//   that.
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

#pragma once

#include "memory_allocator.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace prime {

// ---------------------------------------------------------------------------
// THE POOL OBJECT — every fact about a pool, on the pool.
// ---------------------------------------------------------------------------
struct Pool {
    // ---- identity: set at mint, fixed thereafter (class_id: reclassify only)
    std::string              pool_id;
    std::uint64_t            class_id     = 0;
    std::string              turn_id;            // empty: minted before any turn
    std::vector<std::string> prompt_ids;         // empty: start of a chain
    std::uint64_t            timestamp_ns = 0;   // moment of mint

    // ---- do-not-destroy: the sources this pool is immune to. Empty by default.
    std::vector<std::string> immune_from;

    // ---- bytes: the pool IS this list of blocks. No ceiling, nothing reserved.
    std::vector<uint8_t*>    blocks;
    std::uint64_t            block_size    = 0;   // one allocation unit, as reported
    std::uint64_t            byte_capacity = 0;   // blocks * block_size, kept in step by grow/shrink

    // ---- progress: byte offset of the last committed content. Written by
    //      whatever is writing, read by whatever is reading. Not this file's.
    std::atomic<std::uint64_t> tail{0};
};

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
    explicit PoolMaintenance(MemoryAllocator& mem) : mem_(mem) {}
    ~PoolMaintenance();

    PoolMaintenance(const PoolMaintenance&)            = delete;
    PoolMaintenance& operator=(const PoolMaintenance&) = delete;

    // ---- create: the moment of need --------------------------------------
    // Mint a pool NOW, with its first block. One call for every pool.
    //
    //   cls            — the Class ID number, or the declared name; a name is
    //                    read off LiveRegistry here, at this moment.
    //   turn_id        — the turn this pool belongs to; empty before any turn.
    //   continues_from — the ONE pool this continues; its prompt id(s) are
    //                    copied off that pool. Empty at the start of a chain.
    //   pool_id_out    — the caller's request for the new Pool ID back. Most
    //                    callers have no use for it and pass nothing; one that
    //                    keeps its own record (ProjectIngest) passes where it
    //                    wants it written. Written only when the pool stands.
    //
    // Nothing is returned. Whether the pool stands is posted to Wellness.
    // Refused — no pool left standing — when the class resolves to nothing,
    // continues_from names a pool that does not exist, or the first block
    // cannot be taken.
    void create(const ClassRef&    cls,
                const std::string& turn_id,
                const std::string& continues_from = std::string(),
                std::string*       pool_id_out    = nullptr);

    // ---- grow and shrink --------------------------------------------------
    // One more block. False when the pool does not exist or the machine has
    // no block to give.
    bool grow(const std::string& pool_id);

    // Hand back every block beyond keep_bytes. Blocks holding content a
    // reader can currently see are never touched; the tail is stopped at
    // keep_bytes if it pointed past it.
    void shrink(const std::string& pool_id, std::uint64_t keep_bytes);

    // ---- destroy / flag / unflag — one filter, one source ----------------
    // Each returns how many pools it acted on.
    std::uint64_t destroy(const PoolFilter& filter, const std::string& source);
    std::uint64_t flag   (const PoolFilter& filter, const std::string& source);
    std::uint64_t unflag (const PoolFilter& filter, const std::string& source);

    // ---- reclassify --------------------------------------------------------
    enum class Reclassify { Done, NotFound };

    // The new class, as the caller has it. Changes class_id on the pool and
    // nothing else. NotFound when the pool does not exist or the class
    // resolves to nothing.
    Reclassify reclassify(const std::string& pool_id, const ClassRef& cls);

private:
    // The number the caller gave, or the number LiveRegistry holds for the
    // name it gave. Zero is "no such class". Used by create and reclassify.
    static std::uint64_t resolve_class(const ClassRef& cls);

    // Every criterion supplied must match; exclusions win. Used by destroy,
    // flag and unflag — the one matcher.
    static bool matches(const Pool& p, const PoolFilter& f);

    // One block onto the end of the pool. Caller holds the lock.
    bool take_block_locked(Pool& p);

    // Every block back to the allocator. Caller holds the lock.
    void release_blocks_locked(Pool& p);

    MemoryAllocator& mem_;

    // The pools this file is managing memory for: which blocks belong to
    // which pool. Its own bookkeeping, for its own operations. Not a lookup.
    std::unordered_map<std::string, std::unique_ptr<Pool>> pools_;
    mutable std::mutex mutex_;
};

} // namespace prime
