// pool_maintenance.cpp — the pool: minted, grown, reclassified, flagged and
// destroyed here, nowhere else. Every action builds the new content of the
// map units it touches apart and switches the screen to show it. Every read
// of a pool's bytes is granted here.

#include "pool_maintenance.h"

#include "id_generation.h"

#include <algorithm>
#include <chrono>
#include <cstddef>    // offsetof

namespace prime {

// ---------------------------------------------------------------------------
// OS_OWES — declared here exactly as they are called; the OS defines them.
// ---------------------------------------------------------------------------

// Chunks: one unit of pool memory, at the one uniform size. Null when there
// is none to give.
std::uint64_t os_chunk_size();
std::uint8_t* os_chunk_take();
void          os_chunk_return(std::uint8_t* chunk);

// The screen: switched to show `content` as that unit, in one step. Null:
// nothing shown for it.
void          os_screen_show(std::uint64_t unit, const Pool* content);

// Retired unit content, handed over at the switch. Released once no read
// begun before the switch can still be on it.
void          os_retire(const Pool* retired);

// One pool's own memory, from the VRAM part.
constexpr std::uint64_t kPoolBytes = 1ull << 20;

// ---------------------------------------------------------------------------
// Arrive / leave — a pool's bytes
// ---------------------------------------------------------------------------
PoolRead PoolMaintenance::arrive(MapUnit* unit) {
    Gate& g = unit->gate;
    std::lock_guard<std::mutex> lock(g.m);
    // A flagged or gone pool: no entry. Posted at the instant it is refused.
    const bool wellness_check_pool_entry_refused = g.closed;
    (void)wellness_check_pool_entry_refused;
    if (g.closed) return {};
    g.holders.insert(0);
    return { unit, 0, true };
}

void PoolMaintenance::leave(const PoolRead& read, bool died) {
    (void)died;
    if (!read.granted) return;
    bool last = false;
    {
        std::lock_guard<std::mutex> lock(read.unit->gate.m);
        read.unit->gate.holders.erase(read.unit->gate.holders.find(read.stamp));
        last = read.unit->gate.closed && read.unit->gate.holders.empty();
    }
    if (!last) return;
    // The last reader out of a flagged pool: it goes now.
    std::lock_guard<std::mutex> lock(mutex_);
    end_pool_locked(map_, *read.unit);
}

// ---------------------------------------------------------------------------
// The one edit, per unit — built apart, then switched
// ---------------------------------------------------------------------------
void PoolMaintenance::write_unit_locked(PoolMap& map, MapUnit& u, Pool next, bool present) {
    const Pool* built = present ? new Pool(std::move(next)) : nullptr;   // built complete, apart
    const Pool* old   = u.live;
    os_screen_show(static_cast<std::uint64_t>(&u - map.units), built);  // OS_OWES — the switch
    u.live = built;
    if (old != nullptr) os_retire(old);                                 // OS_OWES
}

MapUnit* PoolMaintenance::live_unit(PoolMap& map, const std::string& pool_id) {
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        const Pool* p = map.units[i].live;
        if (p != nullptr && !p->flagged_for_destruction && p->pool_id == pool_id)
            return &map.units[i];
    }
    return nullptr;
}

void PoolMaintenance::end_pool_locked(PoolMap& map, MapUnit& u) {
    const Pool* p = u.live;
    if (p == nullptr || !p->flagged_for_destruction) return;
    const std::vector<std::uint8_t*> chunks = p->chunks;
    write_unit_locked(map, u, Pool{}, false);                   // the edit: the entry gone
    for (std::uint8_t* c : chunks) os_chunk_return(c);          // OS_OWES
}

// ---------------------------------------------------------------------------
// The map key — this file's layout, stated as positions
// ---------------------------------------------------------------------------
MapKey PoolMaintenance::map_key() const {
    MapKey k;
    k.unit_count              = map_.unit_count;
    k.unit_size               = sizeof(Pool);
    k.pool_id                 = offsetof(Pool, pool_id);
    k.class_id                = offsetof(Pool, class_id);
    k.turn_id                 = offsetof(Pool, turn_id);
    k.prompt_ids              = offsetof(Pool, prompt_ids);
    k.timestamp_ns            = offsetof(Pool, timestamp_ns);
    k.byte_capacity           = offsetof(Pool, byte_capacity);
    k.flagged_for_destruction = offsetof(Pool, flagged_for_destruction);
    return k;
}

// ---------------------------------------------------------------------------
// The class, as a number
// ---------------------------------------------------------------------------
std::uint64_t PoolMaintenance::resolve_class(const ClassRef& cls) {
    if (const auto* id = std::get_if<std::uint64_t>(&cls)) return *id;
    // A name: read straight off LiveRegistry, now. Zero means the loaded
    // pipeline declares no such pool.
    return live_registry().class_id_for(std::get<std::string>(cls));
}

// ---------------------------------------------------------------------------
// Create — the moment of need
// ---------------------------------------------------------------------------
void PoolMaintenance::create(const ClassRef&    cls,
                             const std::string& turn_id,
                             const std::string& continues_from,
                             std::string*       pool_id_out) {
    // Whether this pool actually came to stand on the map. Posted once, at
    // the end, whatever happened on the way. The caller is not told;
    // Wellness is.
    bool wellness_check_pool_created = false;

    const std::uint64_t class_id = resolve_class(cls);
    if (class_id != 0) { // zero: no such class — refused, not invented
        Pool p;
        p.pool_id      = IdGeneration::instance().mint_pool_id();
        p.class_id     = class_id;
        p.turn_id      = turn_id;
        p.timestamp_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        std::lock_guard<std::mutex> lock(mutex_);
        PoolMap& map = map_;

        // The id chain: prompt id(s) come off the one pool this continues,
        // read off the live map. A continuation naming a pool that is not
        // there, or is flagged for destruction, is a refusal.
        bool chain_ok = true;
        if (!continues_from.empty()) {
            MapUnit* from = live_unit(map, continues_from);
            if (from == nullptr) chain_ok = false;
            else p.prompt_ids = from->live->prompt_ids;
        }

        // A free unit: no pool on it.
        MapUnit* free_unit = nullptr;
        if (chain_ok)
            for (std::uint64_t i = 0; i < map.unit_count && free_unit == nullptr; ++i)
                if (map.units[i].live == nullptr)
                    free_unit = &map.units[i];
        // No free unit: posted at the instant it is known.
        const bool wellness_check_pool_map_full = chain_ok && free_unit == nullptr;
        (void)wellness_check_pool_map_full;

        // The pool's own 1 MiB of the VRAM part, before the edit.
        if (free_unit != nullptr && static_cast<std::uint64_t>(vram_end_ - vram_next_) >= kPoolBytes) {
            p.section       = vram_next_;
            p.byte_capacity = kPoolBytes;
            vram_next_     += kPoolBytes;
        }
        if (free_unit != nullptr && p.section != nullptr) {
            const std::string id = p.pool_id;
            {
                std::lock_guard<std::mutex> gate(free_unit->gate.m);
                free_unit->gate.closed = false;
            }
            write_unit_locked(map, *free_unit, std::move(p), true);   // the edit: the entry, whole
            if (pool_id_out != nullptr) *pool_id_out = id;
            wellness_check_pool_created = true;
        }
    }

    (void)wellness_check_pool_created;
}

// ---------------------------------------------------------------------------
// Grow
// ---------------------------------------------------------------------------
bool PoolMaintenance::grow(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& map = map_;
    MapUnit* u = live_unit(map, pool_id);
    if (u == nullptr) return false;
    Pool next = *u->live;
    if (!take_chunk_locked(next)) return false;
    write_unit_locked(map, *u, std::move(next), true);   // the edit: one chunk appended
    return true;
}

bool PoolMaintenance::take_chunk_locked(Pool& p) {
    std::uint8_t* c = os_chunk_take();   // OS_OWES

    // The OS had no chunk to give — the only thing that can fail this.
    // Posted at the instant it is known.
    const bool wellness_check_pool_out_of_memory = (c == nullptr);
    (void)wellness_check_pool_out_of_memory;

    if (c == nullptr) return false;
    p.chunks.push_back(c);
    p.byte_capacity = p.chunks.size() * os_chunk_size();   // OS_OWES
    return true;
}

// ---------------------------------------------------------------------------
// The one matcher
// ---------------------------------------------------------------------------
bool PoolMaintenance::matches(const Pool& p, const PoolFilter& f) {
    if (std::find(f.exclude.begin(), f.exclude.end(), p.pool_id) != f.exclude.end())
        return false;
    if (f.pool_id   && *f.pool_id  != p.pool_id)          return false;
    if (f.class_id  && *f.class_id != p.class_id)         return false;
    if (f.turn_id   && *f.turn_id  != p.turn_id)          return false;
    if (f.prompt_id && p.prompt_ids.count(*f.prompt_id) == 0) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Destroy / flag / unflag
// All three: per unit, built apart, then switched.
// Destroy flags for destruction and closes the gate; the pool ends at its
// last leave, or now if it has no reader.
// ---------------------------------------------------------------------------
std::uint64_t PoolMaintenance::destroy(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& map = map_;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        const Pool* live = u.live;
        if (live == nullptr || live->flagged_for_destruction) continue;
        const Pool& p = *live;
        const bool immune =
            std::find(p.immune_from.begin(), p.immune_from.end(), source) != p.immune_from.end();
        if (!matches(p, filter) || immune) continue;

        Pool next = p;
        next.flagged_for_destruction = true;
        write_unit_locked(map, u, std::move(next), true);          // the edit: flagged
        bool empty = false;
        {
            std::lock_guard<std::mutex> gate(u.gate.m);
            u.gate.closed = true;                                   // no new reader
            empty = u.gate.holders.empty();
        }
        if (empty) end_pool_locked(map, u);
        ++n;
    }
    return n;
}

std::uint64_t PoolMaintenance::flag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& map = map_;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        const Pool* live = u.live;
        if (live == nullptr || live->flagged_for_destruction) continue;
        if (!matches(*live, filter)) continue;
        ++n;
        if (std::find(live->immune_from.begin(), live->immune_from.end(), source)
            != live->immune_from.end()) continue;
        Pool next = *live;
        next.immune_from.push_back(source);
        write_unit_locked(map, u, std::move(next), true);          // the edit
    }
    return n;
}

std::uint64_t PoolMaintenance::unflag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& map = map_;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        const Pool* live = u.live;
        if (live == nullptr || live->flagged_for_destruction) continue;
        if (!matches(*live, filter)) continue;
        ++n;
        const auto pos = std::find(live->immune_from.begin(), live->immune_from.end(), source);
        if (pos == live->immune_from.end()) continue;
        Pool next = *live;
        next.immune_from.erase(next.immune_from.begin() + (pos - live->immune_from.begin()));
        write_unit_locked(map, u, std::move(next), true);          // the edit
    }
    return n;
}

// ---------------------------------------------------------------------------
// Reclassify — Class ID on the entry, nothing else
// ---------------------------------------------------------------------------
PoolMaintenance::Reclassify
PoolMaintenance::reclassify(const std::string& pool_id, const ClassRef& cls) {
    const std::uint64_t class_id = resolve_class(cls);
    if (class_id == 0) return Reclassify::NotFound;

    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& map = map_;
    MapUnit* u = live_unit(map, pool_id);
    if (u == nullptr) return Reclassify::NotFound;
    Pool next = *u->live;
    next.class_id = class_id;
    write_unit_locked(map, *u, std::move(next), true);   // the edit
    return Reclassify::Done;
}

} // namespace prime
