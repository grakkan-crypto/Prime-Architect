// pool_maintenance.cpp — the pool: minted, resized, reclassified, flagged and
// destroyed here, nowhere else.

#include "pool_maintenance.h"

#include "id_generation.h"
#include "live_registry.h"

#include <algorithm>
#include <chrono>

namespace prime {

// ---------------------------------------------------------------------------
// Teardown of the whole set: every block back.
// ---------------------------------------------------------------------------
PoolMaintenance::~PoolMaintenance() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, p] : pools_) release_blocks_locked(*p);
    pools_.clear();
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
    // Whether this pool actually came to stand. Posted once, at the end,
    // whatever happened on the way. The caller is not told; Wellness is.
    bool wellness_check_pool_created = false;

    const std::uint64_t class_id = resolve_class(cls);
    if (class_id != 0) { // zero: no such class — refused, not invented
        auto p = std::make_unique<Pool>();
        p->pool_id      = IdGeneration::instance().mint_pool_id();
        p->class_id     = class_id;
        p->turn_id      = turn_id;
        p->timestamp_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        // One block is one allocation unit, as the allocator reports it.
        p->block_size = mem_.granularity();

        std::lock_guard<std::mutex> lock(mutex_);

        // The id chain: prompt id(s) come off the one pool this continues. A
        // continuation naming a pool that is not here is a refusal.
        bool chain_ok = true;
        if (!continues_from.empty()) {
            auto it = pools_.find(continues_from);
            if (it == pools_.end()) chain_ok = false;
            else                    p->prompt_ids = it->second->prompt_ids;
        }

        // The first block. If it cannot be taken there is no pool — not an
        // empty one standing in for the one that was asked for.
        if (chain_ok && take_block_locked(*p)) {
            if (pool_id_out != nullptr) *pool_id_out = p->pool_id;
            const std::string id = p->pool_id;
            pools_.emplace(id, std::move(p));
            wellness_check_pool_created = true;
        }
    }

    (void)wellness_check_pool_created;
}

// ---------------------------------------------------------------------------
// Grow / shrink
// ---------------------------------------------------------------------------
bool PoolMaintenance::grow(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(pool_id);
    if (it == pools_.end()) return false;
    return take_block_locked(*it->second);
}

void PoolMaintenance::shrink(const std::string& pool_id, std::uint64_t keep_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(pool_id);
    if (it == pools_.end()) return;
    Pool& p = *it->second;

    const std::uint64_t needed_blocks =
        (keep_bytes + p.block_size - 1) / p.block_size;

    // Releasing only ever touches blocks beyond keep_bytes, appended last, so
    // nothing a reader is currently looking at moves or disappears.
    while (p.blocks.size() > needed_blocks) {
        mem_.release(p.blocks.back(), p.block_size);
        p.blocks.pop_back();
    }
    p.byte_capacity = p.blocks.size() * p.block_size;

    // The tail may not point past the blocks that now exist.
    if (p.tail.load(std::memory_order_acquire) > keep_bytes)
        p.tail.store(keep_bytes, std::memory_order_release);
}

bool PoolMaintenance::take_block_locked(Pool& p) {
    // Appended. Appending never touches a block already holding content, so
    // concurrent readers are unaffected.
    uint8_t* b = mem_.acquire(p.block_size);

    // The machine had no block to give — the only thing that can fail this.
    // Posted at the instant it is known.
    const bool wellness_check_pool_out_of_memory = (b == nullptr);
    (void)wellness_check_pool_out_of_memory;

    if (b == nullptr) return false;
    p.blocks.push_back(b);
    p.byte_capacity = p.blocks.size() * p.block_size;
    return true;
}

void PoolMaintenance::release_blocks_locked(Pool& p) {
    for (uint8_t* b : p.blocks) mem_.release(b, p.block_size);
    p.blocks.clear();
    p.byte_capacity = 0;
}

// ---------------------------------------------------------------------------
// The one matcher
// ---------------------------------------------------------------------------
bool PoolMaintenance::matches(const Pool& p, const PoolFilter& f) {
    if (std::find(f.exclude.begin(), f.exclude.end(), p.pool_id) != f.exclude.end())
        return false;
    if (f.pool_id  && *f.pool_id  != p.pool_id)  return false;
    if (f.class_id && *f.class_id != p.class_id) return false;
    if (f.turn_id  && *f.turn_id  != p.turn_id)  return false;
    if (f.prompt_id &&
        std::find(p.prompt_ids.begin(), p.prompt_ids.end(), *f.prompt_id) == p.prompt_ids.end())
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// Destroy / flag / unflag
// ---------------------------------------------------------------------------
std::uint64_t PoolMaintenance::destroy(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    for (auto it = pools_.begin(); it != pools_.end();) {
        Pool& p = *it->second;
        const bool immune =
            std::find(p.immune_from.begin(), p.immune_from.end(), source) != p.immune_from.end();
        if (matches(p, filter) && !immune) {
            release_blocks_locked(p);
            it = pools_.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    return n;
}

std::uint64_t PoolMaintenance::flag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    for (auto& [id, p] : pools_) {
        if (!matches(*p, filter)) continue;
        if (std::find(p->immune_from.begin(), p->immune_from.end(), source) == p->immune_from.end())
            p->immune_from.push_back(source);
        ++n;
    }
    return n;
}

std::uint64_t PoolMaintenance::unflag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t n = 0;
    for (auto& [id, p] : pools_) {
        if (!matches(*p, filter)) continue;
        auto& list = p->immune_from;
        auto pos = std::find(list.begin(), list.end(), source);
        if (pos != list.end()) list.erase(pos);
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Reclassify — class_id on the pool, nothing else
// ---------------------------------------------------------------------------
PoolMaintenance::Reclassify
PoolMaintenance::reclassify(const std::string& pool_id, const ClassRef& cls) {
    const std::uint64_t class_id = resolve_class(cls);
    if (class_id == 0) return Reclassify::NotFound;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(pool_id);
    if (it == pools_.end()) return Reclassify::NotFound;
    it->second->class_id = class_id;
    return Reclassify::Done;
}

} // namespace prime
