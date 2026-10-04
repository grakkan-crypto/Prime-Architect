// pool_maintenance.h — POOL MAINTENANCE. AN OS LAYER OF THE ONE SYSTEM.
//
// FUNCTION
//   Owns the pool memory: the RAM it claims from the RAM Manager at boot,
//   held for pools and for every model's KV cache, the tokenised form of a
//   pool's contents. It is the only place a pool comes into existence and
//   goes out of existence. It lives on the pool map, which this file holds.
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
//   One unit on the pool map. Its identity, its Class ID, its immunity and
//   its destruction flag are fields on that one unit — not a table beside
//   it, not a record in this file, not a lookup anywhere. Anything needing a
//   fact about a pool reads the map. This file is the only thing that
//   CREATES, GROWS, RECLASSIFIES, FLAGS, or DESTROYS a pool. Content is
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
//   A pool is bytes. Mint takes 1 MiB of the VRAM part. Whatever is writing
//   decides WHEN to grow, by reading capacity off the entry and calling
//   grow. This file never watches for that.
//
// THE BARRIER
//   Every pool has a barrier, and the same barrier stands around every KV
//   section that represents it.
//
// CREATE AND ITS CALLER
//   The caller fires and moves on. Create hands nothing back to it. The one
//   exception is the Pool ID, and only when the caller asked for it. Each
//   create is one pool: a refusal touches that pool alone, undoes nothing
//   already standing, and stops nothing else a caller is minting.
//
// BOOT
//   The claim of one continuous block from the RAM Manager, part of it
//   designated RAM and the rest VRAM. The RAM Manager sets both amounts. The
//   RAM part holds the map. The VRAM part is pool memory.
//
// RAM
//   Pool memory held is the VRAM part of the boot block and every stretch
//   received since. Free is what is held less what pools are using. On a
//   spawn that free space cannot cover, the shortfall is asked for and the
//   spawn goes ahead. The RAM Manager asks for RAM back when it cannot cover
//   what it needs; Pool Maintenance chooses which bytes go and never gives up
//   bytes in use or its RAM part. Continuity is required of the boot block
//   alone.
//
// THE SCREEN
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

using ClassRef = std::variant<std::uint64_t, std::string>;

struct PoolFilter {
    std::optional<std::uint64_t> class_id;
    std::optional<std::string>   turn_id;
    std::optional<std::string>   prompt_id;
    std::optional<std::string>   pool_id;
    std::vector<std::string>     exclude;
};

struct Pool {
    std::string              pool_id;
    std::uint64_t            class_id     = 0;
    std::string              turn_id;
    std::set<std::string>    prompt_ids;
    std::uint64_t            timestamp_ns = 0;
    std::uint8_t*            section      = nullptr;
    std::uint64_t            byte_capacity = 0;
    std::vector<std::string> immune_from;
    bool                     flagged_for_destruction = false;
};

struct Gate {
    std::mutex                   m;
    std::multiset<std::uint64_t> holders;
    bool                         closed = false;
};

struct MapUnit;

struct KVSection {
    MapUnit*      pool   = nullptr;
    std::uint8_t* bytes  = nullptr;
    std::uint64_t length = 0;
};

struct MapUnit {
    Pool record;
    bool present = false;
    Gate gate;
};

struct PoolMap {
    MapUnit*                   units = nullptr;
    std::atomic<std::uint64_t> unit_count{0};
};

struct Stretch {
    std::uint8_t* at    = nullptr;
    std::uint64_t bytes = 0;
};

struct PoolRead {
    MapUnit*      unit    = nullptr;
    std::uint64_t stamp   = 0;
    bool          granted = false;
};

struct MapKey {
    std::uint64_t pool_id = 0, class_id = 0, turn_id = 0, prompt_ids = 0, timestamp_ns = 0,
                  byte_capacity = 0, flagged_for_destruction = 0;
};

template <class T>
inline const T& map_field(const std::uint8_t* at, std::uint64_t offset) {
    return *reinterpret_cast<const T*>(at + offset);
}

class PoolMaintenance {
public:
    PoolMaintenance() = default;

    PoolMaintenance(const PoolMaintenance&)            = delete;
    PoolMaintenance& operator=(const PoolMaintenance&) = delete;

    MapKey   map_key() const;

    void boot();

    std::vector<Stretch> give_back(std::uint64_t bytes);

    PoolRead arrive(MapUnit* unit);
    void     leave(const PoolRead& read, bool died = false);

    void create(const ClassRef&    cls,
                const std::string& turn_id,
                const std::string& continues_from = std::string(),
                std::string*       pool_id_out    = nullptr);

    bool grow(const std::string& pool_id);

    std::uint64_t destroy(const PoolFilter& filter, const std::string& source);
    std::uint64_t flag   (const PoolFilter& filter, const std::string& source);
    std::uint64_t unflag (const PoolFilter& filter, const std::string& source);

    enum class Reclassify { Done, NotFound };

    Reclassify reclassify(const std::string& pool_id, const ClassRef& cls);

private:
    static std::uint64_t resolve_class(const ClassRef& cls);

    static bool matches(const Pool& p, const PoolFilter& f);

    void write_unit_locked(MapUnit& u, Pool next, bool present);

    MapUnit* live_unit(const std::string& pool_id);

    void end_pool_locked(MapUnit& u);

    std::uint8_t* ram_       = nullptr;
    std::uint64_t ram_bytes_ = 0;

    std::vector<Stretch> held_;
    std::uint64_t        held_bytes_ = 0, used_bytes_ = 0;
    std::uint8_t*        vram_next_ = nullptr, *vram_end_ = nullptr;

    PoolMap map_;

    mutable std::mutex mutex_;
};

PoolMaintenance& pool_maintenance();

} // namespace prime
