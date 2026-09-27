// agent_config.h — Prime Architect frontend agent authoring
//
// Owns the SOLO-AGENT side of config.json authoring: the in-memory set of agent
// entries the operator builds in the settings panel, and the rules for what a
// valid entry is. This is config-TIME state — what will be stamped into the
// config — as distinct from agent_roster.h, which is RUNTIME state (what is
// currently resident, with live temperatures). Different lifecycle, different
// data, deliberately not sharing the "roster" vocabulary.
//
// SINGLE RESPONSIBILITY
//   Create, edit, and delete solo agent entries. Assign each a department, a
//   model (by discovered load_path), and a compute target. Hand the resulting
//   entries to the serializer. It does not render, does not walk the disk, does
//   not own teams, and does not decide when a save happens.
//
// MODEL VISIBILITY (mirrors the established selection rule)
//   Specialised modality departments (Aperture, Accord, Artist, Artisan,
//   Announcer — vision/asr/tts/image/video) may only be assigned models from
//   their OWN department folder. Text/reasoning departments may be assigned
//   models from their own folder PLUS every other text department's folder — a
//   deliberate cross-department pool, with each candidate carrying its origin
//   department so a reasoning-vs-logic specialist is identifiable at a glance.
//   The specialised set is a hardcoded modality list by design; it is extended
//   by explicit instruction when a genuinely new modality department appears,
//   not auto-derived from the folder.
//
// COMPUTE TARGET
//   An explicit per-agent field ("rdna" / "xdna" / "hybrid"), chosen by the
//   operator. It is NOT inferred from the model file — there is no file-type
//   lookup. Written under config_data.compute_target, the key the engine's
//   config reader consumes.
//
// NAME IS THE IDENTITY KEY
//   Agent name is the addressable key used everywhere downstream (config
//   resolution, prompts, rules, team rosters). Renaming an agent is therefore
//   not a local relabel: it must cascade to every team roster that references
//   the old name, atomically, or the engine's config reader hard-errors on an
//   unresolved member and refuses the whole config. The cascade is the caller's
//   contract (see rename_agent), coordinated with the team-authoring layer.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace prime {

struct AgentEntry {
    std::string id;              // immutable, e.g. "manual_4e91d9"
    std::string name;            // addressable key; editable, cascades on change
    std::string department;      // authoritative role; the folder it belongs to
    std::string kind = "agent";  // "agent" or "team" — structural kind, the sole
                                 // discriminator on disk and in the shared
                                 // entries_ vector. A solo agent entry is always
                                 // "agent". Replaces the former type + is_team.
    std::string mapped_path;     // discovered model load_path
    std::string compute_target;  // "rdna" | "xdna" | "hybrid"; operator-chosen
};

// A model offered to an agent for assignment, carrying its origin department so
// a cross-department text candidate shows where it actually came from.
struct ModelChoice {
    std::string name;
    std::string load_path;
    std::string origin_department;
};

class AgentConfig {
public:
    // Replace the working set (e.g. loaded from an existing config on panel open).
    void set_entries(std::vector<AgentEntry> entries);

    const std::vector<AgentEntry>& entries() const { return entries_; }

    // Solo agents in one department, in insertion order.
    std::vector<AgentEntry> in_department(const std::string& department) const;

    // Create a new solo agent in a department. id is minted immutable. name must
    // be unique across all addressable names; uniqueness enforcement is the
    // caller's contract against the combined agent+team namespace. Returns the
    // created entry.
    AgentEntry create_agent(const std::string& name,
                            const std::string& department,
                            const std::string& mapped_path,
                            const std::string& compute_target);

    // Edit assignable fields of an existing agent by id. Does NOT rename — name
    // changes go through rename_agent so the cascade contract is explicit.
    bool edit_agent(const std::string& id,
                    const std::string& mapped_path,
                    const std::string& compute_target);

    // Rename an agent by id and report BOTH names so the caller can cascade the
    // change into every team roster referencing the old name. Returns nullopt if
    // the id is unknown or the new name collides. On success the agent's name is
    // updated in place; propagating {old_name -> new_name} into team rosters is
    // the caller's responsibility, performed atomically with this call.
    struct RenameResult { std::string old_name; std::string new_name; };
    std::optional<RenameResult> rename_agent(const std::string& id,
                                             const std::string& new_name);

    bool delete_agent(const std::string& id);

    // The models an agent in `department` may be assigned, applying the
    // specialised-vs-text visibility rule. `discovered` is the flat loadable set
    // from ModelDiscovery (Malformed already excluded upstream, or ignored here).
    std::vector<ModelChoice>
    assignable_models(const std::string& department,
                      const std::vector<struct DiscoveredModel>& discovered) const;

    // True if `department` is a specialised (single-folder) modality department.
    static bool is_specialised_department(const std::string& department);

private:
    std::vector<AgentEntry> entries_;
};

} // namespace prime
