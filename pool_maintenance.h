#pragma once

#include "id_generation.h"
#include "live_registry.h"

#include <cstdint>
#include <deque>
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

struct MapUnit {
    std::uint64_t timestamp_ns  = 0;
    std::uint64_t byte_capacity = 0;
    std::uint64_t section       = 0;
    char          pool_id  [kPoolIdBytes]   = {};
    char          turn_id  [kTurnIdBytes]   = {};
    char          prompt_id[kPromptIdBytes] = {};
    std::uint8_t  class_id = 0;
    std::uint8_t  flags    = 0;
};

inline constexpr std::uint8_t kDestructionBit = 1u << 0;
inline constexpr std::uint8_t kImmunityBit    = 1u << 1;

struct MapKey {
    std::uint64_t unit_size = 0, units_per_page = 0,
                  pool_id = 0, class_id = 0, turn_id = 0, prompt_id = 0, timestamp_ns = 0,
                  byte_capacity = 0, section = 0, flags = 0,
                  destruction_bit = 0, immunity_bit = 0;
};

struct MapHead {
    MapKey        key;
    std::uint64_t unit_count = 0;
};

struct Gate {
    std::mutex                   m;
    std::multiset<std::uint64_t> holders;
    std::uint64_t                generation = 0;
    bool                         closed     = false;
};

struct KVSection {
    std::uint64_t unit   = 0;
    std::uint8_t* bytes  = nullptr;
    std::uint64_t length = 0;
};

struct Stretch {
    std::uint8_t* at    = nullptr;
    std::uint64_t bytes = 0;
};

struct PoolRead {
    Gate*         gate    = nullptr;
    std::uint64_t stamp   = 0;
    bool          granted = false;
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

    Gate screen_gate;

    Gate& pool_gate(std::uint64_t unit);

    void boot();

    std::vector<Stretch> give_back(std::uint64_t bytes);

    PoolRead arrive(Gate& gate);
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
    static constexpr std::uint64_t kNone = ~0ull;

    static std::uint64_t resolve_class(const ClassRef& cls);

    bool matches(std::uint64_t unit, const PoolFilter& f) const;

    const MapUnit* unit_on_screen(std::uint64_t unit) const;

    std::uint64_t unit_count() const;

    std::uint8_t* take_page_locked();

    bool put_unit_locked(std::uint64_t unit, const MapUnit& content);

    void write_field_locked(std::uint64_t unit, std::uint64_t offset, std::uint8_t value, int op);

    void reclaim_locked();

    std::uint64_t live_unit(const std::string& pool_id) const;

    std::uint8_t* ram_       = nullptr;
    std::uint64_t ram_bytes_ = 0;
    std::uint8_t* ram_next_  = nullptr;

    std::uint64_t page_bytes_     = 0;
    std::uint64_t units_per_page_ = 0;

    std::vector<std::uint8_t*> free_pages_;

    struct Retired {
        std::uint8_t* page       = nullptr;
        std::uint64_t generation = 0;
    };
    std::vector<Retired> retired_;

    std::vector<Stretch> held_;
    std::uint64_t        held_bytes_ = 0, used_bytes_ = 0;
    std::uint8_t*        vram_next_ = nullptr, *vram_end_ = nullptr;

    std::deque<Gate> pool_gates_;

    mutable std::mutex mutex_;
};

PoolMaintenance& pool_maintenance();

}
