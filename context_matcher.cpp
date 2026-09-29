// context_matcher.cpp — ContextMatcher implementation

#include "context_matcher.h"

#include "live_registry.h"
#include "pool_maintenance.h"
#include "watcher.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace prime {

namespace {

// The input pools the matching serves, by registry name (coder.cpp).
constexpr const char* kInputPools[] = { "ANALYST_INPUT", "ADEPT_INPUT" };

// The map key: asked for once, before the first visit of the session, held.
std::once_flag key_once;
MapKey         key;

// The prompt ID of the newest pool.
std::string held;

// The prompt agents: every agent with write access on an input pool, each
// once. Held for the pipeline's lifespan.
std::vector<std::string> prompt_agents;

// A pool ID's sequence number: what follows its last '-'.
std::uint64_t sequence(const std::string& pool_id) {
    return std::strtoull(pool_id.c_str() + pool_id.rfind('-') + 1, nullptr, 10);
}

} // namespace

void ContextMatcher_Watch() {
    prompt_agents.clear();
    for (const char* pool : kInputPools) {
        const std::uint64_t class_id = live_registry().class_id_for(pool);
        const bool wellness_check_context_matcher_class_found = class_id != 0;
        (void)wellness_check_context_matcher_class_found;
        if (class_id == 0) continue;

        const PoolDeclaration d = live_registry().pool(pool)->declaration;
        for (const PoolAgent& a : d.agents)
            if (permission_key::bit_set(a.bits, permission_key::write_bit(d.mask_count)) &&
                std::find(prompt_agents.begin(), prompt_agents.end(), a.agent) == prompt_agents.end())
                prompt_agents.push_back(a.agent);

        Entry e;
        e.kind                     = SourceKind::PoolMap;
        e.selector.class_id.values = { class_id };
        e.test                     = Test::Exists;
        e.negate                   = false;

        Request r;
        r.name       = std::string("ContextMatcher.") + pool;
        r.triggers   = { e };
        r.combine    = Combine::Or;
        r.recipients = { ContextMatcher_Receive };
        r.message    = pool;
        r.count      = 0;
        r.scopes     = { "pipeline" };
        r.level      = ActiveLevel::Foreground;

        watcher().Watcher_Register(std::move(r));
    }
}

bool ContextMatcher_Receive(const std::string&, const std::string& message) {
    std::thread([pool = message]() {
        const std::uint64_t class_id = live_registry().class_id_for(pool);
        const bool wellness_check_context_matcher_class_found = class_id != 0;
        (void)wellness_check_context_matcher_class_found;
        if (class_id == 0) return;

        std::call_once(key_once, [] { key = pool_maintenance().map_key(); });
        const void* screen = live_registry().screen;

        const std::uint8_t* newest = nullptr;
        std::uint64_t       seq    = 0;
        std::string         prompt;
        const PoolRead read = pool_maintenance().arrive(nullptr);
        const bool wellness_check_context_matcher_map_read_granted = read.granted;
        (void)wellness_check_context_matcher_map_read_granted;
        if (read.granted)
            for (std::uint64_t i = 0; i < key.unit_count; ++i) {
                const std::uint8_t* p = map_record(key, screen, i, read.stamp);
                if (p == nullptr || map_field<std::uint64_t>(p, key.class_id) != class_id) continue;
                const std::uint64_t n = sequence(map_field<std::string>(p, key.pool_id));
                if (newest != nullptr && n <= seq) continue;
                newest = p;
                seq    = n;
                prompt = *map_field<std::set<std::string>>(p, key.prompt_ids).begin();
            }
        pool_maintenance().leave(read);

        const bool wellness_check_context_matcher_class_on_map = newest != nullptr;
        (void)wellness_check_context_matcher_class_on_map;
        if (newest != nullptr) held = std::move(prompt);
    }).detach();
    return true;
}

} // namespace prime
