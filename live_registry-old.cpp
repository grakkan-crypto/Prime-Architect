// live_registry.cpp — Prime Engine live registry implementation

#include "live_registry.h"

#include "../foundation/text_file.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace prime {

namespace {

class Cursor {
public:
    explicit Cursor(const std::string& buf) : buf_(buf) {}

    uint32_t u32() {
        require(sizeof(uint32_t));
        uint32_t v = 0;
        std::memcpy(&v, buf_.data() + at_, sizeof(v));
        at_ += sizeof(v);
        return v;
    }

    double f64() {
        require(sizeof(double));
        double v = 0.0;
        std::memcpy(&v, buf_.data() + at_, sizeof(v));
        at_ += sizeof(v);
        return v;
    }

    bool flag() {
        require(1);
        const bool v = buf_[at_] != 0;
        at_ += 1;
        return v;
    }

    std::string str() {
        const uint32_t len = u32();
        require(len);
        std::string s = buf_.substr(at_, len);
        at_ += len;
        return s;
    }

    bool exhausted() const { return at_ >= buf_.size(); }

private:
    void require(size_t n) const {
        if (at_ + n > buf_.size())
            throw std::runtime_error("live_registry: file ends mid-record");
    }

    const std::string& buf_;
    size_t             at_ = 0;
};

void put_u32(std::string& out, uint32_t v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void put_f64(std::string& out, double v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void put_flag(std::string& out, bool v) {
    const char c = v ? 1 : 0;
    out.append(&c, 1);
}

void put_str(std::string& out, const std::string& s) {
    put_u32(out, static_cast<uint32_t>(s.size()));
    out.append(s);
}

std::string join_path(const std::string& root, const std::string& leaf) {
    if (root.empty()) return leaf;
    if (root.back() == '/' || root.back() == '\\') return root + leaf;
    return root + "/" + leaf;
}

std::string directory_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash);
}

}

std::string LiveRegistry::department_of(const std::string& name) {
    const size_t cut = name.find_first_of("-_");
    if (cut == std::string::npos) return name;
    return name.substr(0, cut);
}

bool LiveRegistry::is_temperature_controllable(const std::string& department,
                                               const std::string& agent_name) {
    for (const char* d : temperature_policy::kDepartments) {
        if (department != d) continue;
        if (department == std::string("Architect"))
            return agent_name == temperature_policy::kArchitectMember;
        return true;
    }
    return false;
}

std::string LiveRegistry::config_file_path() {
    return "PLACEHOLDER_PATH/config.bin";
}

std::string LiveRegistry::temperature_file_path(const std::string& pipeline_name) {
    return join_path("PLACEHOLDER_PATH", pipeline_name + "-temp.bin");
}

ReconcileReport LiveRegistry::load(const std::string& pipeline_name,
                                   const std::vector<std::string>& agent_names) {
    std::lock_guard<std::mutex> lock(mutex_);

    pipeline_name_.clear();
    agents_.clear();
    teams_.clear();
    split_parents_.clear();
    temperatures_.clear();

    ReconcileReport report;

    {
        const std::string path = config_file_path();
        std::string blob;
        switch (read_text_file(path, blob)) {
            case FileRead::Ok:
                break;
            case FileRead::Absent:
                throw std::runtime_error("live_registry: config not found: " + path);
            case FileRead::Unreadable:
                report.config_unreadable = true;
                throw std::runtime_error("live_registry: config unreadable: " + path);
        }

        Cursor c(blob);

        const uint32_t agent_count = c.u32();
        agents_.reserve(agent_names.size());
        for (uint32_t i = 0; i < agent_count; ++i) {
            LiveAgent a;
            a.id             = c.str();
            a.name           = c.str();
            a.gguf_path      = c.str();
            a.compute_target = c.str();
            if (a.name.empty())
                throw std::runtime_error("live_registry: agent with no name");
            if (std::find(agent_names.begin(), agent_names.end(), a.name)
                == agent_names.end())
                continue;
            agents_.push_back(std::move(a));
        }

        for (const auto& wanted : agent_names) {
            const bool found =
                std::any_of(agents_.begin(), agents_.end(),
                            [&](const LiveAgent& a) { return a.name == wanted; });
            if (!found)
                throw std::runtime_error(
                    "live_registry: pipeline names agent '" + wanted +
                    "' which the config does not declare");
        }

        const uint32_t team_count = c.u32();
        teams_.reserve(team_count);
        for (uint32_t i = 0; i < team_count; ++i) {
            LiveTeam t;
            t.id            = c.str();
            t.name          = c.str();
            t.parent        = c.str();
            t.split_enabled = c.flag();
            const uint32_t members = c.u32();
            t.roster.reserve(members);
            for (uint32_t m = 0; m < members; ++m) t.roster.push_back(c.str());
            if (t.name.empty())
                throw std::runtime_error("live_registry: team with no name");

            const bool mine =
                !t.roster.empty() &&
                std::all_of(t.roster.begin(), t.roster.end(),
                            [&](const std::string& m) {
                                return std::find(agent_names.begin(),
                                                 agent_names.end(), m)
                                       != agent_names.end();
                            });
            if (!mine) continue;

            teams_.push_back(std::move(t));
        }

        const uint32_t split_count = c.u32();
        split_parents_.reserve(split_count);
        for (uint32_t i = 0; i < split_count; ++i) {
            LiveSplitParent s;
            s.parent_name = c.str();
            const uint32_t runners = c.u32();
            s.runners.reserve(runners);
            for (uint32_t r = 0; r < runners; ++r) s.runners.push_back(c.str());
            if (s.runners.empty() || s.runners.front() != s.parent_name)
                throw std::runtime_error(
                    "live_registry: split parent '" + s.parent_name +
                    "' does not list itself first");

            const bool mine =
                std::any_of(teams_.begin(), teams_.end(),
                            [&](const LiveTeam& t) { return t.name == s.parent_name; });
            if (!mine) continue;

            split_parents_.push_back(std::move(s));
        }
    }

    for (const auto& t : teams_) {
        for (const auto& member : t.roster) {
            if (find_agent_locked(member) == nullptr)
                throw std::runtime_error("live_registry: team '" + t.name +
                                         "' names unknown member '" + member + "'");
        }
    }

    pipeline_name_ = pipeline_name;

    std::vector<LiveTemperature> expected;
    std::vector<std::string>     teamed;

    for (const auto& t : teams_) {
        if (!t.parent.empty()) continue;
        for (const auto& member : t.roster) {
            if (!is_temperature_controllable(department_of(member), member)) continue;
            teamed.push_back(member);
            LiveTemperature e;
            e.team  = t.name;
            e.agent = member;
            expected.push_back(std::move(e));
        }
    }

    for (const auto& a : agents_) {
        if (!is_temperature_controllable(department_of(a.name), a.name)) continue;
        if (std::find(teamed.begin(), teamed.end(), a.name) != teamed.end()) continue;
        LiveTemperature e;
        e.agent = a.name;
        expected.push_back(std::move(e));
    }

    std::vector<LiveTemperature> from_file;
    {
        std::string blob;
        const std::string path = temperature_file_path(pipeline_name);
        switch (read_text_file(path, blob)) {
            case FileRead::Ok: {
                Cursor c(blob);
                const uint32_t n = c.u32();
                from_file.reserve(n);
                for (uint32_t i = 0; i < n; ++i) {
                    LiveTemperature e;
                    e.team       = c.str();
                    e.agent      = c.str();
                    e.value      = c.f64();
                    e.is_default = c.flag();
                    from_file.push_back(std::move(e));
                }
                break;
            }
            case FileRead::Absent:

                break;
            case FileRead::Unreadable:

                report.temperatures_unreadable = true;
                break;
        }
    }

    for (const auto& f : from_file) {
        const bool still_here =
            std::any_of(expected.begin(), expected.end(),
                        [&](const LiveTemperature& e) {
                            return e.team == f.team && e.agent == f.agent;
                        });
        if (!still_here) report.dropped.emplace_back(f.team, f.agent);
    }

    temperatures_.reserve(expected.size());
    for (auto& e : expected) {
        auto it = std::find_if(from_file.begin(), from_file.end(),
                               [&](const LiveTemperature& f) {
                                   return f.team == e.team && f.agent == e.agent;
                               });
        if (it != from_file.end() &&
            it->value >= temperature_policy::kMin &&
            it->value <= temperature_policy::kMax) {
            e.value      = it->value;
            e.is_default = it->is_default;
        } else {
            e.value      = temperature_policy::kDefault;
            e.is_default = true;
            report.defaulted.emplace_back(e.team, e.agent);
        }
        temperatures_.push_back(std::move(e));
    }

    return report;
}

void LiveRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_name_.clear();

    agents_.clear();
    teams_.clear();
    split_parents_.clear();
    temperatures_.clear();
}

std::string LiveRegistry::pipeline_name() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pipeline_name_;
}

bool LiveRegistry::loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pipeline_name_.empty();
}

const LiveAgent* LiveRegistry::find_agent_locked(const std::string& name) const {
    for (const auto& a : agents_)
        if (a.name == name) return &a;
    return nullptr;
}

std::optional<LiveAgent> LiveRegistry::agent(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const LiveAgent* a = find_agent_locked(name)) return *a;
    return std::nullopt;
}

std::vector<LiveAgent> LiveRegistry::agents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return agents_;
}

std::vector<std::string> LiveRegistry::agent_names() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.reserve(agents_.size());
    for (const auto& a : agents_) out.push_back(a.name);
    return out;
}

bool LiveRegistry::has_agent(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find_agent_locked(name) != nullptr;
}

size_t LiveRegistry::agent_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return agents_.size();
}

std::vector<std::string> LiveRegistry::models_to_load() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    for (const auto& a : agents_) {
        if (a.gguf_path.empty()) continue;
        if (std::find(out.begin(), out.end(), a.gguf_path) == out.end())
            out.push_back(a.gguf_path);
    }
    return out;
}

bool LiveRegistry::attach_weights(const std::string& agent_name,
                                  const WeightRegion* region) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& a : agents_) {
        if (a.name != agent_name) continue;
        a.weights = region;
        return true;
    }
    return false;
}

const LiveTeam* LiveRegistry::find_team_locked(const std::string& name) const {
    for (const auto& t : teams_)
        if (t.name == name) return &t;
    return nullptr;
}

std::optional<LiveTeam> LiveRegistry::team(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const LiveTeam* t = find_team_locked(name)) return *t;
    return std::nullopt;
}

std::vector<LiveTeam> LiveRegistry::teams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return teams_;
}

std::vector<std::string>
LiveRegistry::generating_members_locked(const LiveTeam& t) const {

    int arbiters = 0;
    for (const auto& m : t.roster)
        if (department_of(m) == "Arbiter") ++arbiters;

    std::vector<std::string> out;
    out.reserve(t.roster.size());
    for (const auto& m : t.roster) {
        if (arbiters == 1 && department_of(m) == "Arbiter") continue;
        out.push_back(m);
    }
    return out;
}

std::vector<std::string>
LiveRegistry::generating_members(const std::string& team_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const LiveTeam* t = find_team_locked(team_name))
        return generating_members_locked(*t);
    return {};
}

std::string LiveRegistry::veto_seat(const std::string& team_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const LiveTeam* t = find_team_locked(team_name);
    if (!t) return {};

    std::string found;
    int arbiters = 0;
    for (const auto& m : t->roster) {
        if (department_of(m) != "Arbiter") continue;
        ++arbiters;
        found = m;
    }
    return (arbiters == 1) ? found : std::string{};
}

std::vector<LiveSplitParent> LiveRegistry::split_parents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return split_parents_;
}

std::optional<LiveSplitParent>
LiveRegistry::split_parent(const std::string& parent_team) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : split_parents_)
        if (s.parent_name == parent_team) return s;
    return std::nullopt;
}

std::vector<LiveTemperature> LiveRegistry::temperatures() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return temperatures_;
}

std::optional<double>
LiveRegistry::temperature_value(const std::string& team,
                                const std::string& agent) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& e : temperatures_)
        if (e.team == team && e.agent == agent) return e.value;
    return std::nullopt;
}

std::optional<bool>
LiveRegistry::temperature_is_default(const std::string& team,
                                     const std::string& agent) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& e : temperatures_)
        if (e.team == team && e.agent == agent) return e.is_default;
    return std::nullopt;
}

std::optional<double>
LiveRegistry::flat_temperature_for_runner(const std::string& parent_team,
                                          const std::string& runner_name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const LiveSplitParent* sp = nullptr;
    for (const auto& s : split_parents_)
        if (s.parent_name == parent_team) { sp = &s; break; }
    if (!sp) return std::nullopt;

    size_t position = 0;
    bool   found    = false;
    for (size_t i = 0; i < sp->runners.size(); ++i) {
        if (sp->runners[i] != runner_name) continue;
        position = i;
        found    = true;
        break;
    }
    if (!found) return std::nullopt;

    const LiveTeam* t = find_team_locked(parent_team);
    if (!t) return std::nullopt;

    const std::vector<std::string> members = generating_members_locked(*t);
    if (position >= members.size()) return std::nullopt;

    const std::string& borrowed_from = members[position];
    for (const auto& e : temperatures_)
        if (e.team == parent_team && e.agent == borrowed_from) return e.value;

    return std::nullopt;
}

bool LiveRegistry::commit_temperatures(const std::vector<LiveTemperature>& edited,
                                       std::string& rejected_team_out,
                                       std::string& rejected_agent_out) {
    std::lock_guard<std::mutex> lock(mutex_);

    rejected_team_out.clear();
    rejected_agent_out.clear();

    for (const auto& e : edited) {
        if (e.value >= temperature_policy::kMin && e.value <= temperature_policy::kMax)
            continue;
        rejected_team_out  = e.team;
        rejected_agent_out = e.agent;
        return false;
    }

    for (const auto& e : edited) {
        for (auto& held : temperatures_) {
            if (held.team != e.team || held.agent != e.agent) continue;
            held.value      = e.value;
            held.is_default = false;
            break;
        }
    }

    return write_temperatures_locked();
}

bool LiveRegistry::acknowledge_defaults() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : temperatures_) e.is_default = false;
    return write_temperatures_locked();
}

bool LiveRegistry::write_temperatures_locked() const {
    if (pipeline_name_.empty()) return false;

    std::string blob;
    put_u32(blob, static_cast<uint32_t>(temperatures_.size()));
    for (const auto& e : temperatures_) {
        put_str(blob, e.team);
        put_str(blob, e.agent);
        put_f64(blob, e.value);
        put_flag(blob, e.is_default);
    }

    return write_text_file(temperature_file_path(pipeline_name_), blob);
}

void LiveRegistry::set_rebuttal_active(bool active) {
    const bool was = rebuttal_active_.exchange(active, std::memory_order_acq_rel);
    if (was == active) return;

    std::vector<std::function<void(bool)>> signals;
    {
        std::lock_guard<std::mutex> lock(rebuttal_signal_mutex_);
        signals = rebuttal_signals_;
    }
    for (const auto& s : signals) s(active);
}

bool LiveRegistry::rebuttal_active() const {
    return rebuttal_active_.load(std::memory_order_acquire);
}

void LiveRegistry::on_rebuttal_switch(std::function<void(bool active)> signal) {
    std::lock_guard<std::mutex> lock(rebuttal_signal_mutex_);
    rebuttal_signals_.push_back(std::move(signal));
}

void LiveRegistry::link_prompt(const std::string& prompt_id,
                               std::vector<std::string> linked_pool_ids) {
    std::lock_guard<std::mutex> lock(links_mutex_);
    prompt_links_[prompt_id] = std::move(linked_pool_ids);
}

std::vector<std::string> LiveRegistry::linked_pools(const std::string& prompt_id) const {
    std::lock_guard<std::mutex> lock(links_mutex_);
    auto it = prompt_links_.find(prompt_id);
    return it == prompt_links_.end() ? std::vector<std::string>{} : it->second;
}

void LiveRegistry::unlink_prompt(const std::string& prompt_id) {
    std::lock_guard<std::mutex> lock(links_mutex_);
    prompt_links_.erase(prompt_id);
}

void LiveRegistry::assign_class_ids(const std::vector<std::string>& declared) {
    std::lock_guard<std::mutex> lock(class_table_mutex_);
    class_ids_.clear();
    uint64_t next = kSharedContextClassId + 1;
    for (const auto& name : declared) {
        if (name == kSharedContextClassName) continue;
        if (class_ids_.count(name) != 0) continue;
        class_ids_[name] = next++;
    }
}

uint64_t LiveRegistry::class_id_for(const std::string& name) const {
    if (name == kSharedContextClassName) return kSharedContextClassId;
    std::lock_guard<std::mutex> lock(class_table_mutex_);
    auto it = class_ids_.find(name);
    return it == class_ids_.end() ? 0 : it->second;
}

std::optional<uint64_t> LiveRegistry::class_size_bytes_per_token(
        uint64_t class_id) const {
    std::lock_guard<std::mutex> lock(class_table_mutex_);
    auto it = class_sizes_.find(class_id);
    if (it == class_sizes_.end()) return std::nullopt;
    return it->second;
}

void LiveRegistry::set_class_size_bytes_per_token(uint64_t class_id,
                                                  uint64_t bytes) {
    std::lock_guard<std::mutex> lock(class_table_mutex_);
    class_sizes_[class_id] = bytes;
}

void LiveRegistry::set_mask_pool_facts(const std::string& pool_id,
bool fixed_no_holder,
bool cascade_exempt) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto& f = mask_facts_[pool_id];
    f.fixed_no_holder = fixed_no_holder;
    f.cascade_exempt  = cascade_exempt;
}

std::optional<bool> LiveRegistry::mask_fixed_no_holder(const std::string& pool_id) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_facts_.find(pool_id);
    if (it == mask_facts_.end()) return std::nullopt;
    return it->second.fixed_no_holder;
}

std::optional<bool> LiveRegistry::mask_cascade_exempt(const std::string& pool_id) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_facts_.find(pool_id);
    if (it == mask_facts_.end()) return std::nullopt;
    return it->second.cascade_exempt;
}

void LiveRegistry::set_mask_agent_turn_level(const std::string& agent, bool turn_level) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    if (turn_level) mask_turn_level_agents_.insert(agent);
    else            mask_turn_level_agents_.erase(agent);
}

bool LiveRegistry::mask_agent_turn_level(const std::string& agent) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    return mask_turn_level_agents_.count(agent) != 0;
}

void LiveRegistry::set_mask_standing_open(const std::string& pool_id,
const std::string& agent, bool open) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto& v = mask_facts_[pool_id].standing_open;
    auto it = std::find(v.begin(), v.end(), agent);
    if (open && it == v.end()) v.push_back(agent);
    if (!open && it != v.end()) v.erase(it);
}

bool LiveRegistry::mask_standing_open(const std::string& pool_id,
const std::string& agent) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_facts_.find(pool_id);
    if (it == mask_facts_.end()) return false;
    const auto& v = it->second.standing_open;
    return std::find(v.begin(), v.end(), agent) != v.end();
}

void LiveRegistry::set_mask_fixed_visible(const std::string& pool_id,
const std::string& agent,
std::vector<TokenRange> visible) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    mask_facts_[pool_id].fixed_visible[agent] = std::move(visible);
}

std::vector<TokenRange> LiveRegistry::mask_fixed_visible(const std::string& pool_id,
const std::string& agent) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_facts_.find(pool_id);
    if (it == mask_facts_.end()) return {};
    auto a = it->second.fixed_visible.find(agent);
    return a == it->second.fixed_visible.end() ? std::vector<TokenRange>{}
                                               : a->second;
}

void LiveRegistry::set_mask_mirror(const std::string& controller,
std::vector<std::string> slaves) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    mask_mirrors_[controller] = std::move(slaves);
}

std::vector<std::string> LiveRegistry::mask_mirror_slaves(const std::string& controller) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_mirrors_.find(controller);
    return it == mask_mirrors_.end() ? std::vector<std::string>{} : it->second;
}

bool LiveRegistry::mask_is_pure_slave(const std::string& agent) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    for (const auto& [c, slaves] : mask_mirrors_)
        if (std::find(slaves.begin(), slaves.end(), agent) != slaves.end())
            return true;
    return false;
}

std::string LiveRegistry::mask_controller_of_slave(const std::string& slave) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    for (const auto& [c, slaves] : mask_mirrors_)
        if (std::find(slaves.begin(), slaves.end(), slave) != slaves.end())
            return c;
    return {};
}

void LiveRegistry::set_mask_mirror_exception(const std::string& slave,
const std::string& pool_id) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    mask_mirror_exceptions_.emplace(slave, pool_id);
}

bool LiveRegistry::mask_mirror_excepted(const std::string& slave,
const std::string& pool_id) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    return mask_mirror_exceptions_.count({slave, pool_id}) != 0;
}

void LiveRegistry::mask_open_id(const std::string& id, const std::string& controller) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    mask_open_ids_[id] = controller;
}

void LiveRegistry::mask_close_id(const std::string& id) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    mask_open_ids_.erase(id);
    for (auto& [pool, reveals] : mask_reveals_) {
        (void)pool;
        reveals.erase(std::remove_if(reveals.begin(), reveals.end(),
                                     [&](const MaskReveal& r) {
                                         return r.key == id;
                                     }),
                      reveals.end());
    }
}

std::optional<std::string> LiveRegistry::mask_id_controller(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_open_ids_.find(id);
    if (it == mask_open_ids_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> LiveRegistry::mask_open_ids_of(const std::string& controller) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    std::vector<std::string> out;
    for (const auto& [id, c] : mask_open_ids_)
        if (c == controller) out.push_back(id);
    return out;
}

void LiveRegistry::mask_put_reveal(const std::string& pool_id, MaskReveal reveal) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto& v = mask_reveals_[pool_id];
    for (auto& r : v)
        if (r.key == reveal.key && r.controller == reveal.controller &&
            r.beneficiary == reveal.beneficiary) {
            r = std::move(reveal);
            return;
        }
    v.push_back(std::move(reveal));
}

void LiveRegistry::mask_drop_reveal(const std::string& pool_id,
const std::string& key,
const std::string& controller,
const std::string& beneficiary) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_reveals_.find(pool_id);
    if (it == mask_reveals_.end()) return;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const MaskReveal& r) {
                               return r.key == key &&
                                      r.controller == controller &&
                                      r.beneficiary == beneficiary;
                           }),
            v.end());
}

std::vector<MaskReveal> LiveRegistry::mask_reveals(const std::string& pool_id) const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    auto it = mask_reveals_.find(pool_id);
    return it == mask_reveals_.end() ? std::vector<MaskReveal>{} : it->second;
}

void LiveRegistry::mask_set_all_covered(bool covered) {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    mask_all_covered_ = covered;
}

bool LiveRegistry::mask_all_covered() const {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    return mask_all_covered_;
}

void LiveRegistry::mask_sweep_live() {
    std::lock_guard<std::mutex> lock(mask_mutex_);
    mask_open_ids_.clear();
    mask_reveals_.clear();
    mask_all_covered_ = false;
}

}
