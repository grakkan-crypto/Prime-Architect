// team_config.cpp — Prime Architect frontend team authoring implementation

#include "team_config.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_set>

namespace prime {

namespace {

constexpr const char* kArbiter = "Arbiter";

std::string mint_id(const std::string& prefix) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t v = dist(rng);
    static const char* hex = "0123456789abcdef";
    std::string suffix(6, '0');
    for (int i = 0; i < 6; ++i) {
        suffix[i] = hex[v & 0xF];
        v >>= 4;
    }
    return prefix + "_" + suffix;
}

} // namespace

namespace split_policy {

bool department_allows_split(const std::string& department) {
    for (const char* d : kDepartments)
        if (department == d) return true;
    return false;
}

std::string split_name_for(const std::string& base, int k) {
    if (k <= 1) return base + "-Split";
    return base + "-Split" + std::to_string(k);
}

} // namespace split_policy

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
void TeamConfig::set_teams(std::vector<TeamEntry> teams) {
    teams_ = std::move(teams);
}

TeamEntry* TeamConfig::find(const std::string& team_id) {
    for (auto& t : teams_)
        if (t.id == team_id) return &t;
    return nullptr;
}

const TeamEntry* TeamConfig::find(const std::string& team_id) const {
    for (const auto& t : teams_)
        if (t.id == team_id) return &t;
    return nullptr;
}

std::optional<TeamEntry> TeamConfig::by_name(const std::string& name) const {
    for (const auto& t : teams_)
        if (t.name == name) return t;
    return std::nullopt;
}

std::vector<TeamEntry>
TeamConfig::in_department(const std::string& department) const {
    std::vector<TeamEntry> out;
    for (const auto& t : teams_)
        if (t.department == department) out.push_back(t);
    return out;
}

std::vector<TeamEntry>
TeamConfig::authorable_in_department(const std::string& department) const {
    std::vector<TeamEntry> out;
    for (const auto& t : teams_)
        if (t.department == department && !t.is_split()) out.push_back(t);
    return out;
}

std::vector<TeamEntry> TeamConfig::splits_of(const std::string& parent_name) const {
    std::vector<TeamEntry> out;
    for (const auto& t : teams_)
        if (t.parent == parent_name) out.push_back(t);
    return out;
}

std::vector<std::string>
TeamConfig::generating_members(const std::vector<std::string>& roster,
                               const DepartmentResolver& resolve) {
    std::vector<std::string> out;
    for (const auto& name : roster)
        if (!resolve || resolve(name) != kArbiter)
            out.push_back(name);
    return out;
}

// ---------------------------------------------------------------------------
// Authoring
// ---------------------------------------------------------------------------
TeamEntry TeamConfig::create_team(const std::string& name,
                                  const std::string& department) {
    TeamEntry t;
    t.id            = mint_id("team");
    t.name          = name;
    t.department    = department;
    t.kind          = entry_kind::kTeam;
    t.split_enabled = false;
    teams_.push_back(t);
    return t;
}

AddMemberResult
TeamConfig::check_eligibility(const TeamEntry& team,
                              const MemberCandidate& candidate,
                              const DepartmentResolver& resolve) const {
    if (team.is_split()) return AddMemberResult::IsSplit;

    if (std::find(team.roster.begin(), team.roster.end(), candidate.name)
        != team.roster.end())
        return AddMemberResult::AlreadyPresent;

    // The team's own department, plus Arbiter. Nothing else.
    const bool in_department = (candidate.department == team.department);
    const bool is_arbiter    = (candidate.department == kArbiter);
    if (!in_department && !is_arbiter)
        return AddMemberResult::NotEligible;

    if (is_arbiter && team.department != kArbiter) {
        int existing = 0;
        for (const auto& member : team.roster)
            if (resolve && resolve(member) == kArbiter) ++existing;
        if (existing >= 1) return AddMemberResult::ArbiterCapped;
    }

    return AddMemberResult::Added;
}

AddMemberResult
TeamConfig::add_member(const std::string& team_id,
                       const MemberCandidate& candidate,
                       const DepartmentResolver& resolve) {
    TeamEntry* t = find(team_id);
    if (!t) return AddMemberResult::UnknownTeam;

    const AddMemberResult verdict = check_eligibility(*t, candidate, resolve);
    if (verdict != AddMemberResult::Added) return verdict;

    t->roster.push_back(candidate.name);

    // A membership change changes N. The duplicates follow immediately, so the
    // roster and the fusions derived from it are never two states a caller has
    // to remember to reconcile.
    resync_splits(resolve);
    return verdict;
}

bool TeamConfig::remove_member(const std::string& team_id,
                               const std::string& member_name,
                               const DepartmentResolver& resolve) {
    TeamEntry* t = find(team_id);
    if (!t || t->is_split()) return false;

    auto it = std::find(t->roster.begin(), t->roster.end(), member_name);
    if (it == t->roster.end()) return false;
    t->roster.erase(it);

    resync_splits(resolve);
    return true;
}

SplitToggleResult
TeamConfig::set_split_enabled(const std::string& team_id,
                              bool enabled,
                              const DepartmentResolver& resolve) {
    TeamEntry* t = find(team_id);
    if (!t) return SplitToggleResult::UnknownTeam;
    if (t->is_split()) return SplitToggleResult::TargetIsSplit;
    if (!split_policy::department_allows_split(t->department))
        return SplitToggleResult::DepartmentNotEligible;

    t->split_enabled = enabled;
    resync_splits(resolve);
    return SplitToggleResult::Applied;
}

bool TeamConfig::delete_team(const std::string& team_id) {
    const TeamEntry* t = find(team_id);
    if (!t || t->is_split()) return false;

    const std::string name = t->name;
    drop_splits_for(name);

    const auto before = teams_.size();
    teams_.erase(
        std::remove_if(teams_.begin(), teams_.end(),
                       [&](const TeamEntry& e) { return e.id == team_id; }),
        teams_.end());
    return teams_.size() != before;
}

int TeamConfig::apply_rename(const std::string& old_name,
                             const std::string& new_name) {
    // Duplicates hold the same member names as their parent, so both are
    // rewritten by the same pass. There is no separate set of generated agent
    // names to repoint, because none were ever created.
    int rewritten = 0;
    for (auto& t : teams_)
        for (auto& member : t.roster)
            if (member == old_name) {
                member = new_name;
                ++rewritten;
            }
    return rewritten;
}

// ---------------------------------------------------------------------------
// Split synchronisation
// ---------------------------------------------------------------------------
void TeamConfig::resync_splits(const DepartmentResolver& resolve) {
    // Snapshot the parents first: rebuild and drop both mutate teams_, so
    // iterating it directly would be walking a vector being appended to and
    // erased from underneath.
    std::vector<TeamEntry> parents;
    for (const auto& t : teams_)
        if (!t.is_split()) parents.push_back(t);

    for (const auto& parent : parents) {
        const bool eligible =
            parent.split_enabled &&
            split_policy::department_allows_split(parent.department);

        if (eligible) rebuild_splits_for(parent, resolve);
        else          drop_splits_for(parent.name);
    }

    // A duplicate whose parent no longer exists is orphaned. Drop it, so nothing
    // is left pointing at a name that does not resolve.
    std::unordered_set<std::string> live;
    for (const auto& t : teams_)
        if (!t.is_split()) live.insert(t.name);

    std::vector<std::string> orphaned;
    for (const auto& t : teams_)
        if (t.is_split() && live.find(t.parent) == live.end())
            orphaned.push_back(t.parent);

    for (const auto& parent_name : orphaned)
        drop_splits_for(parent_name);
}

void TeamConfig::rebuild_splits_for(const TeamEntry& parent,
                                    const DepartmentResolver& resolve) {
    // N comes out of the roster. A team with three generating members has N == 3
    // because it has three, not because a 3 was recorded somewhere that could
    // fall out of step.
    const auto generating = generating_members(parent.roster, resolve);
    const int n = static_cast<int>(generating.size());

    // Rebuilt from nothing rather than diffed. Cheaper to reason about, and the
    // only way the result is guaranteed to match the parent rather than to match
    // whatever the last diff believed.
    drop_splits_for(parent.name);

    if (n == 0) return;

    for (int k = 1; k <= n; ++k) {
        TeamEntry dup;
        dup.id            = mint_id("split");
        dup.name          = split_policy::split_name_for(parent.name, k);
        dup.department    = parent.department;
        dup.kind          = entry_kind::kTeam;
        dup.split_enabled = false;
        dup.parent        = parent.name;

        // The parent's roster, verbatim. THE SAME AGENTS — the same names, the
        // same entries, the same single set of loaded weights. Nothing here
        // creates, copies, or renames an agent; an agent simply belongs to more
        // than one fusion at once.
        //
        // Every member comes across, including any Arbiter. The generating count
        // decided how many duplicates exist; it does not decide what is inside
        // one, and a fusion without its veto seat is a different fusion.
        dup.roster = parent.roster;

        teams_.push_back(std::move(dup));
    }
}

void TeamConfig::drop_splits_for(const std::string& parent_name) {
    // Teams only. There is nothing else to remove.
    teams_.erase(
        std::remove_if(teams_.begin(), teams_.end(),
                       [&](const TeamEntry& t) { return t.parent == parent_name; }),
        teams_.end());
}

} // namespace prime
