// context_matcher.cpp — ContextMatcher implementation

#include "context_matcher.h"

#include "live_registry.h"
#include "pool_maintenance.h"
#include "watcher.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// BUILD OUTLINE — TO BE REMOVED ONCE MASKING IS BUILT.
// Masking is told of every change to a prompt ID's pools as it is made: the
// prompt IDs and the pool IDs just noted against them, or just taken off
// them. Called exactly as it will exist; its real header replaces these
// declarations outright.
// ---------------------------------------------------------------------------
namespace prime {

void Masking_Link(const std::vector<std::string>& prompt_ids,
                  const std::vector<std::string>& pool_ids);
void Masking_Unlink(const std::vector<std::string>& prompt_ids,
                    const std::vector<std::string>& pool_ids);

} // namespace prime

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

// One pool's lifts in the evaluation: its most recent, oldest first, and the
// sum and count of all of them.
struct Track {
    std::deque<float> recent;
    double            sum   = 0.0;
    std::uint64_t     count = 0;
};

// The evaluation of the held prompt ID: the prompt ID being evaluated, each
// pool's lifts, and the pools noted against it.
std::string                  evaluating;
std::map<std::string, Track> tracks;
std::vector<std::string>     noted;

// The evaluation's thresholds. A pool qualifies once it has kSettle recent
// lifts and either their mean reaches kSustained, or at least kStandoutHits
// of its last kStandoutWindow lifts reach kStandout. A noted pool that no
// longer qualifies is taken off once the mean of all its lifts is below
// kBackground.
constexpr std::size_t kSettle        = 8;
constexpr std::size_t kWindow        = 16;
constexpr float       kSustained     = 2.5f;
constexpr float       kStandout      = 10.0f;
constexpr std::size_t kStandoutWindow = 8;
constexpr std::size_t kStandoutHits  = 3;
constexpr double      kBackground    = 1.0;

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
        if (newest == nullptr) return;
        live_registry().link_prompt(prompt, {});
        held = std::move(prompt);
    }).detach();
    return true;
}

void ContextMatcher_Evaluate(const AttentionStep& step) {
    if (evaluating != held) { evaluating = held; tracks.clear(); noted.clear(); }
    if (evaluating.empty()) return;
    if (step.last) {
        live_registry().link_prompt(evaluating, std::move(noted));
        held.clear();
        evaluating.clear();
        tracks.clear();
        noted.clear();
        return;
    }

    std::uint64_t all = 0;
    for (const PoolAttention& p : step.pools) all += p.tokens;

    std::vector<std::string> added, removed;
    for (const PoolAttention& p : step.pools) {
        Track& t = tracks[p.pool_id];
        const float lift = p.tokens != 0 && step.class_total > 0.0f
                               ? p.weight / step.class_total * static_cast<float>(all) / static_cast<float>(p.tokens)
                               : 0.0f;
        t.recent.push_back(lift);
        if (t.recent.size() > kWindow) t.recent.pop_front();
        t.sum += lift;
        ++t.count;

        bool qualifies = false;
        if (t.recent.size() >= kSettle) {
            float       sum  = 0.0f;
            std::size_t hits = 0;
            for (std::size_t i = 0; i < t.recent.size(); ++i) {
                sum += t.recent[i];
                if (i + kStandoutWindow >= t.recent.size() && t.recent[i] >= kStandout) ++hits;
            }
            qualifies = sum / static_cast<float>(t.recent.size()) >= kSustained || hits >= kStandoutHits;
        }

        const auto at = std::find(noted.begin(), noted.end(), p.pool_id);
        if (at == noted.end()) {
            if (qualifies) { noted.push_back(p.pool_id); added.push_back(p.pool_id); }
        } else if (!qualifies && t.sum / static_cast<double>(t.count) < kBackground) {
            noted.erase(at);
            removed.push_back(p.pool_id);
        }
    }
    if (!added.empty())   Masking_Link({ evaluating }, added);
    if (!removed.empty()) Masking_Unlink({ evaluating }, removed);
}

void ContextMatcher_Release(const std::vector<std::string>& prompt_ids) {
    for (const std::string& id : prompt_ids) live_registry().unlink_prompt(id);
}

} // namespace prime
