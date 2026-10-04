// pipeline_loader.cpp — the pipeline trigger point implementation
//
// The whole of one load: run the handed-in pipeline function (the one
// wait), post the payload's shape and stop if it is malformed, post the
// roster/pool cross-check, then fire Rules, Temperature, ModelWeights, and
// the LiveRegistry store together — detached, unwaited, each with its own
// owned data — and return. Nothing retained, nothing reported.
//
// Every decision point is a WELLNESS CHECK: a bare boolean named
// wellness_check_<what it answers>, set the instant it is answered, never
// combined, never handed off. WellnessSystem is positioned at every one of
// them, live; an N is Wellness's cue. The only checks this file reads back
// are the gates — the handed-in checks and the shape checks (Ruling 6) —
// and each is read once, for the gate, and never again.

#include "pipeline_loader.h"

#include "live_registry.h"
#include "model_weights.h"
#include "rules.h"
#include "temperature.h"

#include <algorithm>
#include <thread>

namespace prime {

bool present(const std::string& target);

}

namespace prime {

namespace {

bool needs_covered(const std::vector<std::string>& declared,
                   const std::vector<std::string>& carried) {
    for (const auto& need : declared) {
        if (std::find(carried.begin(), carried.end(), need) == carried.end())
            return false;
    }
    return true;
}

bool fits_in(std::uint32_t value, std::uint32_t width) {
    return (static_cast<std::uint64_t>(value) >> width) == 0;
}

const std::vector<std::string> kCarriedToRules = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
};
const std::vector<std::string> kCarriedToTemperature = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
};
const std::vector<std::string> kCarriedToModelWeights = {
    payload_categories::kRoster,
};
const std::vector<std::string> kCarriedToStore = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
    payload_categories::kPools,
};

}

void PipelineLoader::load(const std::string& pipeline_name,
                          const PipelineFn& pipeline_fn) {

    const bool wellness_check_pipeline_named = !pipeline_name.empty();
    if (!wellness_check_pipeline_named) return;

    const bool wellness_check_pipeline_function_supplied =
        static_cast<bool>(pipeline_fn);
    if (!wellness_check_pipeline_function_supplied) return;

    const PipelinePayload payload = pipeline_fn();

    const bool wellness_check_payload_roster_present = !payload.roster.empty();
    if (!wellness_check_payload_roster_present) return;

    bool roster_ok = true;
    for (const auto& name : payload.roster) {
        if (name.empty()) { roster_ok = false; break; }
    }
    const bool wellness_check_payload_roster_format = roster_ok;
    if (!wellness_check_payload_roster_format) return;

    const bool wellness_check_payload_pools_present = !payload.pools.empty();
    if (!wellness_check_payload_pools_present) return;

    bool pools_ok = true;
    for (const auto& pool : payload.pools) {
        if (pool.name.empty())                    { pools_ok = false; break; }
        if (pool.mask_count > kMaxMasksPerPool)   { pools_ok = false; break; }
        if (!fits_in(pool.mask_triggers, 3 * pool.mask_count)) {
            pools_ok = false; break;
        }
        const std::uint32_t width = 2 + 3 * pool.mask_count;
        for (const auto& entry : pool.agents) {
            if (entry.agent.empty() || !fits_in(entry.bits, width)) {
                pools_ok = false;
                break;
            }
        }
        if (!pools_ok) break;
    }
    const bool wellness_check_payload_pools_format = pools_ok;
    if (!wellness_check_payload_pools_format) return;

    bool every_pool_agent_rostered = true;
    for (const auto& pool : payload.pools) {
        for (const auto& entry : pool.agents) {
            if (std::find(payload.roster.begin(), payload.roster.end(),
                          entry.agent) == payload.roster.end()) {
                every_pool_agent_rostered = false;
                break;
            }
        }
        if (!every_pool_agent_rostered) break;
    }
    const bool wellness_check_payload_every_pool_agent_in_roster =
        every_pool_agent_rostered;
    (void)wellness_check_payload_every_pool_agent_in_roster;

    bool every_roster_agent_pooled = true;
    for (const auto& name : payload.roster) {
        bool found = false;
        for (const auto& pool : payload.pools) {
            for (const auto& entry : pool.agents) {
                if (entry.agent == name) { found = true; break; }
            }
            if (found) break;
        }
        if (!found) { every_roster_agent_pooled = false; break; }
    }
    const bool wellness_check_payload_every_roster_agent_in_a_pool =
        every_roster_agent_pooled;
    (void)wellness_check_payload_every_roster_agent_in_a_pool;

    {
        const bool wellness_check_rules_present =
            present("Rules");
        const bool wellness_check_rules_contract_matches =
            needs_covered(Rules::declared_needs, kCarriedToRules);
        (void)wellness_check_rules_present;
        (void)wellness_check_rules_contract_matches;

        std::thread([name = pipeline_name, roster = payload.roster]() {
            Rules{}.load(name, roster);
        }).detach();
    }

    {
        const bool wellness_check_temperature_present =
            present("Temperature");
        const bool wellness_check_temperature_contract_matches =
            needs_covered(Temperature::declared_needs, kCarriedToTemperature);
        (void)wellness_check_temperature_present;
        (void)wellness_check_temperature_contract_matches;

        std::thread([name = pipeline_name, roster = payload.roster]() {
            Temperature{}.load(name, roster);
        }).detach();
    }

    {
        const bool wellness_check_model_weights_present =
            present("ModelWeights");
        const bool wellness_check_model_weights_contract_matches =
            needs_covered(ModelWeights::declared_needs, kCarriedToModelWeights);
        (void)wellness_check_model_weights_present;
        (void)wellness_check_model_weights_contract_matches;

        std::thread([roster = payload.roster]() {
            ModelWeights{}.load(roster);
        }).detach();
    }

    {
        const bool wellness_check_store_present =
            present("LiveRegistry");
        const bool wellness_check_store_contract_matches =
            needs_covered(LiveRegistry::declared_needs, kCarriedToStore);
        (void)wellness_check_store_present;
        (void)wellness_check_store_contract_matches;

        std::thread([name = pipeline_name, data = payload]() {
            live_registry().store_pipeline(name, data);
        }).detach();
    }

}

}
