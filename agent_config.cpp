// agent_config.cpp — Prime Architect frontend agent authoring implementation

#include "agent_config.h"
#include "model_discovery.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>

namespace prime {

namespace {

// Specialised modality departments: assignment restricted to their own folder.
// Hardcoded by design — a new modality is added here by explicit instruction,
// never auto-derived from disk.
const std::array<const char*, 5> kSpecialised = {
    "Aperture",  // vision
    "Accord",    // asr
    "Artist",    // image
    "Artisan",   // video
    "Announcer"  // tts
};

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

bool AgentConfig::is_specialised_department(const std::string& department) {
    return std::any_of(kSpecialised.begin(), kSpecialised.end(),
                       [&](const char* s) { return department == s; });
}

void AgentConfig::set_entries(std::vector<AgentEntry> entries) {
    entries_ = std::move(entries);
}

const AgentEntry* AgentConfig::find_by_name(const std::string& name) const {
    for (const auto& e : entries_)
        if (e.name == name) return &e;
    return nullptr;
}

std::optional<AgentEntry> AgentConfig::by_name(const std::string& name) const {
    const AgentEntry* e = find_by_name(name);
    if (!e) return std::nullopt;
    return *e;
}

std::vector<AgentEntry>
AgentConfig::in_department(const std::string& department) const {
    std::vector<AgentEntry> out;
    for (const auto& e : entries_)
        if (e.kind == entry_kind::kAgent && e.department == department)
            out.push_back(e);
    return out;
}

AgentEntry AgentConfig::create_agent(const std::string& name,
                                     const std::string& department,
                                     const std::string& mapped_path,
                                     const std::string& compute_target) {
    AgentEntry e;
    e.id             = mint_id("manual");
    e.name           = name;
    e.department     = department;
    e.kind           = entry_kind::kAgent;
    e.mapped_path    = mapped_path;
    e.compute_target = compute_target;
    entries_.push_back(e);
    return e;
}

bool AgentConfig::edit_agent(const std::string& id,
                             const std::string& mapped_path,
                             const std::string& compute_target) {
    for (auto& e : entries_) {
        if (e.id != id || e.kind != entry_kind::kAgent) continue;
        e.mapped_path    = mapped_path;
        e.compute_target = compute_target;
        return true;
    }
    return false;
}

std::optional<AgentConfig::RenameResult>
AgentConfig::rename_agent(const std::string& id, const std::string& new_name) {
    for (const auto& e : entries_)
        if (e.name == new_name)
            return std::nullopt;

    for (auto& e : entries_) {
        if (e.id != id || e.kind != entry_kind::kAgent) continue;
        RenameResult r{e.name, new_name};
        e.name = new_name;
        return r;
    }
    return std::nullopt;
}

bool AgentConfig::delete_agent(const std::string& id) {
    const auto before = entries_.size();
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const AgentEntry& e) {
                           return e.id == id && e.kind == entry_kind::kAgent;
                       }),
        entries_.end());
    return entries_.size() != before;
}

std::vector<ModelChoice>
AgentConfig::assignable_models(const std::string& department,
                               const std::vector<DiscoveredModel>& discovered) const {
    std::vector<ModelChoice> out;
    const bool specialised = is_specialised_department(department);

    for (const auto& m : discovered) {
        if (m.kind == ModelKind::Malformed) continue;

        const bool same_dept = (m.department == department);
        const bool visible = specialised
                           ? same_dept
                           : (same_dept || !is_specialised_department(m.department));
        if (!visible) continue;

        out.push_back(ModelChoice{m.name, m.load_path, m.department});
    }
    return out;
}

} // namespace prime
