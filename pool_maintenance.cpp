// pool_maintenance.cpp — the pool: minted, grown, reclassified, flagged and
// destroyed here, nowhere else. Every read of a pool's bytes is granted here.

#include "pool_maintenance.h"

#include "id_generation.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <new>

namespace prime {

// BUILD OUTLINE — TO BE REMOVED ONCE THE RAM MANAGER IS BUILT. The RAM
// Manager hands over one continuous block, writing its size into `bytes` and
// the size of its RAM part into `ram_bytes`, its first `ram_bytes`
// designated RAM and the rest VRAM, or null when it cannot; hands over a
// stretch of `bytes`, designated RAM when `ram` and VRAM otherwise, not
// necessarily continuous with anything held, or null when it cannot; and
// calls give_back when it cannot cover what it needs.
std::uint8_t* ram_manager_claim_continuous(std::uint64_t& bytes, std::uint64_t& ram_bytes);
std::uint8_t* ram_manager_supply(std::uint64_t bytes, bool ram);

constexpr std::uint64_t kPoolBytes = 1ull << 20;

void PoolMaintenance::boot() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t bytes = 0, ram_bytes = 0;
    std::uint8_t* const b = ram_manager_claim_continuous(bytes, ram_bytes);
    if (b == nullptr) return;
    ram_       = b;
    ram_bytes_ = ram_bytes;
    map_.units = reinterpret_cast<MapUnit*>(b);
    held_.push_back({ b + ram_bytes, bytes - ram_bytes });
    held_bytes_ = bytes - ram_bytes;
    vram_next_  = b + ram_bytes;
    vram_end_   = b + bytes;
}

std::vector<Stretch> PoolMaintenance::give_back(std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
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

PoolRead PoolMaintenance::arrive(MapUnit* unit) {
    Gate& g = unit->gate;
    std::lock_guard<std::mutex> lock(g.m);
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
    std::lock_guard<std::mutex> lock(mutex_);
    end_pool_locked(*read.unit);
}

void PoolMaintenance::write_unit_locked(MapUnit& u, Pool next, bool present) {
    u.record  = std::move(next);
    u.present = present;
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
    write_unit_locked(u, Pool{}, false);
}

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

std::uint64_t PoolMaintenance::resolve_class(const ClassRef& cls) {
    if (const auto* id = std::get_if<std::uint64_t>(&cls)) return *id;
    return live_registry().class_id_for(std::get<std::string>(cls));
}

void PoolMaintenance::create(const ClassRef&    cls,
                             const std::string& turn_id,
                             const std::string& continues_from,
                             std::string*       pool_id_out) {
    const std::uint64_t class_id = resolve_class(cls);
    if (class_id != 0) {
        Pool p;
        p.pool_id      = IdGeneration::instance().mint_pool_id();
        p.class_id     = class_id;
        p.turn_id      = turn_id;
        p.timestamp_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        std::lock_guard<std::mutex> lock(mutex_);

        bool chain_ok = true;
        if (!continues_from.empty()) {
            MapUnit* from = live_unit(continues_from);
            if (from == nullptr) chain_ok = false;
            else p.prompt_ids = from->record.prompt_ids;
        }

        MapUnit* free_unit = nullptr;
        if (chain_ok) {
            const std::uint64_t n = map_.unit_count.load();
            for (std::uint64_t i = 0; i < n && free_unit == nullptr; ++i)
                if (!map_.units[i].present) free_unit = &map_.units[i];
            if (free_unit == nullptr && ram_ != nullptr &&
                reinterpret_cast<std::uint8_t*>(map_.units + n + 1) <= ram_ + ram_bytes_) {
                free_unit = new (map_.units + n) MapUnit();
                map_.unit_count.store(n + 1, std::memory_order_release);
            }
        }

        if (free_unit != nullptr && static_cast<std::uint64_t>(vram_end_ - vram_next_) >= kPoolBytes) {
            p.section       = vram_next_;
            p.byte_capacity = kPoolBytes;
            vram_next_     += kPoolBytes;
            used_bytes_    += kPoolBytes;
        }
        if (free_unit != nullptr && p.section != nullptr) {
            const std::string id = p.pool_id;
            {
                std::lock_guard<std::mutex> gate(free_unit->gate.m);
                free_unit->gate.closed = false;
            }
            write_unit_locked(*free_unit, std::move(p), true);
            if (pool_id_out != nullptr) *pool_id_out = id;
        }
    }
}

bool PoolMaintenance::grow(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    MapUnit* u = live_unit(pool_id);
    if (u == nullptr) return false;
    Pool next = u->record;
    write_unit_locked(*u, std::move(next), true);
    return true;
}

bool PoolMaintenance::matches(const Pool& p, const PoolFilter& f) {
    if (std::find(f.exclude.begin(), f.exclude.end(), p.pool_id) != f.exclude.end())
        return false;
    if (f.pool_id   && *f.pool_id  != p.pool_id)          return false;
    if (f.class_id  && *f.class_id != p.class_id)         return false;
    if (f.turn_id   && *f.turn_id  != p.turn_id)          return false;
    if (f.prompt_id && p.prompt_ids.count(*f.prompt_id) == 0) return false;
    return true;
}

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
        write_unit_locked(u, std::move(next), true);
        bool empty = false;
        {
            std::lock_guard<std::mutex> gate(u.gate.m);
            u.gate.closed = true;
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
        write_unit_locked(u, std::move(next), true);
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
        write_unit_locked(u, std::move(next), true);
    }
    return n;
}

PoolMaintenance::Reclassify
PoolMaintenance::reclassify(const std::string& pool_id, const ClassRef& cls) {
    const std::uint64_t class_id = resolve_class(cls);
    if (class_id == 0) return Reclassify::NotFound;

    std::lock_guard<std::mutex> lock(mutex_);
    MapUnit* u = live_unit(pool_id);
    if (u == nullptr) return Reclassify::NotFound;
    Pool next = u->record;
    next.class_id = class_id;
    write_unit_locked(*u, std::move(next), true);
    return Reclassify::Done;
}

}
