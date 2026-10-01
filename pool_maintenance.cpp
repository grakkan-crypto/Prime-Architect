// pool_maintenance.cpp — the pool: minted, grown, reclassified, flagged and
// destroyed here, nowhere else. Every action builds the new content of the
// units it touches and swaps each unit's screen slot to it. Every read of a
// pool's bytes is granted here.

#include "pool_maintenance.h"

#include "id_generation.h"

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace prime {

// ---------------------------------------------------------------------------
// OS_OWES — declared here exactly as they are called; the OS defines them.
// ---------------------------------------------------------------------------

// Chunks: one unit of pool memory, at the one uniform size. Null when there
// is none to give.
std::uint64_t os_chunk_size();
std::uint8_t* os_chunk_take();
void          os_chunk_return(std::uint8_t* chunk);

// Superseded unit content, handed over at the swap. Released once no read
// that began before the swap can still be on it.
void          os_retire(const Pool* superseded);

// ---------------------------------------------------------------------------
// The one edit, per unit — build, then swap
// ---------------------------------------------------------------------------
void PoolMaintenance::swap_locked(std::uint64_t unit, const Pool* next) {
    ScreenSlot& slot = live_registry().screen[unit];
    const Pool* old  = slot.load(std::memory_order_relaxed);
    slot.store(next, std::memory_order_release);                 // the swap
    if (old != nullptr) os_retire(old);                          // OS_OWES
}

std::uint64_t PoolMaintenance::live_unit(const std::string& pool_id) const {
    const ScreenSlot* screen = live_registry().screen;
    for (std::uint64_t i = 0; i < map_.unit_count; ++i) {
        const Pool* p = screen[i].load(std::memory_order_relaxed);
        if (p != nullptr && !p->flagged_for_destruction && p->pool_id == pool_id) return i;
    }
    return map_.unit_count;
}

void PoolMaintenance::end_pool_locked(std::uint64_t unit) {
    const Pool* p = live_registry().screen[unit].load(std::memory_order_relaxed);
    if (p == nullptr || !p->flagged_for_destruction) return;
    const std::vector<std::uint8_t*> chunks = p->chunks;
    swap_locked(unit, nullptr);                                  // the edit: the entry gone
    for (std::uint8_t* c : chunks) os_chunk_return(c);           // OS_OWES
}

// ---------------------------------------------------------------------------
// Arrive / leave — a pool's bytes
// ---------------------------------------------------------------------------
PoolRead PoolMaintenance::arrive(std::uint64_t unit) {
    Gate& g = map_.gates[unit];
    std::lock_guard<std::mutex> lock(g.m);
    // A flagged or gone pool: no entry. Posted at the instant it is refused.
    const bool wellness_check_pool_entry_refused = g.closed;
    (void)wellness_check_pool_entry_refused;
    if (g.closed) return {};
    ++g.holders;
    return { unit, true };
}

void PoolMaintenance::leave(const PoolRead& read) {
    if (!read.granted) return;
    Gate& g = map_.gates[read.unit];
    bool last = false;
    {
        std::lock_guard<std::mutex> lock(g.m);
        --g.holders;
        last = g.closed && g.holders == 0;
    }
    if (!last) return;
    // The last reader out of a flagged pool: it goes now.
    std::lock_guard<std::mutex> lock(mutex_);
    end_pool_locked(read.unit);
}

// ---------------------------------------------------------------------------
// The map key — unit content, stated as positions
// ---------------------------------------------------------------------------
MapKey PoolMaintenance::map_key() const {
    MapKey k;
    k.unit_count              = map_.unit_count;
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
        Pool* p = new Pool;
        p->pool_id      = IdGeneration::instance().mint_pool_id();
        p->class_id     = class_id;
        p->turn_id      = turn_id;
        p->timestamp_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        std::lock_guard<std::mutex> lock(mutex_);
        const ScreenSlot* screen = live_registry().screen;

        // The id chain: prompt id(s) come off the one pool this continues.
        // A continuation naming a pool that is not there, or is flagged for
        // destruction, is a refusal.
        bool chain_ok = true;
        if (!continues_from.empty()) {
            const std::uint64_t from = live_unit(continues_from);
            if (from == map_.unit_count) chain_ok = false;
            else p->prompt_ids = screen[from].load(std::memory_order_relaxed)->prompt_ids;
        }

        // A free unit: no pool on it.
        std::uint64_t free_unit = map_.unit_count;
        if (chain_ok)
            for (std::uint64_t i = 0; i < map_.unit_count && free_unit == map_.unit_count; ++i)
                if (screen[i].load(std::memory_order_relaxed) == nullptr) free_unit = i;
        // No free unit: posted at the instant it is known.
        const bool wellness_check_pool_map_full = chain_ok && free_unit == map_.unit_count;
        (void)wellness_check_pool_map_full;

        // The first chunk, before the edit. If it cannot be taken there is
        // no pool — not an empty one standing in for the one asked for.
        if (free_unit != map_.unit_count && take_chunk_locked(*p)) {
            if (pool_id_out != nullptr) *pool_id_out = p->pool_id;
            {
                std::lock_guard<std::mutex> gate(map_.gates[free_unit].m);
                map_.gates[free_unit].closed = false;
            }
            swap_locked(free_unit, p);                           // the edit: the entry, whole
            wellness_check_pool_created = true;
        } else {
            delete p;
        }
    }

    (void)wellness_check_pool_created;
}

// ---------------------------------------------------------------------------
// Grow
// ---------------------------------------------------------------------------
bool PoolMaintenance::grow(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t u = live_unit(pool_id);
    if (u == map_.unit_count) return false;
    Pool* next = new Pool(*live_registry().screen[u].load(std::memory_order_relaxed));
    if (!take_chunk_locked(*next)) { delete next; return false; }
    swap_locked(u, next);                                        // the edit: one chunk appended
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
// Destroy flags for destruction and closes the gate; the pool ends at its
// last leave, or now if it has no reader.
// ---------------------------------------------------------------------------
std::uint64_t PoolMaintenance::destroy(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ScreenSlot* screen = live_registry().screen;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map_.unit_count; ++i) {
        const Pool* p = screen[i].load(std::memory_order_relaxed);
        if (p == nullptr || p->flagged_for_destruction) continue;
        const bool immune =
            std::find(p->immune_from.begin(), p->immune_from.end(), source) != p->immune_from.end();
        if (!matches(*p, filter) || immune) continue;

        Pool* next = new Pool(*p);
        next->flagged_for_destruction = true;
        swap_locked(i, next);                                    // the edit: flagged
        bool empty = false;
        {
            std::lock_guard<std::mutex> gate(map_.gates[i].m);
            map_.gates[i].closed = true;                         // no new reader
            empty = map_.gates[i].holders == 0;
        }
        if (empty) end_pool_locked(i);
        ++n;
    }
    return n;
}

std::uint64_t PoolMaintenance::flag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ScreenSlot* screen = live_registry().screen;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map_.unit_count; ++i) {
        const Pool* p = screen[i].load(std::memory_order_relaxed);
        if (p == nullptr || p->flagged_for_destruction) continue;
        if (!matches(*p, filter)) continue;
        ++n;
        if (std::find(p->immune_from.begin(), p->immune_from.end(), source)
            != p->immune_from.end()) continue;
        Pool* next = new Pool(*p);
        next->immune_from.push_back(source);
        swap_locked(i, next);                                    // the edit
    }
    return n;
}

std::uint64_t PoolMaintenance::unflag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ScreenSlot* screen = live_registry().screen;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map_.unit_count; ++i) {
        const Pool* p = screen[i].load(std::memory_order_relaxed);
        if (p == nullptr || p->flagged_for_destruction) continue;
        if (!matches(*p, filter)) continue;
        ++n;
        const auto pos = std::find(p->immune_from.begin(), p->immune_from.end(), source);
        if (pos == p->immune_from.end()) continue;
        Pool* next = new Pool(*p);
        next->immune_from.erase(next->immune_from.begin() + (pos - p->immune_from.begin()));
        swap_locked(i, next);                                    // the edit
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
    const std::uint64_t u = live_unit(pool_id);
    if (u == map_.unit_count) return Reclassify::NotFound;
    Pool* next = new Pool(*live_registry().screen[u].load(std::memory_order_relaxed));
    next->class_id = class_id;
    swap_locked(u, next);                                        // the edit
    return Reclassify::Done;
}

} // namespace prime
