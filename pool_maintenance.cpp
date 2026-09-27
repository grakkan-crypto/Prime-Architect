// pool_maintenance.cpp — the pool: minted, grown, reclassified, flagged and
// destroyed here, nowhere else. Every action edits the future layer of the
// map and mounts it. This file keeps nothing between calls.

#include "pool_maintenance.h"

#include "id_generation.h"

#include <algorithm>
#include <chrono>

namespace prime {

// ---------------------------------------------------------------------------
// OS_OWES — declared here exactly as they are called; the OS defines them.
// ---------------------------------------------------------------------------

// The future layer of the pool map (live_registry.h), for editing. Pool
// Maintenance alone may have it; the OS refuses anyone else. If the next
// future is still being formed, this waits for it.
PoolMap& os_pool_map_future();

// Mount: the future becomes current, the current becomes old, and the next
// future is formed from the new current — in one indivisible step. False
// when the OS did not mount; the future is then left exactly as edited.
bool os_pool_map_mount();

// Chunks: one unit of pool memory, at the one uniform size. Null when there
// is none to give.
std::uint64_t os_chunk_size();
std::uint8_t* os_chunk_take();
void          os_chunk_return(std::uint8_t* chunk);

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
        PoolMap& future = os_pool_map_future();   // OS_OWES

        // The id chain: prompt id(s) come off the one pool this continues,
        // read off the future layer — this file's own latest state of the
        // map. A continuation naming a pool that is not there is a refusal.
        bool chain_ok = true;
        if (!continues_from.empty()) {
            auto it = future.find(continues_from);
            if (it == future.end()) chain_ok = false;
            else                    p.prompt_ids = it->second.prompt_ids;
        }

        // The first chunk, before the edit. If it cannot be taken there is
        // no pool — not an empty one standing in for the one asked for.
        if (chain_ok && take_chunk_locked(p)) {
            const std::string id = p.pool_id;
            future.emplace(id, std::move(p));               // the edit: the entry, whole

            const bool wellness_check_pool_map_mounted = os_pool_map_mount();   // OS_OWES
            (void)wellness_check_pool_map_mounted;
            // Not mounted: the pool stands in the future layer with its
            // chunk. Nothing is reverted; the flag above says so.
            if (wellness_check_pool_map_mounted) {
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
    PoolMap& future = os_pool_map_future();   // OS_OWES
    auto it = future.find(pool_id);
    if (it == future.end()) return false;
    if (!take_chunk_locked(it->second)) return false;   // the edit: one chunk appended

    const bool wellness_check_pool_map_mounted = os_pool_map_mount();   // OS_OWES
    (void)wellness_check_pool_map_mounted;
    return wellness_check_pool_map_mounted;
}

bool PoolMaintenance::take_chunk_locked(Pool& p) {
    // Appended. Appending never touches a chunk already holding content, so
    // concurrent readers are unaffected.
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
// ---------------------------------------------------------------------------
std::uint64_t PoolMaintenance::destroy(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& future = os_pool_map_future();   // OS_OWES

    // The chunks of every entry removed, returned only after the mount.
    std::vector<std::uint8_t*> chunks;
    std::uint64_t n = 0;
    for (auto it = future.begin(); it != future.end();) {
        const Pool& p = it->second;
        const bool immune =
            std::find(p.immune_from.begin(), p.immune_from.end(), source) != p.immune_from.end();
        if (matches(p, filter) && !immune) {
            chunks.insert(chunks.end(), p.chunks.begin(), p.chunks.end());
            it = future.erase(it);                          // the edit: the entry gone
            ++n;
        } else {
            ++it;
        }
    }
    if (n == 0) return 0;   // nothing edited, nothing to mount

    const bool wellness_check_pool_map_mounted = os_pool_map_mount();   // OS_OWES
    (void)wellness_check_pool_map_mounted;
    // Not mounted: the entries still stand on the current layer, and so do
    // their chunks; nothing goes back and nothing is reverted. The flag
    // says so.
    if (!wellness_check_pool_map_mounted) return 0;

    for (std::uint8_t* c : chunks) os_chunk_return(c);   // OS_OWES
    return n;
}

std::uint64_t PoolMaintenance::flag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& future = os_pool_map_future();   // OS_OWES
    std::uint64_t n = 0;
    for (auto& [id, p] : future) {
        if (!matches(p, filter)) continue;
        if (std::find(p.immune_from.begin(), p.immune_from.end(), source) == p.immune_from.end())
            p.immune_from.push_back(source);                // the edit
        ++n;
    }
    if (n == 0) return 0;

    const bool wellness_check_pool_map_mounted = os_pool_map_mount();   // OS_OWES
    (void)wellness_check_pool_map_mounted;
    return wellness_check_pool_map_mounted ? n : 0;
}

std::uint64_t PoolMaintenance::unflag(const PoolFilter& filter, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& future = os_pool_map_future();   // OS_OWES
    std::uint64_t n = 0;
    for (auto& [id, p] : future) {
        if (!matches(p, filter)) continue;
        auto& list = p.immune_from;
        auto pos = std::find(list.begin(), list.end(), source);
        if (pos != list.end()) list.erase(pos);             // the edit
        ++n;
    }
    if (n == 0) return 0;

    const bool wellness_check_pool_map_mounted = os_pool_map_mount();   // OS_OWES
    (void)wellness_check_pool_map_mounted;
    return wellness_check_pool_map_mounted ? n : 0;
}

// ---------------------------------------------------------------------------
// Reclassify — Class ID on the entry, nothing else
// ---------------------------------------------------------------------------
PoolMaintenance::Reclassify
PoolMaintenance::reclassify(const std::string& pool_id, const ClassRef& cls) {
    const std::uint64_t class_id = resolve_class(cls);
    if (class_id == 0) return Reclassify::NotFound;

    std::lock_guard<std::mutex> lock(mutex_);
    PoolMap& future = os_pool_map_future();   // OS_OWES
    auto it = future.find(pool_id);
    if (it == future.end()) return Reclassify::NotFound;
    it->second.class_id = class_id;                         // the edit

    const bool wellness_check_pool_map_mounted = os_pool_map_mount();   // OS_OWES
    (void)wellness_check_pool_map_mounted;
    return wellness_check_pool_map_mounted ? Reclassify::Done : Reclassify::NotMounted;
}

} // namespace prime
