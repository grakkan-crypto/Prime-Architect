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

bool is_rules_directives_name(const std::string& name) {
    std::string upper;
    upper.reserve(name.size());
    for (unsigned char c : name)
        upper.push_back(static_cast<char>(std::toupper(c)));
    for (const char* root : kRulesDirectivesRootWords)
        if (upper.find(root) != std::string::npos) return true;
    return false;
}

}

const std::vector<std::string> LiveRegistry::declared_needs = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
    payload_categories::kPools,
};

void LiveRegistry::store_pipeline(const std::string& pipeline_name,
                                  const PipelinePayload& payload) {

    std::vector<std::string> roster;
    std::vector<LivePool>    table;
    bool built = true;

    try {
        roster = payload.roster;

        for (const auto& declared : payload.pools) {
            if (declared.name != kSharedContextPoolName) continue;
            LivePool p;
            p.class_id    = kSharedContextClassId;
            p.declaration = declared;
            table.push_back(std::move(p));
            break;
        }

        {
            LivePool p;
            p.class_id         = kRulesDirectivesClassId;
            p.declaration.name = kRulesDirectivesPoolName;
            table.push_back(std::move(p));
        }

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

            wellness_check_pipeline_landed =
                pipeline_name_ == pipeline_name &&
                roster_.size() == expected_roster &&
                pools_.size()  == expected_pools;
        }
    }
    (void)wellness_check_pipeline_landed;

    {
        std::lock_guard<std::mutex> lock(links_mutex_);
        prompt_links_.clear();
    }
}

std::string LiveRegistry::pipeline_name() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return pipeline_name_;
}

bool LiveRegistry::loaded() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return !pipeline_name_.empty();
}

std::vector<std::string> LiveRegistry::agent_names() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return roster_;
}

std::vector<LivePool> LiveRegistry::pools() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return pools_;
}

std::optional<LivePool> LiveRegistry::pool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);

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
    return 0;
}

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

void LiveRegistry::set_rebuttal_active(bool active) {
    const bool was = rebuttal_active_.exchange(active, std::memory_order_acq_rel);
    if (was == active) return;

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

namespace {

LiveRegistry the_one;
}

LiveRegistry& live_registry() {
    return the_one;
}

}
