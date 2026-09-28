// pool_maintenance.cpp — the pool: minted, grown, reclassified, flagged and
// destroyed here, nowhere else. Every action writes new versions of the map
// units it touches, copy-on-write, and every read is granted here. This file
// keeps nothing between calls. [[COW-EDIT 18]]

#include "pool_maintenance.h"

#include "id_generation.h"

#include <algorithm>
#include <chrono>
#include <limits>     // [[COW-EDIT 19]]

namespace prime {

// ---------------------------------------------------------------------------
// OS_OWES — declared here exactly as they are called; the OS defines them.
// ---------------------------------------------------------------------------

// [[COW-EDIT 20]] The OS calls for the map are removed; Pool Maintenance
// edits it directly.

// Chunks: one unit of pool memory, at the one uniform size. Null when there
// is none to give.
std::uint64_t os_chunk_size();
std::uint8_t* os_chunk_take();
void          os_chunk_return(std::uint8_t* chunk);

// ---------------------------------------------------------------------------
// [[COW-EDIT 21]] Arrive / leave — every read of the map (on the screen) and
// of a pool's bytes
// ---------------------------------------------------------------------------
PoolRead PoolMaintenance::arrive(MapUnit* unit) {
    Gate& g = unit != nullptr ? unit->gate : live_registry().screen->gate;
    std::lock_guard<std::mutex> lock(g.m);
    // A flagged or gone pool: no entry. Posted at the instant it is refused.
    const bool wellness_check_pool_entry_refused = g.closed;
    (void)wellness_check_pool_entry_refused;
    if (g.closed) return {};
    const std::uint64_t stamp = unit != nullptr ? 0 : g.clock;
    g.holders.insert(stamp);
    return { unit, stamp, true };
}

void PoolMaintenance::leave(const PoolRead& read, bool died) {
    if (!read.granted) return;
    PoolMap& map = *live_registry().screen;
    if (read.unit == nullptr) {
        // [[COW-EDIT 44]] Every stash this read is pinned to loses it; at
        // zero, its bytes are released now.
        std::lock_guard<std::mutex> lock(map.gate.m);
        map.gate.holders.erase(map.gate.holders.find(read.stamp));
        bool was_pinned = false;
        for (std::uint64_t i = 0; i < map.unit_count; ++i)
            for (Stash* st = map.units[i].stashes.load(std::memory_order_relaxed); st != nullptr;
                 st = st->older.load(std::memory_order_relaxed))
                if (st->from <= read.stamp && read.stamp < st->to) {
                    was_pinned = true;
                    if (--st->pinned == 0) st->content = Pool{};      // the bytes released
                    break;
                }
        // A reader died while pinned to one or more stashes. Posted at the
        // instant it is known.
        const bool wellness_check_pool_map_reader_died_pinned = died && was_pinned;
        (void)wellness_check_pool_map_reader_died_pinned;
        release_unseen(map);
        return;
    }
    bool last = false;
    {
        std::lock_guard<std::mutex> lock(read.unit->gate.m);
        read.unit->gate.holders.erase(read.unit->gate.holders.find(read.stamp));
        last = read.unit->gate.closed && read.unit->gate.holders.empty();
    }
    if (!last) return;
    // The last reader out of a flagged pool: it goes now.
    std::lock_guard<std::mutex> lock(mutex_);
    end_pool_locked(map, *read.unit);
}

// ---------------------------------------------------------------------------
// [[COW-EDIT 22]] The one edit, per unit — copy-on-write
// ---------------------------------------------------------------------------
void PoolMaintenance::write_unit_locked(PoolMap& map, MapUnit& u, Pool next, bool present) {
    std::lock_guard<std::mutex> lock(map.gate.m);
    const std::uint64_t from = u.live_from.load(std::memory_order_relaxed);
    const std::uint64_t to   = ++map.gate.clock;
    // Readers pinned to live: present, and arrived since this unit was last
    // forked.
    std::uint64_t pinned = 0;
    for (auto it = map.gate.holders.lower_bound(from); it != map.gate.holders.end(); ++it) ++pinned;
    // A fork occurred for this edit. Informational, not a failure.
    const bool wellness_check_pool_map_forked = pinned != 0;
    (void)wellness_check_pool_map_forked;
    if (pinned != 0) {
        Stash* st   = new Stash;
        st->content = u.live;                                        // the one copy, pre-edit
        st->present = u.present.load(std::memory_order_relaxed);
        st->from    = from;
        st->to      = to;
        st->pinned  = pinned;
        st->older.store(u.stashes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        u.stashes.store(st, std::memory_order_release);              // pinned before the write
    }
    u.live_from.store(to, std::memory_order_release);
    u.live = std::move(next);                                        // written live, in place
    u.present.store(present, std::memory_order_release);
}

void PoolMaintenance::release_unseen(PoolMap& map) {
    // An emptied stash is reached only by readers stamped before its `to`;
    // once none remain, it and everything older on its unit are dropped.
    const std::uint64_t oldest = map.gate.holders.empty()
        ? std::numeric_limits<std::uint64_t>::max() : *map.gate.holders.begin();
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        std::atomic<Stash*>* link = &map.units[i].stashes;
        Stash* st = link->load(std::memory_order_relaxed);
        while (st != nullptr && st->to > oldest) { link = &st->older; st = link->load(std::memory_order_relaxed); }
        link->store(nullptr, std::memory_order_release);
        while (st != nullptr) { Stash* o = st->older.load(std::memory_order_relaxed); delete st; st = o; }
    }
}

MapUnit* PoolMaintenance::live_unit(PoolMap& map, const std::string& pool_id) {
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        if (u.present.load(std::memory_order_relaxed) && !u.live.flagged_for_destruction &&
            u.live.pool_id == pool_id)
            return &u;
    }
    return nullptr;
}

void PoolMaintenance::end_pool_locked(PoolMap& map, MapUnit& u) {
    if (!u.present.load(std::memory_order_relaxed) || !u.live.flagged_for_destruction) return;
    const std::vector<std::uint8_t*> chunks = u.live.chunks;
    clear_pool_file_tag(u.live.pool_id);                        // [[COW-EDIT 60]] its tag goes with it
    write_unit_locked(map, u, Pool{}, false);                   // the edit: the entry gone
    for (std::uint8_t* c : chunks) os_chunk_return(c);          // OS_OWES
}

// ---------------------------------------------------------------------------
// [[COW-EDIT 59]] The pool-file table
// ---------------------------------------------------------------------------
std::map<std::string, std::string> PoolMaintenance::pool_file_table() const {
    std::lock_guard<std::mutex> lock(files_mutex_);
    return pool_files_;
}

void PoolMaintenance::set_pool_file_tag(const std::string& pool_id, const std::string& file) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    pool_files_[pool_id] = file;
}

void PoolMaintenance::clear_pool_file_tag(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    pool_files_.erase(pool_id);
}

void PoolMaintenance::clear_file_tags(const std::string& file) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    for (auto it = pool_files_.begin(); it != pool_files_.end();)
        it = it->second == file ? pool_files_.erase(it) : std::next(it);
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
        PoolMap& map = *live_registry().screen;   // [[COW-EDIT 24]] the map, through the screen

        // The id chain: prompt id(s) come off the one pool this continues,
        // read off the live map. A continuation naming a pool that is not
        // there, or is flagged for destruction, is a refusal.
        bool chain_ok = true;
        if (!continues_from.empty()) {
            MapUnit* from = live_unit(map, continues_from);
            if (from == nullptr) chain_ok = false;
            else p.prompt_ids = from->live.prompt_ids;
        }

        // [[COW-EDIT 25]] A free unit: empty, with no stash left on it — no
        // reader can still see a pool that stood there, so none can reach
        // its gate.
        MapUnit* free_unit = nullptr;
        if (chain_ok) {
            std::lock_guard<std::mutex> gate(map.gate.m);
            for (std::uint64_t i = 0; i < map.unit_count && free_unit == nullptr; ++i)
                if (!map.units[i].present.load(std::memory_order_relaxed) &&
                    map.units[i].stashes.load(std::memory_order_relaxed) == nullptr)
                    free_unit = &map.units[i];
        }
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
    PoolMap& map = *live_registry().screen;   // [[COW-EDIT 26]]
    MapUnit* u = live_unit(map, pool_id);
    if (u == nullptr) return false;
    Pool next = u->live;
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
// [[COW-EDIT 27]] All three rewritten: per-unit copy-on-write.
// Destroy flags for destruction and closes the gate; the pool ends at its
// last leave, or now if it has no reader.
// ---------------------------------------------------------------------------
std::uint64_t PoolMaintenance::destroy(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& map = *live_registry().screen;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        if (!u.present.load(std::memory_order_relaxed) || u.live.flagged_for_destruction) continue;
        const Pool& p = u.live;
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
    PoolMap& map = *live_registry().screen;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        if (!u.present.load(std::memory_order_relaxed) || u.live.flagged_for_destruction) continue;
        if (!matches(u.live, filter)) continue;
        ++n;
        if (std::find(u.live.immune_from.begin(), u.live.immune_from.end(), source)
            != u.live.immune_from.end()) continue;
        Pool next = u.live;
        next.immune_from.push_back(source);
        write_unit_locked(map, u, std::move(next), true);          // the edit
    }
    return n;
}

std::uint64_t PoolMaintenance::unflag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& map = *live_registry().screen;
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < map.unit_count; ++i) {
        MapUnit& u = map.units[i];
        if (!u.present.load(std::memory_order_relaxed) || u.live.flagged_for_destruction) continue;
        if (!matches(u.live, filter)) continue;
        ++n;
        const auto pos = std::find(u.live.immune_from.begin(), u.live.immune_from.end(), source);
        if (pos == u.live.immune_from.end()) continue;
        Pool next = u.live;
        next.immune_from.erase(next.immune_from.begin() + (pos - u.live.immune_from.begin()));
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
    PoolMap& map = *live_registry().screen;   // [[COW-EDIT 28]]
    MapUnit* u = live_unit(map, pool_id);
    if (u == nullptr) return Reclassify::NotFound;
    Pool next = u->live;
    next.class_id = class_id;
    write_unit_locked(map, *u, std::move(next), true);   // the edit
    return Reclassify::Done;
}

} // namespace prime
