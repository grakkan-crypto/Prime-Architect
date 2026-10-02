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

// BUILD OUTLINE — TO BE REMOVED ONCE THE RAM MANAGER IS BUILT. The RAM
// Manager hands over one continuous block of `bytes`, or null when it cannot;
// hands over a stretch of `bytes`, not necessarily continuous with anything
// held, or null when it cannot, and posts the exchange to Wellness; and calls
// give_back when, at its own current rate of growth, its free space would run
// out before an exchange could complete.
std::uint8_t* ram_manager_claim_continuous(std::uint64_t bytes);
std::uint8_t* ram_manager_supply(std::uint64_t bytes);

static std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
void PoolMaintenance::boot() {
    constexpr std::uint64_t kBootClaim = 80ull << 30;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t t0 = now_ns();
    std::uint8_t* const b  = ram_manager_claim_continuous(kBootClaim);
    exchange_ns_ = now_ns() - t0;
    const bool wellness_check_pool_memory_claimed = b != nullptr;
    (void)wellness_check_pool_memory_claimed;
    if (b != nullptr) { held_.push_back({ b, kBootClaim }); held_bytes_ = kBootClaim; }
    live_registry().screen = map_.records;   // the screen's bytes, claimed
}

// ---------------------------------------------------------------------------
// RAM — asked for, handed back
// ---------------------------------------------------------------------------
void PoolMaintenance::ask_locked(std::uint64_t bytes) {
    const bool wellness_check_pool_memory_low = true;
    (void)wellness_check_pool_memory_low;
    const std::uint64_t t0 = now_ns();
    std::uint8_t* const s  = ram_manager_supply(bytes);
    exchange_ns_ = now_ns() - t0;
    const bool wellness_check_pool_memory_supplied = s != nullptr;
    (void)wellness_check_pool_memory_supplied;
    if (s != nullptr) { held_.push_back({ s, bytes }); held_bytes_ += bytes; }
}

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
// The screen — always permitted, every reader known
// ---------------------------------------------------------------------------
ScreenRead PoolMaintenance::arrive_screen() {
    std::lock_guard<std::mutex> lock(screen_mutex_);
    if (showing_ != nullptr) { ++showing_->readers; return { showing_->records, showing_ }; }
    ++live_readers_;
    return { map_.records, nullptr };
}

void PoolMaintenance::leave_screen(const ScreenRead& read) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::lock_guard<std::mutex> screen(screen_mutex_);
    if (read.image != nullptr) {
        if (--read.image->readers == 0 && read.image != showing_) {
            images_.erase(std::find(images_.begin(), images_.end(), read.image));
            delete[] read.image->records;
            delete read.image;
        }
        return;
    }
    if (--live_readers_ == 0 && showing_ != nullptr) switch_in_locked(map_);
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
    end_pool_locked(map_, *read.unit);
}

// ---------------------------------------------------------------------------
// The one edit, per unit — built apart, then switched
// ---------------------------------------------------------------------------
void PoolMaintenance::write_unit_locked(PoolMap& map, MapUnit& u, Pool next, bool present) {
    const std::uint64_t i = static_cast<std::uint64_t>(&u - map.units);
    map.next[i]      = std::move(next);                         // built elsewhere
    u.staged         = true;
    u.staged_present = present;
    std::lock_guard<std::mutex> screen(screen_mutex_);
    if (showing_ == nullptr) {                                   // the image, for new arrivals
        ScreenImage* img = new ScreenImage{ new Pool[map.unit_count], 0 };
        std::copy(map.records, map.records + map.unit_count, img->records);
        images_.push_back(img);
        showing_ = img;
        live_registry().screen = img->records;
    }
    if (live_readers_ == 0) switch_in_locked(map);
}

void PoolMaintenance::switch_in_locked(PoolMap& map) {
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        if (!u.staged) continue;
        map.records[i] = std::move(map.next[i]);
        u.present      = u.staged_present;
        u.staged       = false;
    }
    showing_ = nullptr;
    live_registry().screen = map.records;
    for (auto it = images_.begin(); it != images_.end();) {
        if ((*it)->readers != 0) { ++it; continue; }
        delete[] (*it)->records;
        delete *it;
        it = images_.erase(it);
    }
}

const Pool* PoolMaintenance::view(const PoolMap& map, const MapUnit& u) {
    const std::uint64_t i = static_cast<std::uint64_t>(&u - map.units);
    if (u.staged) return u.staged_present ? &map.next[i] : nullptr;
    return u.present ? &map.records[i] : nullptr;
}

MapUnit* PoolMaintenance::live_unit(PoolMap& map, const std::string& pool_id) {
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        const Pool* p = view(map, map.units[i]);
        if (p != nullptr && !p->flagged_for_destruction && p->pool_id == pool_id)
            return &map.units[i];
    }
    return nullptr;
}

void PoolMaintenance::end_pool_locked(PoolMap& map, MapUnit& u) {
    const Pool* p = view(map, u);
    if (p == nullptr || !p->flagged_for_destruction) return;
    const std::vector<std::uint8_t*> chunks = p->chunks;
    write_unit_locked(map, u, Pool{}, false);                   // the edit: the entry gone
    for (std::uint8_t* c : chunks) os_chunk_return(c);          // OS_OWES
    const std::uint64_t freed = chunks.size() * os_chunk_size();   // OS_OWES
    used_bytes_ = used_bytes_ > freed ? used_bytes_ - freed : 0;
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
            else p.prompt_ids = view(map, *from)->prompt_ids;
        }

        // A free unit: no pool on it.
        MapUnit* free_unit = nullptr;
        if (chain_ok)
            for (std::uint64_t i = 0; i < map.unit_count && free_unit == nullptr; ++i)
                if (view(map, map.units[i]) == nullptr)
                    free_unit = &map.units[i];
        // No free unit: posted at the instant it is known.
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
    Pool next = *view(map, *u);
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
    const std::uint64_t cs = os_chunk_size();   // OS_OWES
    p.chunks.push_back(c);
    p.byte_capacity = p.chunks.size() * cs;
    used_bytes_ += cs;

    // The current rate of growth, over one exchange: what it would consume
    // before more RAM could arrive. Free space below that: ask.
    const std::uint64_t t  = now_ns();
    const std::uint64_t dt = t - last_take_ns_;
    last_take_ns_ = t;
    const double need = dt != 0 ? static_cast<double>(cs) * static_cast<double>(exchange_ns_) / static_cast<double>(dt) : 0.0;
    const std::uint64_t free = held_bytes_ > used_bytes_ ? held_bytes_ - used_bytes_ : 0;
    if (static_cast<double>(free) < need) ask_locked(static_cast<std::uint64_t>(need));
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
        const Pool* live = view(map, u);
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
        const Pool* live = view(map, u);
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
        const Pool* live = view(map, u);
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
    Pool next = *view(map, *u);
    next.class_id = class_id;
    write_unit_locked(map, *u, std::move(next), true);   // the edit
    return Reclassify::Done;
}

} // namespace prime
