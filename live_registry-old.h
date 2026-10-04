// live_registry.h — Prime Engine live registry
//
// REPLACES slot_registry.h, config_reader.h and temperature_store.h ENTIRELY.
// This is the one resident place that answers "what is currently true about
// this pipeline", for anything that needs to know.
//
// WHY THIS EXISTS
//   Those three were the same job split into three things that could disagree:
//   a config reader you constructed and then asked questions of, a slot
//   registry holding agent identity and nothing else, and a temperature store
//   the UI kept privately. There is exactly one pipeline loaded at a time and
//   exactly one set of facts about it. One place holds them.
//
// NO ASKING, NO HANDOFF
//   Reading this is a direct reach into resident memory — same process, same
//   address space. No channel, no request, no object standing in the middle
//   waiting to be queried. Filling it is ONE motion at pipeline load: the
//   config file is read once, everything in it is resolved in that single
//   pass, and the result is written straight in. Nothing here is re-derived
//   afterwards from something else.
//
// THREE FLAT SECTIONS — DISK AND MEMORY ARE THE SAME SHAPE
//   Agents, Teams, SplitParents. No department tree. The file on disk holds
//   these same three sections in this same order, so loading is filling this
//   layout directly rather than translating into it.
//
// DEPARTMENT IS NEVER STORED
//   An agent's department is the leading word of its own name, always, by
//   naming convention (Architect-Ingest -> Architect, Analyst_Coder ->
//   Analyst, Aether -> Aether). It is read off the name at the point it is
//   needed. Storing it as well would be a second copy of one fact.
//
// SPLIT PARENTS
//   One entry per split-enabled team. `runners` is the ordered list of names
//   that borrow this team's members' temperatures — runners[0] is ALWAYS the
//   parent team itself (never omitted), runners[k] for k >= 1 is the k'th
//   generated duplicate, in the exact order the system names them when it
//   creates them.
//
//   Which member's value a given runner borrows is position k against the
//   parent team's own roster. Nothing computed, nothing guessed, nothing
//   stored twice — two lists that both already exist, read positionally.
//
//   The composition never changes. A split team is the SAME fusion of the
//   SAME members; what differs is that each runner is forced flat onto one
//   borrowed value instead of each member using its own.
//
// TEMPERATURE IS HELD HERE, NOT IN THE UI
//   The Temperature panel is a user of this data, not its owner. Opening the
//   panel takes a working copy that lives only as long as the panel is open;
//   editing changes that copy and nothing else; closing without saving
//   discards it and what generation runs on was never touched. Save is the
//   one moment values come back here and go to disk, together.
//
//   is_default stays true until the operator has actually opened the panel —
//   whether or not any value changed. Opening IS the acknowledgement. That is
//   what lets an unnoticed default survive a restart without becoming a
//   silent surprise, and lets an operator who is happy with 0.7 clear it by
//   opening the panel and closing it again.
//
// RECONCILIATION HAPPENS IN THE SAME LOAD, NEVER AS A SEPARATE PASS
//   A (team, agent) entry in the temperature file that is not in this
//   session's resolved agents is DROPPED outright — no count, no grace. A
//   controllable pair that exists this session but has no entry in the file
//   is added at 0.7, marked default, and reported. The report is
//   NON-BLOCKING: the pipeline is fully usable, the operator is simply told.
//
// FILE FORMAT — LENGTH-PREFIXED, NO PARSER
//   Both files this owns are written and read directly. A fixed-size value (a
//   count, a number, a flag) is written as itself. Anything not fixed size (a
//   name, a path) is preceded by its byte length. No JSON, no brace scanning,
//   no escaping, nothing standing between the bytes and this layout.
//
//   Every read is bounds-checked against what is actually left in the buffer.
//   A file that runs out mid-record is a hard failure, not a partial load —
//   half a pipeline standing up is worse than none.
//
// WHAT THIS DOES NOT DO
//   It does not map weights (it has no allocator and should not) and it does
//   not resolve directive paths (that convention belongs to Rules, which
//   reads names from here and applies it itself). Mapped weight pointers are
//   attached after mapping, by the pipeline-load path, via attach_weights().

#pragma once

#include "dispatch.h"
#include "../prime_types.h"

#include <atomic>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace prime {

namespace temperature_policy {

    inline const char* const kDepartments[] = {
        "Adept", "Artist", "Artisan", "Aperture", "Architect",
    };

    inline const char* const kArchitectMember = "Architect-Ingest";

    inline constexpr double kDefault = 0.7;

    inline constexpr double kMin = 0.0;
    inline constexpr double kMax = 2.0;

}

struct LiveAgent {
    std::string         id;
    std::string         name;
    std::string         gguf_path;
    std::string         compute_target;
    Contract            contract = Contract::TextToText;
    ModelFormat         format   = ModelFormat::Unknown;
    ComputeTarget       target   = ComputeTarget::RDNA;
    const WeightRegion* weights  = nullptr;
};

struct LiveTeam {
    std::string              id;
    std::string              name;
    std::string              parent;
    bool                     split_enabled = false;
    std::vector<std::string> roster;
};

struct LiveSplitParent {
    std::string              parent_name;
    std::vector<std::string> runners;
};

struct LiveTemperature {
    std::string team;
    std::string agent;
    double      value      = temperature_policy::kDefault;
    bool        is_default = true;
};

struct ReconcileReport {
    std::vector<std::pair<std::string, std::string>> dropped;
    std::vector<std::pair<std::string, std::string>> defaulted;
    bool config_unreadable      = false;
    bool temperatures_unreadable = false;
};

struct TokenRange {
    uint64_t first = 0;
    uint64_t end   = 0;
};

struct MaskReveal {
    std::string                            key;
    std::string                            controller;
    std::string                            beneficiary;
    std::optional<std::vector<TokenRange>> narrowed;
};

class LiveRegistry {
public:
    LiveRegistry() = default;
    LiveRegistry(const LiveRegistry&)            = delete;
    LiveRegistry& operator=(const LiveRegistry&) = delete;

    ReconcileReport load(const std::string& pipeline_name,
                         const std::vector<std::string>& agent_names);

    void clear();

    std::string pipeline_name() const;
    bool        loaded() const;

    std::optional<LiveAgent> agent(const std::string& name) const;
    std::vector<LiveAgent>   agents() const;
    std::vector<std::string> agent_names() const;
    bool                     has_agent(const std::string& name) const;
    size_t                   agent_count() const;

    std::vector<std::string> models_to_load() const;

    bool attach_weights(const std::string& agent_name, const WeightRegion* region);

    std::optional<LiveTeam> team(const std::string& name) const;
    std::vector<LiveTeam>   teams() const;

    std::vector<std::string> generating_members(const std::string& team_name) const;

    std::string veto_seat(const std::string& team_name) const;

    std::vector<LiveSplitParent>   split_parents() const;
    std::optional<LiveSplitParent> split_parent(const std::string& parent_team) const;

    static std::string department_of(const std::string& name);

    static bool is_temperature_controllable(const std::string& department,
                                            const std::string& agent_name);

    std::vector<LiveTemperature> temperatures() const;

    std::optional<double> temperature_value(const std::string& team,
                                            const std::string& agent) const;
    std::optional<bool>   temperature_is_default(const std::string& team,
                                                 const std::string& agent) const;

    std::optional<double> flat_temperature_for_runner(const std::string& parent_team,
                                                      const std::string& runner_name) const;

    bool commit_temperatures(const std::vector<LiveTemperature>& edited,
                             std::string& rejected_team_out,
                             std::string& rejected_agent_out);

    void set_rebuttal_active(bool active);
    bool rebuttal_active() const;
    void on_rebuttal_switch(std::function<void(bool active)> signal);

    void link_prompt(const std::string& prompt_id,
                     std::vector<std::string> linked_pool_ids);
    std::vector<std::string> linked_pools(const std::string& prompt_id) const;
    void unlink_prompt(const std::string& prompt_id);

    std::optional<uint64_t> pool_size_bytes_per_token(const std::string& pool_name) const;
    void set_pool_size_bytes_per_token(const std::string& pool_name, uint64_t bytes);

    void set_mask_pool_facts(const std::string& pool_id,
                             bool fixed_no_holder,
                             bool cascade_exempt);
    std::optional<bool> mask_fixed_no_holder(const std::string& pool_id) const;
    std::optional<bool> mask_cascade_exempt(const std::string& pool_id) const;

    void set_mask_agent_turn_level(const std::string& agent, bool turn_level);
    bool mask_agent_turn_level(const std::string& agent) const;

    void set_mask_standing_open(const std::string& pool_id,
                                const std::string& agent, bool open);
    bool mask_standing_open(const std::string& pool_id,
                            const std::string& agent) const;

    void set_mask_fixed_visible(const std::string& pool_id,
                                const std::string& agent,
                                std::vector<TokenRange> visible);
    std::vector<TokenRange> mask_fixed_visible(const std::string& pool_id,
                                               const std::string& agent) const;

    void set_mask_mirror(const std::string& controller,
                         std::vector<std::string> slaves);
    std::vector<std::string> mask_mirror_slaves(const std::string& controller) const;
    bool mask_is_pure_slave(const std::string& agent) const;
    std::string mask_controller_of_slave(const std::string& slave) const;
    void set_mask_mirror_exception(const std::string& slave,
                                   const std::string& pool_id);
    bool mask_mirror_excepted(const std::string& slave,
                              const std::string& pool_id) const;

    void mask_open_id(const std::string& id, const std::string& controller);
    void mask_close_id(const std::string& id);
    std::optional<std::string> mask_id_controller(const std::string& id) const;
    std::vector<std::string> mask_open_ids_of(const std::string& controller) const;

    void mask_put_reveal(const std::string& pool_id, MaskReveal reveal);
    void mask_drop_reveal(const std::string& pool_id,
                          const std::string& key,
                          const std::string& controller,
                          const std::string& beneficiary);
    std::vector<MaskReveal> mask_reveals(const std::string& pool_id) const;

    void mask_set_all_covered(bool covered);
    bool mask_all_covered() const;
    void mask_sweep_live();

    std::vector<std::string> pool_prompt_ids_live(const std::string& pool_id) const;
    std::optional<std::string> pool_turn_id_live(const std::string& pool_id) const;
    std::vector<std::string> pools_with_prompt_live(const std::string& id) const;
    std::vector<std::string> pools_with_turn_live(const std::string& id) const;

    bool pool_access_row_exists(const std::string& pool_id,
                                const std::string& agent) const;

private:
    mutable std::mutex mutex_;

    std::string pipeline_name_;

    std::vector<LiveAgent>       agents_;
    std::vector<LiveTeam>        teams_;
    std::vector<LiveSplitParent> split_parents_;
    std::vector<LiveTemperature> temperatures_;

    std::atomic<bool>  rebuttal_active_{false};
    mutable std::mutex rebuttal_signal_mutex_;
    std::vector<std::function<void(bool)>> rebuttal_signals_;
    mutable std::mutex links_mutex_;
    std::unordered_map<std::string, std::vector<std::string>> prompt_links_;
    mutable std::mutex pool_table_mutex_;
    std::unordered_map<std::string, uint64_t> pool_sizes_;

    mutable std::mutex mask_mutex_;

    struct PoolMaskFacts {
        bool fixed_no_holder = false;
        bool cascade_exempt  = false;
        std::vector<std::string> standing_open;
        std::map<std::string, std::vector<TokenRange>> fixed_visible;
    };
    std::map<std::string, PoolMaskFacts>                 mask_facts_;
    std::set<std::string>                                mask_turn_level_agents_;
    std::map<std::string, std::vector<std::string>>      mask_mirrors_;
    std::set<std::pair<std::string, std::string>>        mask_mirror_exceptions_;
    std::map<std::string, std::string>                   mask_open_ids_;
    std::map<std::string, std::vector<MaskReveal>>       mask_reveals_;
    bool                                                 mask_all_covered_ = false;

};

}
