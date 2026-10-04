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

}
