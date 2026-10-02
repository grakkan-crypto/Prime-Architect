// context_matcher.cpp — ContextMatcher implementation

#include "context_matcher.h"

#include "live_registry.h"
#include "pool_maintenance.h"
#include "watcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// BUILD OUTLINE — TO BE REMOVED ONCE MASKING IS BUILT.
// Masking is told of every change to a prompt ID's pools as it is made: the
// prompt IDs and the pool IDs just noted against them, or just taken off
// them. Masking hands back its receipt, "got it", and nothing else. Called
// exactly as it will exist; its real header replaces these declarations
// outright.
// ---------------------------------------------------------------------------
namespace prime {

bool Masking_Link(const std::vector<std::string>& prompt_ids,
                  const std::vector<std::string>& pool_ids);
bool Masking_Unlink(const std::vector<std::string>& prompt_ids,
                    const std::vector<std::string>& pool_ids);

} // namespace prime

namespace prime {

namespace {

// The input pools the matching serves, by registry name (coder.cpp).
constexpr const char* kInputPools[] = { "ANALYST_INPUT", "ADEPT_INPUT" };

// The map key: taken before the first visit of the session, held.
bool   has_key = false;
MapKey key;

// The prompt ID being evaluated.
std::string held;

// The prompt agents: every agent with write access on an input pool. Held
// for the pipeline's lifespan.
std::vector<std::string> prompt_agents;

// One pool's lifts in the evaluation: its most recent, oldest first, and the
// sum and count of all of them.
struct Track {
    std::deque<float> recent;
    double            sum   = 0.0;
    std::uint64_t     count = 0;
};

// Each pool's lifts, and the pools noted against the held prompt ID.
std::map<std::string, Track> tracks;
std::vector<std::string>     noted;

// The evaluation's thresholds. A pool qualifies once it has kSettle recent
// lifts and either their mean reaches kSustained, or at least kStandoutHits
// of its last kStandoutWindow lifts reach kStandout. A noted pool is taken
// off once the mean of all its lifts is below kBackground.
constexpr std::size_t kSettle         = 8;
constexpr std::size_t kWindow         = 16;
constexpr float       kSustained      = 2.5f;
constexpr float       kStandout       = 10.0f;
constexpr std::size_t kStandoutWindow = 8;
constexpr std::size_t kStandoutHits   = 3;
constexpr double      kBackground     = 1.0;

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
            if (permission_key::bit_set(a.bits, permission_key::write_bit(d.mask_count)))
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
    const std::uint64_t class_id = live_registry().class_id_for(message);

    if (!has_key) { key = pool_maintenance().map_key(); has_key = true; }
    const void* screen = live_registry().screen;

    const std::uint8_t* newest = nullptr;
    std::uint64_t       seq    = 0;
    std::string         prompt;
    for (std::uint64_t i = 0; i < key.unit_count; ++i) {
        const std::uint8_t* p = map_record(key, screen, i);
        if (map_field<std::uint64_t>(p, key.class_id) != class_id) continue;
        const std::uint64_t n = sequence(map_field<std::string>(p, key.pool_id));
        if (newest != nullptr && n <= seq) continue;
        newest = p;
        seq    = n;
        prompt = *map_field<std::set<std::string>>(p, key.prompt_ids).begin();
    }

    live_registry().link_prompt(prompt, {});
    held = std::move(prompt);
    return true;
}

void ContextMatcher_Evaluate(const AttentionStep& step) {
    {
        double                sum = 0.0;
        bool                  ok  = step.class_total > 0.0f;
        std::set<std::string> seen;
        for (const PoolAttention& p : step.pools) {
            ok = ok && p.weight >= 0.0f && p.tokens > 0 && seen.insert(p.pool_id).second;
            sum += p.weight;
        }
        const bool wellness_check_context_matcher_step_well_formed =
            ok && std::fabs(sum - step.class_total) <= step.class_total * 1e-3;
        (void)wellness_check_context_matcher_step_well_formed;
    }

    if (step.last) {
        live_registry().link_prompt(held, noted);
        const bool wellness_check_context_matcher_pools_posted = live_registry().linked_pools(held) == noted;
        (void)wellness_check_context_matcher_pools_posted;
        held.clear();
        tracks.clear();
        noted.clear();
        return;
    }

    std::uint64_t all = 0;
    for (const PoolAttention& p : step.pools) all += p.tokens;

    for (const PoolAttention& p : step.pools) {
        Track& t = tracks[p.pool_id];
        const float lift = p.weight / step.class_total * static_cast<float>(all) / static_cast<float>(p.tokens);
        t.recent.push_back(lift);
        if (t.recent.size() > kWindow) t.recent.pop_front();
        t.sum += lift;
        ++t.count;

        const auto at = std::find(noted.begin(), noted.end(), p.pool_id);
        if (at != noted.end()) {
            if (t.sum / static_cast<double>(t.count) < kBackground) {
                noted.erase(at);
                const bool wellness_check_context_matcher_masking_unlinked = Masking_Unlink({ held }, { p.pool_id });
                (void)wellness_check_context_matcher_masking_unlinked;
            }
            continue;
        }
        if (t.recent.size() < kSettle) continue;
        float       sum  = 0.0f;
        std::size_t hits = 0;
        for (std::size_t i = 0; i < t.recent.size(); ++i) {
            sum += t.recent[i];
            if (i + kStandoutWindow >= t.recent.size() && t.recent[i] >= kStandout) ++hits;
        }
        if (sum / static_cast<float>(t.recent.size()) >= kSustained || hits >= kStandoutHits) {
            noted.push_back(p.pool_id);
            const bool wellness_check_context_matcher_masking_linked = Masking_Link({ held }, { p.pool_id });
            (void)wellness_check_context_matcher_masking_linked;
        }
    }
}

void ContextMatcher_Release(const std::vector<std::string>& prompt_ids) {
    for (const std::string& id : prompt_ids) live_registry().unlink_prompt(id);
}

} // namespace prime
