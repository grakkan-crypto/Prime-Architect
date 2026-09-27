// name_match.cpp — the one place a name is ever taken apart.

#include "name_match.h"

#include <algorithm>

namespace prime {

namespace {

bool listed(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool begins_with(const std::string& text, const std::string& marker) {
    return text.size() >= marker.size() && text.compare(0, marker.size(), marker) == 0;
}

bool ends_with(const std::string& text, const std::string& marker) {
    return text.size() >= marker.size() &&
           text.compare(text.size() - marker.size(), marker.size(), marker) == 0;
}

} // namespace

// ---- one name taken apart ---------------------------------------------------

std::vector<NameParts> break_down(const std::vector<std::string>& names) {
    std::vector<NameParts> out;
    out.reserve(names.size());
    for (const auto& name : names) {
        NameParts p;
        p.name = name;

        const size_t first  = name.find('-');
        const size_t second = first == std::string::npos ? std::string::npos
                                                         : name.find('-', first + 1);

        p.base       = second == std::string::npos ? name : name.substr(0, second);
        p.department = first  == std::string::npos ? name : name.substr(0, first);
        p.agent      = first  == std::string::npos ? name : p.base.substr(first + 1);
        p.suffix     = second == std::string::npos ? std::string() : name.substr(second + 1);

        out.push_back(std::move(p));
    }
    return out;
}

// ---- departments ------------------------------------------------------------

std::vector<DepartmentGroup> group_by_department(const std::vector<std::string>& names) {
    std::vector<DepartmentGroup> out;
    for (const auto& p : break_down(names)) {
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const DepartmentGroup& g) { return g.department == p.department; });
        if (it == out.end()) {
            out.push_back({ p.department, {} });
            it = out.end() - 1;
        }
        it->names.push_back(p.name);
    }
    return out;
}

std::vector<DepartmentMember> in_departments(const std::vector<std::string>& names,
                                             const std::vector<std::string>& departments) {
    std::vector<DepartmentMember> out;
    for (const auto& p : break_down(names))
        if (listed(departments, p.department)) out.push_back({ p.name, p.department });
    return out;
}

// ---- split families ---------------------------------------------------------

std::vector<SplitFamily> split_families(const std::vector<std::string>& names) {
    std::vector<SplitFamily> out;
    for (const auto& p : break_down(names)) {
        if (!begins_with(p.suffix, kSplitMarker)) continue;

        auto it = std::find_if(out.begin(), out.end(),
                               [&](const SplitFamily& f) { return f.parent == p.base; });
        if (it == out.end()) {
            out.push_back({ p.base, {} });
            it = out.end() - 1;
        }
        it->constituents.push_back(p.name);
    }
    return out;
}

// ---- arbiters ---------------------------------------------------------------

std::string arbiter_for(const std::vector<std::string>& names,
                        const std::string& agent) {
    const NameParts p = break_down({ agent }).front();
    const std::string candidate = p.department == p.base
                                ? std::string(kArbiterMarker) + p.base
                                : std::string(kArbiterMarker) + p.department + p.agent;
    return listed(names, candidate) ? candidate : std::string();
}

Arbiters arbiters(const std::vector<std::string>& names) {
    Arbiters out;
    for (const auto& name : names) {
        if (!begins_with(name, kArbiterMarker)) continue;
        if (ends_with(name, kCoTMarker)) out.cot.push_back(name);
        else                             out.deterministic.push_back(name);
    }
    return out;
}

} // namespace prime
