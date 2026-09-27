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

#include "dispatch.h"        // Contract, ModelFormat, ComputeTarget
#include "../prime_types.h"  // WeightRegion

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

    // Departments whose agents get a control at all. Every other agent is
    // deterministic: no control, no stored value, no entry.
    inline const char* const kDepartments[] = {
        "Adept", "Artist", "Artisan", "Aperture", "Architect",
    };

    // Architect is whitelisted by department but gated to this one member.
    // Every other Architect-family agent is deterministic.
    inline const char* const kArchitectMember = "Architect-Ingest";

    inline constexpr double kDefault = 0.7;

    // Bounds. Out of range is REFUSED, never clamped — a silently corrected
    // temperature is a silently different agent.
    inline constexpr double kMin = 0.0;
    inline constexpr double kMax = 2.0;

} // namespace temperature_policy

// ---------------------------------------------------------------------------
// One resident agent. No department field: read it off `name`.
// No directive_path field: Rules owns that convention and applies it to the
// name when it needs a path.
// ---------------------------------------------------------------------------
struct LiveAgent {
    std::string         id;
    std::string         name;
    std::string         gguf_path;       // normalised; empty for non-GGUF packages
    std::string         compute_target;  // as authored, resolved to enum on use
    Contract            contract = Contract::TextToText;
    ModelFormat         format   = ModelFormat::Unknown;
    ComputeTarget       target   = ComputeTarget::RDNA;
    const WeightRegion* weights  = nullptr;  // attached after mapping, not at load
};

// One resident team. A duplicate's roster names the SAME agents its parent
// names — no agent is ever duplicated by Split.
struct LiveTeam {
    std::string              id;
    std::string              name;
    std::string              parent;         // empty unless this IS a duplicate
    bool                     split_enabled = false;
    std::vector<std::string> roster;         // member agent NAMES, in order
};

// One split-enabled parent and its ordered runners. runners[0] == parent_name.
struct LiveSplitParent {
    std::string              parent_name;
    std::vector<std::string> runners;
};

// One held temperature, keyed by (team, agent) so the same agent sitting in
// two fusions naturally has two independent values. A controllable agent on
// no team is held with an empty `team`.
struct LiveTemperature {
    std::string team;
    std::string agent;
    double      value      = temperature_policy::kDefault;
    bool        is_default = true;
};

// What reconciliation changed at the most recent load. Non-blocking.
struct ReconcileReport {
    std::vector<std::pair<std::string, std::string>> dropped;
    std::vector<std::pair<std::string, std::string>> defaulted;
    bool config_unreadable      = false;
    bool temperatures_unreadable = false;
};

// Token position range, half-open [first, end). Whole-pool is expressed by
// ABSENCE of a range at the call site, never by a sentinel value.
struct TokenRange {
    uint64_t first = 0;
    uint64_t end   = 0;
};

// One live reveal. `key` is the prompt or turn id it is keyed to — the same
// primitive, different tag — or EMPTY for the unkeyed single-controlled mode
// (project blocks, system-written pools: no stamp, so no key to die with;
// held until the controller lowers it or a blanket sweep).
//
// `beneficiary` is empty for the ordinary mirrored fan-out (controller + its
// declared slaves, minus marked exceptions, resolved live at check time —
// never stored as a roster). Non-empty is the marked-exception path only: a
// mirror controller operating a slave's view on a pool excepted from the
// mirror (the split constituent's input — the release that IS split
// starting).
//
// `narrowed` absent == the whole pool under this reveal. Present == exactly
// these ranges are visible under it (an empty list is an imposed cover under
// the key). lower(range) merges in; raise(range) subtracts. The narrowing
// restricts this reveal for EVERYONE it benefits — identical view for
// slaves, narrowing included — and dies with the reveal.
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

    // THE ONE MOTION.
    //
    // TWO THINGS CROSS THIS BOUNDARY AND ONLY TWO: which pipeline, and which
    // agents are in it. Nothing else. No paths, no locations, no "where do I
    // find your own file" — those are not the caller's business and never
    // were.
    //
    //   WHAT it looks up is the variable (this pipeline, these agents).
    //   HOW and WHERE it looks are this object's own permanent knowledge.
    //
    // That distinction is why a background agent reaching in later does not
    // have to carry a path around with it just to ask a question. If a path
    // had to be supplied on every call, every caller would end up holding a
    // copy of the location logic — which is the same duplication this whole
    // registry exists to remove.
    //
    // Throws std::runtime_error on a malformed config — a config that cannot
    // be fully resolved does not produce a half-standing pipeline.
    ReconcileReport load(const std::string& pipeline_name,
                         const std::vector<std::string>& agent_names);

    void clear();

    // ---- identity ---------------------------------------------------------
    std::string pipeline_name() const;
    bool        loaded() const;

    // ---- agents -----------------------------------------------------------
    std::optional<LiveAgent> agent(const std::string& name) const;
    std::vector<LiveAgent>   agents() const;
    std::vector<std::string> agent_names() const;
    bool                     has_agent(const std::string& name) const;
    size_t                   agent_count() const;

    // Every distinct model path referenced by any agent, deduplicated — what
    // the pipeline-load path maps. Derived from the agents already held; not
    // a second stored list.
    std::vector<std::string> models_to_load() const;

    // Attach a mapped region to an agent. Called by the pipeline-load path
    // after mapping, because mapping needs the allocator and weight store
    // this registry deliberately does not own. False when the name is unknown.
    bool attach_weights(const std::string& agent_name, const WeightRegion* region);

    // ---- teams ------------------------------------------------------------
    std::optional<LiveTeam> team(const std::string& name) const;
    std::vector<LiveTeam>   teams() const;

    // Members of a team whose department is not Arbiter, in roster order.
    // The Arbiter rule is functional, never name-based: if EXACTLY ONE member
    // is Arbiter it is the veto seat and is excluded here; any other count
    // (zero, or two or more) means every member generates.
    std::vector<std::string> generating_members(const std::string& team_name) const;

    // The veto seat for a team, when it has exactly one. Empty otherwise.
    std::string veto_seat(const std::string& team_name) const;

    // ---- split parents ----------------------------------------------------
    std::vector<LiveSplitParent>   split_parents() const;
    std::optional<LiveSplitParent> split_parent(const std::string& parent_team) const;

    // ---- department (derived, never stored) --------------------------------
    static std::string department_of(const std::string& name);

    static bool is_temperature_controllable(const std::string& department,
                                            const std::string& agent_name);

    // ---- temperature -------------------------------------------------------
    // Every controllable (team, agent) pair currently held. This is what the
    // Temperature panel copies when it opens.
    std::vector<LiveTemperature> temperatures() const;

    std::optional<double> temperature_value(const std::string& team,
                                            const std::string& agent) const;
    std::optional<bool>   temperature_is_default(const std::string& team,
                                                 const std::string& agent) const;

    // The flat value a runner of a split parent uses. runner_name must be one
    // of that parent's own runners; the value is that parent's member at the
    // same position. Position arithmetic stays in here, never at a call site.
    std::optional<double> flat_temperature_for_runner(const std::string& parent_team,
                                                      const std::string& runner_name) const;

    // COMMIT. Replaces the held values with the panel's working copy and
    // writes them to disk in the same action. Any pair not present in
    // `edited` keeps whatever is currently held. Entries in `edited` that
    // are not currently held are ignored — the registry decides who exists,
    // never the panel.
    //
    // Refuses the WHOLE commit if any value is out of range: nothing is
    // partially applied and the operator is told which pair was wrong.
    // False also on a write failure — a failed save is a visible failure.
    bool commit_temperatures(const std::vector<LiveTemperature>& edited,
                             std::string& rejected_team_out,
                             std::string& rejected_agent_out);

    // ---- rebuttal switch ---------------------------------------------------
    // The ONE switch for which turn method is controlling — the key every
    // mechanic that depends on turn method works from. Flipped by whichever
    // turn file steps down at handover: turn.cpp on the way in (PENDING —
    // turn.cpp still owes its fix; until that lands NOTHING flicks this on),
    // the rebuttal side on the way out, as it hands control back. Flipped
    // nowhere else, ever.
    //
    // THE SWITCH TELLS THE FILES — files never request this from the switch.
    // A file that must react registers a signal below and is told ON CHANGE.
    // No signal means no change: recipients assume their last-told state
    // stands. A redundant set (same value) changes nothing and signals
    // nobody. The plain getter remains for work that runs at its own moment
    // and reads the fact then (the cleanup sweep's migration scope) —
    // reading is not reacting.
    void set_rebuttal_active(bool active);
    bool rebuttal_active() const;
    void on_rebuttal_switch(std::function<void(bool active)> signal);

    // ---- prompt links (standing memory) ------------------------------------
    // prompt id -> the pool ids linked to it, written in ONE motion at the
    // moment the Architect's prompt lands with its already-final pool list —
    // no partial state, no later append. Read wherever a prompt's reveal set
    // is needed; removed when the pool carrying the id arrives in shared
    // context, its cycle complete.
    void link_prompt(const std::string& prompt_id,
                     std::vector<std::string> linked_pool_ids);
    std::vector<std::string> linked_pools(const std::string& prompt_id) const;
    void unlink_prompt(const std::string& prompt_id);

    // ---- pool table (name, permissions, size — together) --------------------
    // The registry is where pool information lives: name, access permissions
    // and size, one entry, one place. The pool manipulation file reads the
    // size here DIRECTLY at every create — a direct lookup, never a figure
    // handed in or held elsewhere. Absent means not resident: the caller is
    // refused, loudly, never given a stand-in.
    //
    // FILLING this table is the absorption work deferred with pool_compiler's
    // removal (see DEFERRED — "LiveRegistry pool table"). These lookups are
    // the settled shape that work must arrive in: direct reads, keyed by pool
    // name, size and permissions living beside each other.
    std::optional<uint64_t> pool_size_bytes_per_token(const std::string& pool_name) const;
    void set_pool_size_bytes_per_token(const std::string& pool_name, uint64_t bytes);

    // ---- files (pool -> source file tag) — its own section entirely ---------
    // Which file a pool's content traces back to. Written and read ONLY by
    // the FileController; data here, logic there. One tag per pool; setting
    // replaces. Generic by design: a project source file, a rules file,
    // anything on disk a pool mirrors — one mechanism, no domain column.
    // NOT part of the pool table above (name/permissions/size) — its own
    // concern, in its own league.
    void set_pool_file_tag(const std::string& pool_name, const std::string& file);
    std::optional<std::string> pool_file_tag(const std::string& pool_name) const;
    std::vector<std::string> pools_with_file_tag(const std::string& file) const;
    void clear_pool_file_tag(const std::string& pool_name);
    void clear_file_tags(const std::string& file);

    // ==== MASKING — state co-located here; masking.cpp is the logic ====

    // ==== load-declared masking facts (written once from the pipeline
    // ==== declaration at load; read forever) ==============================

    // Fixed pool (Rules): masking set at load, no holder exists, no lever to
    // attach a call to. Cascade-exempt: the pool never joins id-keyed
    // reveals or close sweeps — the ONLY masking immunity, independent of
    // what the pool is stamped with.
    void set_mask_pool_facts(const std::string& pool_id,
                             bool fixed_no_holder,
                             bool cascade_exempt);
    std::optional<bool> mask_fixed_no_holder(const std::string& pool_id) const;
    std::optional<bool> mask_cascade_exempt(const std::string& pool_id) const;

    // Agent level: prompt-level or turn-level, declared per pipeline at
    // load, never changing. DEFAULT IS PROMPT-LEVEL — single active prompt
    // with cascading linked context — unless the pipeline explicitly
    // declares the agent turn-level; an agent added without this
    // declaration lands on the ordinary behaviour, never silently on the
    // narrow one. Turn-level agents ignore prompt id entirely (one active
    // turn, cascade purely by the turn id in the pool, link table never
    // consulted, unlimited prompt ids visible under the one open turn);
    // prompt-level agents ignore turn id entirely. Mirror slaves are
    // declared at their controller's level and stay exactly with the
    // parent.
    void set_mask_agent_turn_level(const std::string& agent, bool turn_level);
    bool mask_agent_turn_level(const std::string& agent) const;

    // Standing-open rows: the declared unmasked-by-default facts (the
    // AgentRule rows whose visibility tag is empty). Everything not
    // standing-open is covered by default — the load-bearing stateless
    // illusion.
    void set_mask_standing_open(const std::string& pool_id,
                                const std::string& agent, bool open);
    bool mask_standing_open(const std::string& pool_id,
                            const std::string& agent) const;

    // Fixed segment visibility (Rules): per agent, the visible ranges,
    // verbatim from the segment writer at the moment the segments are
    // written (it is the one thing that knows the extents — see
    // FLAGS_Masking.md). Never changes after load.
    void set_mask_fixed_visible(const std::string& pool_id,
                                const std::string& agent,
                                std::vector<TokenRange> visible);
    std::vector<TokenRange> mask_fixed_visible(const std::string& pool_id,
                                               const std::string& agent) const;

    // Mirror membership, fixed at pipeline load, split agents included —
    // nothing decided at runtime. Slaves are PURE slaves: identical view,
    // narrowing included, no levers. Exceptions are marked per (slave,
    // pool): on an excepted pool the slave does not auto-mirror; its view
    // there is operated by its controller explicitly (the split input pool).
    void set_mask_mirror(const std::string& controller,
                         std::vector<std::string> slaves);
    std::vector<std::string> mask_mirror_slaves(const std::string& controller) const;
    bool mask_is_pure_slave(const std::string& agent) const;
    std::string mask_controller_of_slave(const std::string& slave) const;
    void set_mask_mirror_exception(const std::string& slave,
                                   const std::string& pool_id);
    bool mask_mirror_excepted(const std::string& slave,
                              const std::string& pool_id) const;

    // ==== live mask state (written by Masking; current state only) =========

    // The open ids: id -> controller. This IS the lifted-mask fact — the
    // prompt an agent is working on is the one open here under its name,
    // looked up, never duplicated as an agent-side field. Several ids open
    // at once is normal (one per agent for prompts by construction; turns:
    // as many as there are openers — rebuttal is unlimited).
    void mask_open_id(const std::string& id, const std::string& controller);
    void mask_close_id(const std::string& id);
    std::optional<std::string> mask_id_controller(const std::string& id) const;
    std::vector<std::string> mask_open_ids_of(const std::string& controller) const;

    // Per-pool reveal records: the unkeyed single-controlled reveals and the
    // per-key narrowings. Replace-by-identity: one record per (pool, key,
    // controller, beneficiary).
    void mask_put_reveal(const std::string& pool_id, MaskReveal reveal);
    void mask_drop_reveal(const std::string& pool_id,
                          const std::string& key,
                          const std::string& controller,
                          const std::string& beneficiary);
    std::vector<MaskReveal> mask_reveals(const std::string& pool_id) const;

    // Blanket cover (Abort): everything in the pipeline resolves covered
    // while set. Cleared by restore_load_defaults, which also sweeps all
    // live state — the declared baseline is what remains.
    void mask_set_all_covered(bool covered);
    bool mask_all_covered() const;
    void mask_sweep_live();

    // ==== the live stamp view — settled-shape contracts ====================
    // Read DIRECTLY off the live pool identity in unified memory — never a
    // copy, never a cache that could drift. Filling these is the pool-table
    // absorption work already deferred (the exact pattern
    // pool_size_bytes_per_token established); these signatures are the shape
    // it must arrive in. Absent means empty/nullopt, never a stand-in.
    // Masking's presence check (prompt id if the pool has one, else turn id)
    // reads these and nothing else.
    std::vector<std::string> pool_prompt_ids_live(const std::string& pool_id) const;
    std::optional<std::string> pool_turn_id_live(const std::string& pool_id) const;
    std::vector<std::string> pools_with_prompt_live(const std::string& id) const;
    std::vector<std::string> pools_with_turn_live(const std::string& id) const;
    // Access rows (structural, per pool per agent) land in the registry with
    // the same absorption; masking's gate reads this contract:
    bool pool_access_row_exists(const std::string& pool_id,
                                const std::string& agent) const;
    // (linked_pools(prompt_id) already exists — the standing prompt-link
    //  table from the rebuttal session's registry. Not redeclared here.)

private:
    mutable std::mutex mutex_;

    std::string pipeline_name_;

    std::vector<LiveAgent>       agents_;
    std::vector<LiveTeam>        teams_;
    std::vector<LiveSplitParent> split_parents_;
    std::vector<LiveTemperature> temperatures_;

    // Rebuttal switch, standing prompt links, and the pool table. Own locks so
    // nothing here touches the load-path lock. The signal list is wired at
    // construction time and read on every genuine flip.
    std::atomic<bool>  rebuttal_active_{false};
    mutable std::mutex rebuttal_signal_mutex_;
    std::vector<std::function<void(bool)>> rebuttal_signals_;
    mutable std::mutex links_mutex_;
    std::unordered_map<std::string, std::vector<std::string>> prompt_links_;
    mutable std::mutex pool_table_mutex_;
    std::unordered_map<std::string, uint64_t> pool_sizes_;

    // Files — its own store, own lock. pool name -> file tag. ONE map; the
    // reverse question (file -> pools) is a scan of this one store, never a
    // second map kept in step with it.
    mutable std::mutex files_mutex_;
    std::unordered_map<std::string, std::string> pool_file_tags_;

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

// (Includes needed in live_registry.h for this block: <algorithm>, <map>,
//  <set> — <mutex>, <optional>, <string>, <utility>, <vector> are already
//  there.)
};

} // namespace prime
