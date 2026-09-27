// live_registry.cpp — the live whiteboard implementation
//
// Every section here holds a fact, or hands one back. The one piece of
// organising is the class id walk inside the landing. Nothing reads disk,
// nothing validates, nothing derives.

#include "live_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace prime {

namespace {

// Does this name carry a Rules/Directives root word? Uppercased, then a
// plain contains — casing, separators, order, and suffixes fall away. A
// name check against two fixed roots, not a meaning check.
bool is_rules_directives_name(const std::string& name) {
    std::string upper;
    upper.reserve(name.size());
    for (unsigned char c : name)
        upper.push_back(static_cast<char>(std::toupper(c)));
    for (const char* root : kRulesDirectivesRootWords)
        if (upper.find(root) != std::string::npos) return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// The standing declaration — data, read directly by whoever hands over.
// ---------------------------------------------------------------------------

const std::vector<std::string> LiveRegistry::declared_needs = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
    payload_categories::kPools,
};

// ---------------------------------------------------------------------------
// The pipeline, landed in one motion
// ---------------------------------------------------------------------------

void LiveRegistry::store_pipeline(const std::string& pipeline_name,
                                  const PipelinePayload& payload) {
    // The new state is built outside the lock, before anything held is
    // touched. Whether this succeeds or throws changes nothing about what
    // happens next: the old pipeline comes down regardless.
    std::vector<std::string> roster;
    std::vector<LivePool>    table;
    bool built = true;

    try {
        roster = payload.roster;

        // ---- the fixed slots, first ------------------------------------
        // Shared context: found by name, wherever it sits in the incoming
        // table, and carried across verbatim with id 1.
        for (const auto& declared : payload.pools) {
            if (declared.name != kSharedContextPoolName) continue;
            LivePool p;
            p.class_id    = kSharedContextClassId;
            p.declaration = declared;
            table.push_back(std::move(p));
            break;
        }

        // Rules/Directives: always id 2, whether or not the incoming table
        // names it. The entry exists so the id can be found; its
        // permissions are the Rules file's ruling, not this table's.
        {
            LivePool p;
            p.class_id         = kRulesDirectivesClassId;
            p.declaration.name = kRulesDirectivesPoolName;
            table.push_back(std::move(p));
        }

        // ---- everything the pipeline declares, numbered from 3 ----------
        // Top to bottom, stepping over the two fixed names. The counter
        // knows nothing about what it is counting.
        std::uint64_t next = kFirstDeclaredClassId;
        for (const auto& declared : payload.pools) {
            if (declared.name == kSharedContextPoolName) continue;
            if (is_rules_directives_name(declared.name)) continue;
            LivePool p;
            p.class_id    = next++;
            p.declaration = declared;
            table.push_back(std::move(p));
        }
    } catch (...) {
        built = false;
    }

    const size_t expected_pools  = table.size();
    const size_t expected_roster = roster.size();

    // ---- OLD STATE DOWN — UNCONDITIONAL, ALWAYS FIRST ----------------------
    // A failed landing does not mean the old pipeline is still true. Nothing
    // is left standing that could be mistaken for current while a failure
    // is being worked out: this whiteboard reads as unloaded until a new
    // landing actually succeeds.
    bool wellness_check_pipeline_landed = false;
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);

        pipeline_name_.clear();
        roster_.clear();
        pools_.clear();
        temperatures_.clear();

        if (built) {
            pipeline_name_ = pipeline_name;
            roster_        = std::move(roster);
            pools_         = std::move(table);

            // The transference check: what is now held is what was handed.
            wellness_check_pipeline_landed =
                pipeline_name_ == pipeline_name &&
                roster_.size() == expected_roster &&
                pools_.size()  == expected_pools;
        }
    }
    (void)wellness_check_pipeline_landed;

    // Prompt links belong to the pipeline that generated them. A landing
    // makes every one of them meaningless, win or lose — cleared every
    // time this fires, not only on success. Pool-file tags are untouched:
    // those are direct reads off standing pools, and their owner clears
    // them when it tears those pools down.
    {
        std::lock_guard<std::mutex> lock(links_mutex_);
        prompt_links_.clear();
    }
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

std::string LiveRegistry::pipeline_name() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return pipeline_name_;
}

bool LiveRegistry::loaded() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return !pipeline_name_.empty();
}

// ---------------------------------------------------------------------------
// Roster
// ---------------------------------------------------------------------------

std::vector<std::string> LiveRegistry::agent_names() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return roster_;
}

// ---------------------------------------------------------------------------
// Pools — class ids and permissions, read straight off the held table
// ---------------------------------------------------------------------------

std::vector<LivePool> LiveRegistry::pools() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return pools_;
}

std::optional<LivePool> LiveRegistry::pool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    // Any accepted form of the Rules/Directives name resolves to the fixed
    // slot, whatever name that slot is held under.
    if (is_rules_directives_name(name)) {
        for (const auto& p : pools_)
            if (p.class_id == kRulesDirectivesClassId) return p;
        return std::nullopt;
    }
    for (const auto& p : pools_)
        if (p.declaration.name == name) return p;
    return std::nullopt;
}

std::uint64_t LiveRegistry::class_id_for(const std::string& name) const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (is_rules_directives_name(name)) return kRulesDirectivesClassId;
    for (const auto& p : pools_)
        if (p.declaration.name == name) return p.class_id;
    return 0; // not a class here
}

// ---------------------------------------------------------------------------
// Temperature — held here, written onto here, never worked out here.
// ---------------------------------------------------------------------------

std::vector<LiveTemperature> LiveRegistry::temperatures() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return temperatures_;
}

std::optional<double>
LiveRegistry::temperature_value(const std::string& team,
                                const std::string& identifier) const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    for (const auto& e : temperatures_)
        if (e.team == team &&
            std::find(e.answers_to.begin(), e.answers_to.end(), identifier) != e.answers_to.end())
            return e.value;
    return std::nullopt;
}

std::optional<bool>
LiveRegistry::temperature_is_default(const std::string& team,
                                     const std::string& identifier) const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    for (const auto& e : temperatures_)
        if (e.team == team &&
            std::find(e.answers_to.begin(), e.answers_to.end(), identifier) != e.answers_to.end())
            return e.is_default;
    return std::nullopt;
}

void LiveRegistry::commit_temperatures(std::vector<LiveTemperature> set) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    temperatures_ = std::move(set);
}

void LiveRegistry::acknowledge_defaults() {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    for (auto& e : temperatures_) e.is_default = false;
}

// ---------------------------------------------------------------------------
// Rebuttal switch — the one switch, and its cascade.
// ---------------------------------------------------------------------------

void LiveRegistry::set_rebuttal_active(bool active) {
    const bool was = rebuttal_active_.exchange(active, std::memory_order_acq_rel);
    if (was == active) return; // no change — no signal; recipients assume no change

    // Told on change, outside any lock: the switch telling the files.
    std::vector<std::function<void(bool)>> signals;
    {
        std::lock_guard<std::mutex> lock(rebuttal_signal_mutex_);
        signals = rebuttal_signals_;
    }
    for (const auto& s : signals) s(active);
}

bool LiveRegistry::rebuttal_active() const {
    return rebuttal_active_.load(std::memory_order_acquire);
}

void LiveRegistry::on_rebuttal_switch(std::function<void(bool active)> signal) {
    std::lock_guard<std::mutex> lock(rebuttal_signal_mutex_);
    rebuttal_signals_.push_back(std::move(signal));
}

// ---------------------------------------------------------------------------
// Prompt links — standing memory, whole-set writes only.
// ---------------------------------------------------------------------------

void LiveRegistry::link_prompt(const std::string& prompt_id,
                               std::vector<std::string> linked_pool_ids) {
    std::lock_guard<std::mutex> lock(links_mutex_);
    prompt_links_[prompt_id] = std::move(linked_pool_ids);
}

std::vector<std::string> LiveRegistry::linked_pools(const std::string& prompt_id) const {
    std::lock_guard<std::mutex> lock(links_mutex_);
    auto it = prompt_links_.find(prompt_id);
    return it == prompt_links_.end() ? std::vector<std::string>{} : it->second;
}

void LiveRegistry::unlink_prompt(const std::string& prompt_id) {
    std::lock_guard<std::mutex> lock(links_mutex_);
    prompt_links_.erase(prompt_id);
}

// ---------------------------------------------------------------------------
// Files — Pool ID -> source file tag. Its own section, written by the
// FileLoader in the mint motion. ONE store; file -> pools is a scan of it,
// never a second map.
// ---------------------------------------------------------------------------

void LiveRegistry::set_pool_file_tag(const std::string& pool_id,
                                     const std::string& file) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    pool_file_tags_[pool_id] = file;
}

std::optional<std::string> LiveRegistry::pool_file_tag(
        const std::string& pool_id) const {
    std::lock_guard<std::mutex> lock(files_mutex_);
    auto it = pool_file_tags_.find(pool_id);
    if (it == pool_file_tags_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> LiveRegistry::pools_with_file_tag(
        const std::string& file) const {
    std::lock_guard<std::mutex> lock(files_mutex_);
    std::vector<std::string> out;
    for (const auto& [pool_id, f] : pool_file_tags_)
        if (f == file) out.push_back(pool_id);
    return out;
}

void LiveRegistry::clear_pool_file_tag(const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    pool_file_tags_.erase(pool_id);
}

void LiveRegistry::clear_file_tags(const std::string& file) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    for (auto it = pool_file_tags_.begin(); it != pool_file_tags_.end();) {
        if (it->second == file) it = pool_file_tags_.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------------------
// The one live instance
// ---------------------------------------------------------------------------

namespace {
// Created at system load, static storage: it is there before any pipeline
// is, and stays for the life of the process. Nothing about a pipeline
// brings it into being.
LiveRegistry the_one;
} // namespace

LiveRegistry& live_registry() {
    return the_one;
}

} // namespace prime
