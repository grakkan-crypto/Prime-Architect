// name_match.h — the one place a name is ever taken apart.
//
// ===========================================================================
// THE NAMING SCHEME, SYSTEM-WIDE
//
//   Department-Agent[-Suffix]
//
//   Everything past the second hyphen is subsidiary to the first two parts.
//   A suffix beginning with the Split marker makes that name a constituent
//   of the parent named by its first two parts. An Arbiter is a name that
//   begins with the Arbiter marker; a CoT Arbiter is one of those that ends
//   in the CoT marker. The Arbiter for an agent is the Arbiter marker in
//   front of the agent's first two parts run together.
//
// ===========================================================================
// WHAT THIS FILE IS
//
//   Every question anything in the system needs to ask of a name, answered
//   here and nowhere else. No other file cuts a name, builds a name, or
//   tests a name. It hands a list over with the question it needs answered
//   and takes the answer back. Nothing in here is for any one file; every
//   operation is named for what it does, not for who asks.
//
//   Every operation takes a list and works in the list's own order. A
//   question about one name is a list of one. "Nothing came back" is the
//   answer "no" for every question that can have one.
// ===========================================================================

#pragma once

#include <string>
#include <vector>

namespace prime {

inline constexpr const char* kArbiterMarker = "Arbiter-";
inline constexpr const char* kSplitMarker   = "Split";
inline constexpr const char* kCoTMarker     = "CoT";

// ---- one name taken apart ---------------------------------------------------

struct NameParts {
    std::string name;        // as given
    std::string base;        // the first two parts, as one: the name up to
                             // the second hyphen (the whole name if none)
    std::string department;  // before the first hyphen (the whole name if none)
    std::string agent;       // between the first and second hyphens, or the rest
    std::string suffix;      // past the second hyphen; empty if none
};

// Break every name in the list down into its parts.
std::vector<NameParts> break_down(const std::vector<std::string>& names);

// ---- departments ------------------------------------------------------------

struct DepartmentGroup {
    std::string              department;
    std::vector<std::string> names;
};

// Group the list by department, departments in first-seen order.
std::vector<DepartmentGroup> group_by_department(const std::vector<std::string>& names);

struct DepartmentMember {
    std::string name;
    std::string department;   // which of the given departments it belongs to
};

// Of the list, the names in any of the given departments, each with the
// department it belongs to.
std::vector<DepartmentMember> in_departments(const std::vector<std::string>& names,
                                             const std::vector<std::string>& departments);

// ---- split families ---------------------------------------------------------

struct SplitFamily {
    std::string              parent;          // the family's first two parts
    std::vector<std::string> constituents;    // every Split-suffixed name of it
};

// The Split families in the list, parents in first-seen order. Empty means
// the list has no Split name in it at all. A constituent is a functional
// clone of its parent, so the parent is always there: it is reported, not
// checked for.
std::vector<SplitFamily> split_families(const std::vector<std::string>& names);

// ---- arbiters ---------------------------------------------------------------

// The Arbiter for this agent, if the list has it. Empty if it does not.
std::string arbiter_for(const std::vector<std::string>& names,
                        const std::string& agent);

struct Arbiters {
    std::vector<std::string> deterministic;
    std::vector<std::string> cot;
};

// The list's Arbiters, in their two kinds.
Arbiters arbiters(const std::vector<std::string>& names);

} // namespace prime
