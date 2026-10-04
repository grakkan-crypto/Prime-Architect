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

struct NameParts {
    std::string name;
    std::string base;

    std::string department;
    std::string agent;
    std::string suffix;
};

std::vector<NameParts> break_down(const std::vector<std::string>& names);

struct DepartmentGroup {
    std::string              department;
    std::vector<std::string> names;
};

std::vector<DepartmentGroup> group_by_department(const std::vector<std::string>& names);

struct DepartmentMember {
    std::string name;
    std::string department;
};

std::vector<DepartmentMember> in_departments(const std::vector<std::string>& names,
                                             const std::vector<std::string>& departments);

struct SplitFamily {
    std::string              parent;
    std::vector<std::string> constituents;
};

std::vector<SplitFamily> split_families(const std::vector<std::string>& names);

std::string arbiter_for(const std::vector<std::string>& names,
                        const std::string& agent);

struct Arbiters {
    std::vector<std::string> deterministic;
    std::vector<std::string> cot;
};

Arbiters arbiters(const std::vector<std::string>& names);

}
