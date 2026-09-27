// team_config.h — Prime Architect frontend team authoring
//
// Owns the TEAM side of config.json authoring, and the Split toggle.
//
// DEPARTMENT SCOPING
//   A team created while department D is open belongs to D. Its eligible member
//   pool is: every solo agent whose department is D, PLUS any agent whose
//   department is "Arbiter". Nothing else, ever.
//
//   That rule is what makes member lookup direct everywhere downstream. A member
//   can only be in the team's own department bucket or in Arbiter, so resolving
//   a roster is two bucket reads. Nothing is built anywhere to make a wider
//   search fast, because a wider search never happens.
//
// THE ARBITER CAP
//   A team may hold at most ONE Arbiter — UNLESS the team's own department is
//   "Arbiter", in which case the cap does not apply (a wholly-Arbiter team is an
//   ordinary generating team; there is no outside veto seat to bound).
//
// THE SPLIT TOGGLE — WHAT IT ACTUALLY DOES
//
//   Turning it on creates N NEW FUSIONS, where N is the number of GENERATING
//   agents on the parent roster (every member whose department is not Arbiter).
//   The count comes out of the roster and is never written down as a number.
//
//   Each new fusion:
//     - is functional and unique in its own right
//     - is a complete, uneditable slave to its parent
//     - uses the EXACT SAME AGENTS as its parent
//
//   That last point is the one that matters most here. The agents are NOT
//   duplicated. The same agent entries simply belong to more than one fusion at
//   once. Nothing is created, copied, or loaded a second time — this layer adds
//   teams and only teams.
//
//   Each duplicate is named "<parent>-Split" (k=1), "<parent>-Split2" (k=2), and
//   so on, and carries the parent's FULL roster including any Arbiter. The
//   generating count decides how many duplicates exist; it does not decide
//   what is inside one.
//
//   Turning the toggle off removes the duplicate teams. Since nothing else was
//   ever created, there is nothing else to clean up.
//
//   Keeping them right is one call: resync_splits(). It is idempotent — running
//   it when nothing changed changes nothing — so callers never have to work out
//   whether a given edit needs it. Every mutating path here calls it.
//
// PARENT — STATED, NOT PARSED
//   A duplicate carries the name of the team it mirrors in `parent`. That field
//   IS the relationship. The "-Split" naming is a readable consequence of it,
//   never the source. Nothing anywhere strips a suffix off a name to find a
//   parent.
//
// DEPARTMENT IS STATED ONCE
//   Carried here because the working set is flat. On disk it is not written on
//   the entry — the bucket states it, once. See config_writer.h.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "agent_config.h"

namespace prime {

struct TeamEntry {
    std::string id;              // immutable, e.g. "team_f935e3"
    std::string name;            // addressable key; unique across the namespace
    std::string department;      // working-set only; the bucket states it on disk
    std::vector<std::string> roster;  // member agent NAMES
    std::string kind = entry_kind::kTeam;

    // The operator's Split toggle. Meaningful only on a parent; a duplicate
    // never carries it.
    bool split_enabled = false;

    // Empty for an operator-authored team. For a duplicate, the NAME of the team
    // it mirrors. Read directly; never derived from `name`.
    std::string parent;

    bool is_split() const { return !parent.empty(); }
};

// Which departments may offer the Split toggle: the temperature-controllable
// set, minus Architect.
namespace split_policy {
    inline const char* const kDepartments[] = {
        "Adept", "Artist", "Artisan", "Aperture",
    };

    bool department_allows_split(const std::string& department);

    // "<base>-Split" for k==1, then "<base>-Split2", "<base>-Split3", ...
    std::string split_name_for(const std::string& base, int k);
}

// Minimal view of a solo agent this layer needs for eligibility decisions.
struct MemberCandidate {
    std::string name;
    std::string department;
};

// Resolves a member NAME to its department. Needed because a loaded roster
// holds names only, and the Arbiter cap has to count existing Arbiters.
using DepartmentResolver = std::function<std::string(const std::string& name)>;

enum class AddMemberResult {
    Added,
    NotEligible,      // neither in the team's department nor Arbiter
    ArbiterCapped,    // already holds an Arbiter and department != Arbiter
    AlreadyPresent,
    IsSplit,          // target is a duplicate and cannot be hand-edited
    UnknownTeam
};

enum class SplitToggleResult {
    Applied,
    DepartmentNotEligible,
    TargetIsSplit,
    UnknownTeam
};

class TeamConfig {
public:
    void set_teams(std::vector<TeamEntry> teams);

    const std::vector<TeamEntry>& teams() const { return teams_; }

    // Teams in a department, insertion order. Includes duplicates.
    std::vector<TeamEntry> in_department(const std::string& department) const;

    // Operator-authored teams only — duplicates excluded. What the panel lists
    // as editable rows.
    std::vector<TeamEntry> authorable_in_department(const std::string& department) const;

    std::optional<TeamEntry> by_name(const std::string& name) const;

    // Duplicates generated from a named parent, in generation order. Index k in
    // this list is duplicate k+1, which is the order the Split labels follow.
    std::vector<TeamEntry> splits_of(const std::string& parent_name) const;

    TeamEntry create_team(const std::string& name, const std::string& department);

    AddMemberResult add_member(const std::string& team_id,
                               const MemberCandidate& candidate,
                               const DepartmentResolver& resolve);

    bool remove_member(const std::string& team_id,
                       const std::string& member_name,
                       const DepartmentResolver& resolve);

    // On: create the N duplicate fusions. Off: remove them. Nothing else is
    // created or destroyed either way — no agent was ever involved.
    SplitToggleResult set_split_enabled(const std::string& team_id,
                                        bool enabled,
                                        const DepartmentResolver& resolve);

    // Delete a team; its duplicates go with it. Refuses a duplicate — those go
    // when the parent's toggle goes off.
    bool delete_team(const std::string& team_id);

    // Cascade an agent rename into every roster, parents and duplicates alike
    // (they hold the same names, so both need it). Returns references rewritten.
    int apply_rename(const std::string& old_name, const std::string& new_name);

    // Bring every duplicate back into exact agreement with its parent.
    // Idempotent. Every mutating call above already runs it.
    void resync_splits(const DepartmentResolver& resolve);

    AddMemberResult check_eligibility(const TeamEntry& team,
                                      const MemberCandidate& candidate,
                                      const DepartmentResolver& resolve) const;

    // Members whose department is not Arbiter, in roster order. N is counted
    // from this, and the Split labels follow this order.
    static std::vector<std::string> generating_members(
        const std::vector<std::string>& roster,
        const DepartmentResolver& resolve);

private:
    TeamEntry* find(const std::string& team_id);
    const TeamEntry* find(const std::string& team_id) const;

    void rebuild_splits_for(const TeamEntry& parent,
                            const DepartmentResolver& resolve);
    void drop_splits_for(const std::string& parent_name);

    std::vector<TeamEntry> teams_;
};

} // namespace prime
