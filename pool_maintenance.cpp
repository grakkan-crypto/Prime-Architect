// pool_maintenance.cpp — the pool: minted, grown, reclassified, flagged and
// destroyed here, nowhere else. Every read of a pool's bytes is granted here.

#include "pool_maintenance.h"

#include "id_generation.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>

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
    ram_            = b;
    ram_bytes_      = ram_bytes;
    page_bytes_     = os_page_bytes();
    units_per_page_ = page_bytes_ / sizeof(MapUnit);
    ram_next_       = b + (page_bytes_ - reinterpret_cast<std::uintptr_t>(b) % page_bytes_) % page_bytes_;
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

std::uint8_t* PoolMaintenance::take_page_locked() {
    std::uint8_t* p = nullptr;
    if (!free_pages_.empty()) { p = free_pages_.back(); free_pages_.pop_back(); }
    else if (ram_next_ + page_bytes_ <= ram_ + ram_bytes_) { p = ram_next_; ram_next_ += page_bytes_; }
    else p = ram_manager_supply(page_bytes_, true);
    if (p != nullptr) std::memset(p, 0, page_bytes_);
    return p;
}

std::uint64_t PoolMaintenance::unit_count() const {
    return __atomic_load_n(&reinterpret_cast<const MapHead*>(LiveRegistry::screen)->unit_count, __ATOMIC_ACQUIRE);
}

const MapUnit* PoolMaintenance::unit_on_screen(std::uint64_t unit) const {
    return reinterpret_cast<const MapUnit*>(LiveRegistry::screen + (1 + unit / units_per_page_) * page_bytes_
                                            + (unit % units_per_page_) * sizeof(MapUnit));
}

bool PoolMaintenance::put_unit_locked(std::uint64_t unit, const MapUnit& content) {
    const std::uint8_t* const screen_page = LiveRegistry::screen + (1 + unit / units_per_page_) * page_bytes_;
    std::uint8_t* const fresh = take_page_locked();
    if (fresh == nullptr) return false;
    if (const std::uint8_t* const cur = os_screen_shown(screen_page)) std::memcpy(fresh, cur, page_bytes_);
    std::memcpy(fresh + (unit % units_per_page_) * sizeof(MapUnit), &content, sizeof(MapUnit));
    {
        std::lock_guard<std::mutex> g(screen_gate.m);
        if (std::uint8_t* const old = os_screen_show(screen_page, fresh))
            retired_.push_back({ old, ++screen_gate.generation });
    }
    reclaim_locked();
    return true;
}

void PoolMaintenance::write_field_locked(std::uint64_t unit, std::uint64_t offset, std::uint8_t value, int op) {
    std::uint8_t* const at = os_screen_shown(LiveRegistry::screen + (1 + unit / units_per_page_) * page_bytes_)
                             + (unit % units_per_page_) * sizeof(MapUnit) + offset;
    if (op == 0)      __atomic_store_n (at, value, __ATOMIC_RELEASE);
    else if (op == 1) __atomic_fetch_or (at, value, __ATOMIC_RELEASE);
    else              __atomic_fetch_and(at, static_cast<std::uint8_t>(~value), __ATOMIC_RELEASE);
}

void PoolMaintenance::reclaim_locked() {
    std::lock_guard<std::mutex> g(screen_gate.m);
    const std::uint64_t lo = screen_gate.holders.empty() ? kNone : *screen_gate.holders.begin();
    std::size_t k = 0;
    for (const Retired& r : retired_) {
        if (r.generation <= lo) free_pages_.push_back(r.page);
        else retired_[k++] = r;
    }
    retired_.resize(k);
}

Gate& PoolMaintenance::pool_gate(std::uint64_t unit) {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_gates_[unit];
}

PoolRead PoolMaintenance::arrive(Gate& gate) {
    std::lock_guard<std::mutex> lock(gate.m);
    if (gate.closed) return {};
    gate.holders.insert(gate.generation);
    return { &gate, gate.generation, true };
}

void PoolMaintenance::leave(const PoolRead& read, bool died) {
    (void)died;
    if (!read.granted) return;
    if (read.gate != &screen_gate) {
        std::lock_guard<std::mutex> lock(read.gate->m);
        read.gate->holders.erase(read.gate->holders.find(read.stamp));
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    {
        std::lock_guard<std::mutex> g(screen_gate.m);
        screen_gate.holders.erase(screen_gate.holders.find(read.stamp));
    }
    reclaim_locked();
}

std::uint64_t PoolMaintenance::live_unit(const std::string& pool_id) const {
    const std::uint64_t n = unit_count();
    for (std::uint64_t i = 0; i < n; ++i) {
        const MapUnit* const u = unit_on_screen(i);
        if (u->flags & kDestructionBit) continue;
        if (pool_id == u->pool_id) return i;
    }
    return kNone;
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
    if (class_id == 0) return;
    MapUnit c;
    const std::string id = IdGeneration::instance().mint_pool_id();
    c.pool_id = id;
    c.turn_id = turn_id;
    c.class_id     = static_cast<std::uint8_t>(class_id);
    c.timestamp_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::lock_guard<std::mutex> lock(mutex_);

    if (!continues_from.empty()) {
        const std::uint64_t from = live_unit(continues_from);
        if (from == kNone) return;
        c.prompt_id = unit_on_screen(from)->prompt_id;
    }

    std::uint8_t* s = nullptr;
    if (static_cast<std::uint64_t>(vram_end_ - vram_next_) >= kPoolBytes) { s = vram_next_; vram_next_ += kPoolBytes; }
    else if ((s = ram_manager_supply(kPoolBytes, false)) != nullptr) { held_.push_back({ s, kPoolBytes }); held_bytes_ += kPoolBytes; }
    if (s == nullptr) return;
    used_bytes_    += kPoolBytes;
    c.section       = reinterpret_cast<std::uint64_t>(s);
    c.byte_capacity = kPoolBytes;

    const std::uint64_t n = unit_count();
    if (!put_unit_locked(n, c)) return;
    pool_gates_.emplace_back();
    __atomic_store_n(&reinterpret_cast<MapHead*>(os_screen_shown(LiveRegistry::screen))->unit_count, n + 1, __ATOMIC_RELEASE);
    if (pool_id_out != nullptr) *pool_id_out = id;
}

bool PoolMaintenance::grow(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return live_unit(pool_id) != kNone;
}

bool PoolMaintenance::matches(std::uint64_t unit, const PoolFilter& f) const {
    const MapUnit* const u = unit_on_screen(unit);
    const std::string& id = u->pool_id;
    if (std::find(f.exclude.begin(), f.exclude.end(), id) != f.exclude.end()) return false;
    if (f.pool_id   && *f.pool_id  != id)          return false;
    if (f.class_id  && *f.class_id != u->class_id) return false;
    if (f.turn_id   && *f.turn_id  != u->turn_id)       return false;
    if (f.prompt_id && *f.prompt_id != u->prompt_id) return false;
    return true;
}

std::uint64_t PoolMaintenance::destroy(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    const std::uint64_t count = unit_count();
    for (std::uint64_t i = 0; i < count; ++i) {
        const MapUnit* const u = unit_on_screen(i);
        if ((u->flags & kDestructionBit) || !matches(i, filter)) continue;
        if (u->flags & kImmunityBit) continue;
        write_field_locked(i, offsetof(MapUnit, flags), kDestructionBit, 1);
        {
            std::lock_guard<std::mutex> g(pool_gates_[i].m);
            pool_gates_[i].closed = true;
        }
        ++n;
    }
    return n;
}

std::uint64_t PoolMaintenance::flag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    const std::uint64_t count = unit_count();
    for (std::uint64_t i = 0; i < count; ++i) {
        const MapUnit* const u = unit_on_screen(i);
        if ((u->flags & kDestructionBit) || !matches(i, filter)) continue;
        ++n;
        write_field_locked(i, offsetof(MapUnit, flags), kImmunityBit, 1);
    }
    return n;
}

std::uint64_t PoolMaintenance::unflag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    const std::uint64_t count = unit_count();
    for (std::uint64_t i = 0; i < count; ++i) {
        const MapUnit* const u = unit_on_screen(i);
        if ((u->flags & kDestructionBit) || !matches(i, filter)) continue;
        ++n;
        write_field_locked(i, offsetof(MapUnit, flags), kImmunityBit, 2);
    }
    return n;
}

PoolMaintenance::Reclassify
PoolMaintenance::reclassify(const std::string& pool_id, const ClassRef& cls) {
    const std::uint64_t class_id = resolve_class(cls);
    if (class_id == 0) return Reclassify::NotFound;

    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t i = live_unit(pool_id);
    if (i == kNone) return Reclassify::NotFound;
    write_field_locked(i, offsetof(MapUnit, class_id), static_cast<std::uint8_t>(class_id), 0);
    return Reclassify::Done;
}

}
