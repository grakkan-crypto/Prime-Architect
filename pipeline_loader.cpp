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

// ---------------------------------------------------------------------------
// FORWARD DECLARATION — called exactly as if it exists; built around its
// call, and its real header replaces this declaration outright, with the
// calls below unchanged.
//
// PRESENCE — "is this file here, is this file running?" ONE call, whatever
// the target. Answered from OUTSIDE the target, by the system's own liveness
// mechanism, never by calling into the target itself: a missing file cannot
// report its own absence. The target is named on the way in; the answer is
// a bare boolean; the name is the whole of the specificity. The answer is
// instant — silence IS the N.
//
// DECLARED NEEDS — each target states, as a standing fact about itself, the
// payload categories it currently requires (payload_categories vocabulary).
// It is data on the target, read here directly. The target's declaration
// is the authority.
//
// WAKES — each target's own load, called directly, handed its payload. The
// payload is the only thing that differs from one target to the next.
// Nothing is handed back: whether a target acted is not a fact the target
// asserts about itself. Any header included above that is not yet built is
// built around the call made on it here.
// ---------------------------------------------------------------------------
namespace prime {

bool present(const std::string& target);

} // namespace prime

namespace prime {

namespace {

// Does the carried set cover every declared need? Generic: the same
// comparison for every target, over the one shared category vocabulary.
bool needs_covered(const std::vector<std::string>& declared,
                   const std::vector<std::string>& carried) {
    for (const auto& need : declared) {
        if (std::find(carried.begin(), carried.end(), need) == carried.end())
            return false;
    }
    return true;
}

// Does this value fit in `width` bits — no bit set at or above `width`?
// Structural only. Done in 64 bits so a width of exactly 32 is a defined
// shift, not undefined behaviour.
bool fits_in(std::uint32_t value, std::uint32_t width) {
    return (static_cast<std::uint64_t>(value) >> width) == 0;
}

// What this file hands each target — fixed at build, stated once. These are
// the "carried" side of every contract comparison. Changing what a wake
// carries means changing its list here IN THE SAME EDIT as the wake call
// itself; the two sit together below for exactly that reason.
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

} // namespace

// ---------------------------------------------------------------------------
// THE ONE CALL
// ---------------------------------------------------------------------------
void PipelineLoader::load(const std::string& pipeline_name,
                          const PipelineFn& pipeline_fn) {
    // ---- HANDED IN -----------------------------------------------------
    // The caller supplies both. Either missing is an immediate N: there is
    // nothing to run and nothing to run it on. The function being here IS
    // "is anybody there" — the only handle this file has on a pipeline,
    // decided by the caller before the pipeline is touched. The check IS
    // the loudness — Wellness sees it; the caller, a messenger, is told
    // nothing.
    const bool wellness_check_pipeline_named = !pipeline_name.empty();
    if (!wellness_check_pipeline_named) return;

    const bool wellness_check_pipeline_function_supplied =
        static_cast<bool>(pipeline_fn);
    if (!wellness_check_pipeline_function_supplied) return;

    // ---- THE ONE WAIT --------------------------------------------------
    // The payload is the input to everything downstream — the one genuine
    // data dependency, and the only thing in this file ever waited on. A
    // bare wake: nothing is passed. The pipeline's own bespoke onward wake
    // fires inside this call, on its own clock; this file has no knowledge
    // of it.
    const PipelinePayload payload = pipeline_fn();

    // ---- SHAPE AT RECEIPT — THE GATE (Ruling 6) ------------------------
    // The ONE moment the payload's shape is checked: per category, present,
    // and in its correct structural format. One boolean each, never
    // combined, each posted. Any N stops the load here: this is
    // pre-authored source data, identical on every call until a human
    // edits it, and it is load-bearing — a malformed table is never handed
    // onward. Shape is never re-checked downstream.

    // Roster: present, and every name non-empty.
    const bool wellness_check_payload_roster_present = !payload.roster.empty();
    if (!wellness_check_payload_roster_present) return;

    bool roster_ok = true;
    for (const auto& name : payload.roster) {
        if (name.empty()) { roster_ok = false; break; }
    }
    const bool wellness_check_payload_roster_format = roster_ok;
    if (!wellness_check_payload_roster_format) return;

    // Pool table: present, and every pool structurally well formed — names
    // non-empty, mask_count within the ceiling, triggers fitting in
    // 3 × mask_count bits, every agent's bits fitting in 2 + 3 × mask_count.
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

    // ---- CROSS-CHECK ---------------------------------------------------
    // The roster and the pool table agree with each other. Two booleans,
    // posted, gating nothing. This was PoolMatrix's last remaining job; it
    // lives here because this is the only moment both lists sit together.
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

    // ---- FIRE ----------------------------------------------------------
    // All four together, the moment the payload is in hand. For each: two
    // fresh boundary checks — presence, answered from outside the target,
    // and contract, against the target's CURRENT declaration. Both are
    // posted facts for Wellness and NOTHING else: this file reads neither,
    // decides nothing on either, and fires every wake regardless. These
    // are live runtime facts that can differ on the next call; firing
    // anyway costs at worst a glitch Wellness then fixes. Each wake is
    // handed its data as full copied blocks, verbatim — owned outright,
    // never a reference into this frame, never a translation. Nothing is
    // handed back from any of them.

    // Rules — pipeline name and roster. The roster alone is what Rules
    // needs: every agent gets a Directive, and a paired Arbiter is found by
    // name. Nothing from the pool table.
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

    // Temperature — pipeline name and roster.
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

    // ModelWeights — roster only.
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

    // The store — the pipeline name and the payload, verbatim, as one
    // atomic landing. Old pipeline down, new pipeline in, never a mix —
    // that swap is LiveRegistry's own job; landing correctly IS its acting.
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

    // ---- RETURN. IMMEDIATELY. ------------------------------------------
    // Everything is fired; nothing is waited on; nothing is retained;
    // nothing is reported. The caller was a messenger and its message is
    // delivered. Whether the load worked is Wellness's fact to hold.
}

} // namespace prime
