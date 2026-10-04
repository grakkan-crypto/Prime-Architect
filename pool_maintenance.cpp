// pool_maintenance.cpp — the pool: minted, grown, reclassified, flagged and
// destroyed here, nowhere else. Every edit to a section is preserved,
// redirected, made in place and lifted. Every read of a pool's bytes is
// granted here.

#include "pool_maintenance.h"

#include "id_generation.h"

#include <algorithm>
#include <chrono>
#include <cstddef>    // offsetof
#include <new>

namespace prime {

// ---------------------------------------------------------------------------
// OS_OWES — declared here exactly as they are called; the OS defines them.
// ---------------------------------------------------------------------------

// Chunks: one unit of pool memory, at the one uniform size. Null when there
// is none to give.
std::uint64_t os_chunk_size();
std::uint8_t* os_chunk_take();
void          os_chunk_return(std::uint8_t* chunk);

// BUILD OUTLINE — TO BE REMOVED ONCE THE RAM MANAGER IS BUILT. The RAM
// Manager hands over one continuous block of `bytes`, its first `ram_bytes`
// designated RAM and the rest VRAM, or null when it cannot; hands over a
// stretch of `bytes`, designated RAM when `ram` and VRAM otherwise, not
// necessarily continuous with anything held, or null when it cannot, and
// posts the exchange to Wellness; and calls give_back when it cannot cover
// what it needs, posting that to Wellness.
std::uint8_t* ram_manager_claim_continuous(std::uint64_t bytes, std::uint64_t ram_bytes);
std::uint8_t* ram_manager_supply(std::uint64_t bytes, bool ram);

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
void PoolMaintenance::boot() {
    constexpr std::uint64_t kBootClaim = 80ull << 30, kRamPart = 1ull << 30;
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint8_t* const b = ram_manager_claim_continuous(kBootClaim, kRamPart);
    const bool wellness_check_pool_memory_claimed = b != nullptr;
    (void)wellness_check_pool_memory_claimed;
    if (b == nullptr) return;
    ram_           = b;
    ram_bytes_     = kRamPart;
    preserved_low_ = b + kRamPart;
    map_.units     = reinterpret_cast<MapUnit*>(b);
    held_.push_back({ b + kRamPart, kBootClaim - kRamPart });
    held_bytes_    = kBootClaim - kRamPart;
}

// ---------------------------------------------------------------------------
// The screen — claimed when LiveRegistry starts up
// ---------------------------------------------------------------------------
void PoolMaintenance::claim_screen(LiveRegistry& registry) {
    registry.screen = &map_;   // the reflection: the map's real memory, here
}

ScreenRead::ScreenRead(const PoolMap* reflection) : map_(reflection) {}

ScreenRead::~ScreenRead() {
    PoolMaintenance& pm = pool_maintenance();
    std::lock_guard<std::mutex> lock(pm.screen_mutex_);
    for (Preserved* p : landed_)
        if (--p->readers == 0 && p->lifted) pm.free_preserved_locked(p);
}

std::uint64_t ScreenRead::unit_count() const {
    return map_ == nullptr ? 0 : map_->unit_count.load(std::memory_order_acquire);
}

const std::uint8_t* ScreenRead::unit(std::uint64_t i) {
    const MapUnit& u = map_->units[i];
    {
        std::lock_guard<std::mutex> lock(pool_maintenance().screen_mutex_);
        if (Preserved* p = u.redirect.load(std::memory_order_acquire)) {
            if (std::find(landed_.begin(), landed_.end(), p) == landed_.end()) {
                ++p->readers;
                landed_.push_back(p);
            }
            return p->present ? reinterpret_cast<const std::uint8_t*>(&p->entry) : nullptr;
        }
    }
    return u.present ? reinterpret_cast<const std::uint8_t*>(&u.record) : nullptr;
}

// ---------------------------------------------------------------------------
// RAM — handed back
// ---------------------------------------------------------------------------

std::vector<Stretch> PoolMaintenance::give_back(std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool wellness_check_pool_memory_given = true;
    (void)wellness_check_pool_memory_given;
    std::uint64_t n = std::min(bytes, held_bytes_ > used_bytes_ ? held_bytes_ - used_bytes_ : 0);
    std::vector<Stretch> out;
    while (n != 0 && !held_.empty()) {
        Stretch& s = held_.back();
        const std::uint64_t k = std::min(n, s.bytes);
        s.bytes -= k;
        out.push_back({ s.at + s.bytes, k });
        held_bytes_ -= k;
        n           -= k;
        if (s.bytes == 0) held_.pop_back();
    }
    return out;
}

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
    end_pool_locked(*read.unit);
}

// ---------------------------------------------------------------------------
// The one edit, per section — preserved, redirected, edited in place, lifted
// ---------------------------------------------------------------------------
bool PoolMaintenance::write_unit_locked(MapUnit& u, Pool next, bool present) {
    Preserved* p = nullptr;
    {
        std::lock_guard<std::mutex> screen(screen_mutex_);
        if (free_preserved_ != nullptr) { p = free_preserved_; free_preserved_ = p->next_free; }
        else if (preserved_low_ - sizeof(Preserved) >=
                 reinterpret_cast<std::uint8_t*>(map_.units + map_.unit_count.load())) {
            preserved_low_ -= sizeof(Preserved);
            p = reinterpret_cast<Preserved*>(preserved_low_);
        }
    }
    // No room in the RAM part to preserve the section: the room is asked
    // for from the RAM Manager, and the edit goes ahead.
    if (p == nullptr) {
        const bool wellness_check_pool_ram_part_full = true;
        (void)wellness_check_pool_ram_part_full;
        p = reinterpret_cast<Preserved*>(ram_manager_supply(sizeof(Preserved), true));
        const bool wellness_check_pool_ram_supplied = p != nullptr;
        (void)wellness_check_pool_ram_supplied;
        if (p == nullptr) return false;
    }

    new (p) Preserved{ u.record, u.present, 0, false, nullptr };                 // preserve
    u.redirect.store(p, std::memory_order_release);                              // redirect
    u.record  = std::move(next);                                                 // edit, in place
    u.present = present;
    std::lock_guard<std::mutex> screen(screen_mutex_);
    u.redirect.store(nullptr, std::memory_order_release);                        // lift
    p->lifted = true;
    if (p->readers == 0) free_preserved_locked(p);
    return true;
}

void PoolMaintenance::free_preserved_locked(Preserved* p) {
    p->~Preserved();
    p->next_free    = nullptr;
    new (p) Preserved{};
    p->next_free    = free_preserved_;
    free_preserved_ = p;
}

MapUnit* PoolMaintenance::live_unit(const std::string& pool_id) {
    const std::uint64_t n = map_.unit_count.load();
    for (std::uint64_t i = 0; i < n; ++i) {
        MapUnit& u = map_.units[i];
        if (u.present && !u.record.flagged_for_destruction && u.record.pool_id == pool_id)
            return &u;
    }
    return nullptr;
}

void PoolMaintenance::end_pool_locked(MapUnit& u) {
    if (!u.present || !u.record.flagged_for_destruction) return;
    const std::vector<std::uint8_t*> chunks = u.record.chunks;
    if (!write_unit_locked(u, Pool{}, false)) return;           // the edit: the entry gone
    for (std::uint8_t* c : chunks) os_chunk_return(c);          // OS_OWES
    const std::uint64_t freed = chunks.size() * os_chunk_size();   // OS_OWES
    used_bytes_ = used_bytes_ > freed ? used_bytes_ - freed : 0;
}

// ---------------------------------------------------------------------------
// The map key — this file's layout, stated as positions
// ---------------------------------------------------------------------------
MapKey PoolMaintenance::map_key() const {
    MapKey k;
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

        // The id chain: prompt id(s) come off the one pool this continues,
        // read off the map. A continuation naming a pool that is not there,
        // or is flagged for destruction, is a refusal.
        bool chain_ok = true;
        if (!continues_from.empty()) {
            MapUnit* from = live_unit(continues_from);
            if (from == nullptr) chain_ok = false;
            else p.prompt_ids = from->record.prompt_ids;
        }

        // A free section: no pool on it. None: the map takes the next
        // section of the RAM part, if the preserved sections leave room.
        MapUnit* free_unit = nullptr;
        if (chain_ok) {
            const std::uint64_t n = map_.unit_count.load();
            for (std::uint64_t i = 0; i < n && free_unit == nullptr; ++i)
                if (!map_.units[i].present) free_unit = &map_.units[i];
            if (free_unit == nullptr && ram_ != nullptr) {
                std::lock_guard<std::mutex> screen(screen_mutex_);
                if (reinterpret_cast<std::uint8_t*>(map_.units + n + 1) <= preserved_low_) {
                    free_unit = new (map_.units + n) MapUnit();
                    map_.unit_count.store(n + 1, std::memory_order_release);
                }
            }
        }
        // No free section: posted at the instant it is known.
        const bool wellness_check_pool_map_full = chain_ok && free_unit == nullptr;
        (void)wellness_check_pool_map_full;

        // The first chunk, before the edit. If it cannot be taken there is
        // no pool — not an empty one standing in for the one asked for.
        if (free_unit != nullptr && take_chunk_locked(p)) {
            const std::string id = p.pool_id;
            {
                std::lock_guard<std::mutex> gate(free_unit->gate.m);
                free_unit->gate.closed = false;
            }
            if (write_unit_locked(*free_unit, std::move(p), true)) {   // the edit: the entry, whole
                if (pool_id_out != nullptr) *pool_id_out = id;
                wellness_check_pool_created = true;
            }
        }
    }

    (void)wellness_check_pool_created;
}

// ---------------------------------------------------------------------------
// Grow
// ---------------------------------------------------------------------------
bool PoolMaintenance::grow(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    MapUnit* u = live_unit(pool_id);
    if (u == nullptr) return false;
    Pool next = u->record;
    if (!take_chunk_locked(next)) return false;
    return write_unit_locked(*u, std::move(next), true);   // the edit: one chunk appended
}

bool PoolMaintenance::take_chunk_locked(Pool& p) {
    // Free space that cannot cover the chunk: the shortfall is asked for,
    // and the spawn or grow goes ahead.
    const std::uint64_t cs   = os_chunk_size();   // OS_OWES
    const std::uint64_t free = held_bytes_ > used_bytes_ ? held_bytes_ - used_bytes_ : 0;
    if (free < cs) {
        const bool wellness_check_pool_memory_low = true;
        (void)wellness_check_pool_memory_low;
        std::uint8_t* const s = ram_manager_supply(cs - free, false);
        const bool wellness_check_pool_memory_supplied = s != nullptr;
        (void)wellness_check_pool_memory_supplied;
        if (s != nullptr) { held_.push_back({ s, cs - free }); held_bytes_ += cs - free; }
    }

    std::uint8_t* c = os_chunk_take();   // OS_OWES

    // The OS had no chunk to give — the only thing that can fail this.
    // Posted at the instant it is known.
    const bool wellness_check_pool_out_of_memory = (c == nullptr);
    (void)wellness_check_pool_out_of_memory;

    if (c == nullptr) return false;
    p.chunks.push_back(c);
    p.byte_capacity = p.chunks.size() * cs;
    used_bytes_ += cs;
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
    std::uint64_t n = 0;
    const std::uint64_t count = map_.unit_count.load();
    for (std::uint64_t i = 0; i < count; ++i) {
        MapUnit& u = map_.units[i];
        if (!u.present || u.record.flagged_for_destruction) continue;
        const Pool& p = u.record;
        const bool immune =
            std::find(p.immune_from.begin(), p.immune_from.end(), source) != p.immune_from.end();
        if (!matches(p, filter) || immune) continue;

        Pool next = p;
        next.flagged_for_destruction = true;
        if (!write_unit_locked(u, std::move(next), true)) continue;   // the edit: flagged
        bool empty = false;
        {
            std::lock_guard<std::mutex> gate(u.gate.m);
            u.gate.closed = true;                                       // no new reader
            empty = u.gate.holders.empty();
        }
        if (empty) end_pool_locked(u);
        ++n;
    }
    return n;
}

std::uint64_t PoolMaintenance::flag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    const std::uint64_t count = map_.unit_count.load();
    for (std::uint64_t i = 0; i < count; ++i) {
        MapUnit& u = map_.units[i];
        if (!u.present || u.record.flagged_for_destruction) continue;
        if (!matches(u.record, filter)) continue;
        ++n;
        if (std::find(u.record.immune_from.begin(), u.record.immune_from.end(), source)
            != u.record.immune_from.end()) continue;
        Pool next = u.record;
        next.immune_from.push_back(source);
        write_unit_locked(u, std::move(next), true);                   // the edit
    }
    return n;
}

std::uint64_t PoolMaintenance::unflag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    const std::uint64_t count = map_.unit_count.load();
    for (std::uint64_t i = 0; i < count; ++i) {
        MapUnit& u = map_.units[i];
        if (!u.present || u.record.flagged_for_destruction) continue;
        if (!matches(u.record, filter)) continue;
        ++n;
        const auto pos = std::find(u.record.immune_from.begin(), u.record.immune_from.end(), source);
        if (pos == u.record.immune_from.end()) continue;
        Pool next = u.record;
        next.immune_from.erase(next.immune_from.begin() + (pos - u.record.immune_from.begin()));
        write_unit_locked(u, std::move(next), true);                   // the edit
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
    MapUnit* u = live_unit(pool_id);
    if (u == nullptr) return Reclassify::NotFound;
    Pool next = u->record;
    next.class_id = class_id;
    return write_unit_locked(*u, std::move(next), true) ? Reclassify::Done
                                                        : Reclassify::NotFound;
}

} // namespace prime
