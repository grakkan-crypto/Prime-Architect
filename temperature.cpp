// temperature.cpp — Temperature implementation
//
// The one docked operation with its two branches, save, and cancel. This
// file's own stack composes one set of instructions and docks; the root, the
// naming questions, the config read, the expected set, the file read, the
// reconcile, and the commit all happen inside the docked call.

#include "temperature.h"

#include "file_loader.h"
#include "live_registry.h"
#include "name_match.h"
#include "pipeline_loader.h"
#include "text_file.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace prime {

std::string os_temperature_root();
std::string os_config_path();

}

namespace prime {

namespace {

class Cursor {
public:
    explicit Cursor(const std::string& buf) : buf_(buf) {}

    std::uint32_t u32() {
        std::uint32_t v = 0;
        if (take(sizeof(v))) std::memcpy(&v, buf_.data() + at_ - sizeof(v), sizeof(v));
        return v;
    }

    double f64() {
        double v = 0.0;
        if (take(sizeof(v))) std::memcpy(&v, buf_.data() + at_ - sizeof(v), sizeof(v));
        return v;
    }

    bool flag() {
        return take(1) && buf_[at_ - 1] != 0;
    }

    std::string str() {
        const std::uint32_t len = u32();
        if (!take(len)) return {};
        return buf_.substr(at_ - len, len);
    }

    bool ok() const { return ok_; }

private:
    bool take(std::size_t n) {
        if (!ok_ || at_ + n > buf_.size()) { ok_ = false; return false; }
        at_ += n;
        return true;
    }

    const std::string& buf_;
    std::size_t        at_ = 0;
    bool               ok_ = true;
};

void put_u32(std::string& out, std::uint32_t v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void put_str(std::string& out, const std::string& s) {
    put_u32(out, static_cast<std::uint32_t>(s.size()));
    out.append(s);
}

struct ConfigTeam {
    std::string              name;
    std::vector<std::string> members;
};

bool same_row(const LiveTemperature& a, const LiveTemperature& b) {
    return a.team == b.team &&
           !a.answers_to.empty() && !b.answers_to.empty() &&
           a.answers_to.front() == b.answers_to.front();
}

}

const std::vector<std::string> Temperature::declared_needs = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
};

void Temperature::dock(const std::string& pipeline_name,
                       const std::vector<std::string>& roster,
                       Branch branch) {
    LoadRequest request;
    request.entries.push_back({ [this, pipeline_name, roster, branch](const Disk& disk) {
        EntryOutcome eo;

        const std::string root = os_temperature_root();
        if (root.empty()) {
            eo.failure = "root not supplied by OS";
            return eo;
        }
        const std::string path = root + "/" + pipeline_name + kTemperatureFileSuffix;

        std::vector<std::string> config_agents;
        std::vector<ConfigTeam>  teams;
        bool                     config_read = false;
        {
            std::string blob;
            const FileRead read = read_text_file(os_config_path(), blob);

            const bool wellness_check_temperature_config_readable = read == FileRead::Ok;
            (void)wellness_check_temperature_config_readable;

            if (read == FileRead::Ok) {
                config_read = true;
                Cursor c(blob);

                const std::uint32_t agents = c.u32();
                for (std::uint32_t i = 0; i < agents; ++i) {
                    c.str();
                    config_agents.push_back(c.str());
                    c.str(); c.str();
                }

                const std::uint32_t count = c.u32();
                for (std::uint32_t i = 0; i < count; ++i) {
                    ConfigTeam t;
                    c.str();
                    t.name = c.str();
                    c.str();
                    c.flag();
                    const std::uint32_t members = c.u32();
                    for (std::uint32_t m = 0; m < members; ++m) t.members.push_back(c.str());
                    teams.push_back(std::move(t));
                }

                const bool wellness_check_temperature_config_well_formed = c.ok();
                (void)wellness_check_temperature_config_well_formed;
            }
        }

        const std::string architect_department =
            break_down({ std::string(kTemperatureArchitectMember) }).front().department;

        std::vector<std::string> whitelisted;
        for (const auto& m : in_departments(roster, kTemperatureDepartments)) {
            if (m.department == architect_department && m.name != kTemperatureArchitectMember) continue;
            whitelisted.push_back(m.name);
        }

        std::vector<LiveTemperature> expected;

        const std::vector<SplitFamily> families = split_families(whitelisted);

        for (const auto& f : families) {
            for (std::size_t i = 0; i < f.constituents.size(); ++i) {
                LiveTemperature e;
                e.team       = f.parent;
                e.answers_to = { std::to_string(i + 1), f.constituents[i] };
                e.value      = kTemperatureDefault;
                e.is_default = true;
                expected.push_back(std::move(e));
            }
        }

        for (const auto& name : whitelisted) {
            bool in_family = false;
            for (const auto& f : families) {
                if (f.parent == name ||
                    std::find(f.constituents.begin(), f.constituents.end(), name) != f.constituents.end()) {
                    in_family = true;
                    break;
                }
            }
            if (in_family) continue;

            const ConfigTeam* team = nullptr;
            for (const auto& t : teams)
                if (t.name == name) { team = &t; break; }

            if (team == nullptr) {

                const bool wellness_check_temperature_roster_name_in_config =
                    !config_read ||
                    std::find(config_agents.begin(), config_agents.end(), name) !=
                        config_agents.end();
                (void)wellness_check_temperature_roster_name_in_config;

                LiveTemperature e;
                e.answers_to = { name };
                e.value      = kTemperatureDefault;
                e.is_default = true;
                expected.push_back(std::move(e));
                continue;
            }

            const Arbiters   a          = arbiters(team->members);
            const std::size_t generating = team->members.size() - a.deterministic.size() - a.cot.size();

            const bool wellness_check_temperature_team_has_generating_positions = generating != 0;
            (void)wellness_check_temperature_team_has_generating_positions;

            for (std::size_t i = 0; i < generating; ++i) {
                LiveTemperature e;
                e.team       = name;
                e.answers_to = { std::to_string(i + 1) };
                e.value      = kTemperatureDefault;
                e.is_default = true;
                expected.push_back(std::move(e));
            }
        }

        std::vector<LiveTemperature> from_file;
        {
            std::string blob;
            const FileRead read = disk.read(path, blob);
            if (read == FileRead::Ok) {
                Cursor c(blob);
                bool every_row_identified = true;

                const std::uint32_t n = c.u32();
                for (std::uint32_t i = 0; i < n && c.ok(); ++i) {
                    LiveTemperature e;
                    e.team = c.str();
                    const std::uint32_t ids = c.u32();
                    for (std::uint32_t k = 0; k < ids && c.ok(); ++k) e.answers_to.push_back(c.str());
                    e.value      = c.f64();
                    e.is_default = c.flag();
                    if (!c.ok()) break;
                    if (e.answers_to.empty()) { every_row_identified = false; continue; }
                    from_file.push_back(std::move(e));
                }

                const bool wellness_check_temperature_file_well_formed = c.ok();
                (void)wellness_check_temperature_file_well_formed;
                const bool wellness_check_temperature_file_rows_identified = every_row_identified;
                (void)wellness_check_temperature_file_rows_identified;
            }
        }

        std::vector<LiveTemperature> rows;
        rows.reserve(expected.size());
        for (auto want : expected) {
            for (const auto& have : from_file) {
                if (!same_row(have, want)) continue;
                want.value      = have.value;
                want.is_default = have.is_default;
                break;
            }
            rows.push_back(std::move(want));
        }

        if (branch == Branch::Edit) {
            for (const auto& have : from_file) {
                bool expected_here = false;
                for (const auto& row : rows)
                    if (same_row(row, have)) { expected_here = true; break; }
                if (!expected_here) rows.push_back(have);
            }

            TemperatureSession s;
            s.pipeline = pipeline_name;
            s.path     = path;
            s.entries  = std::move(rows);
            s.expected = std::move(expected);
            held_ = std::move(s);
            eo.ok = true;
            return eo;
        }

        live_registry().commit_temperatures(std::move(rows));

        eo.ok = true;
        return eo;
    } });

    FileLoader loader;
    const LoaderReport report = loader.load(request);
    (void)report;
}

TemperatureSession& Temperature::held() {
    return held_;
}

void Temperature::save() {

    TemperatureSession session = std::move(held_);
    held_ = TemperatureSession{};

    LoadRequest request;
    request.entries.push_back({ [path = session.path,
                                 entries = std::move(session.entries)](const Disk& disk) {
        EntryOutcome eo;

        std::string blob;
        put_u32(blob, static_cast<std::uint32_t>(entries.size()));
        for (const auto& e : entries) {
            put_str(blob, e.team);
            put_u32(blob, static_cast<std::uint32_t>(e.answers_to.size()));
            for (const auto& id : e.answers_to) put_str(blob, id);
            blob.append(reinterpret_cast<const char*>(&e.value), sizeof(e.value));
            blob.push_back(0);
        }
        disk.save(path, blob);

        eo.ok = true;
        return eo;
    } });

    FileLoader loader;
    const LoaderReport written = loader.load(request);
    (void)written;

    if (live_registry().pipeline_name() != session.pipeline) return;
    dock(session.pipeline, live_registry().agent_names(), Branch::Load);
}

void Temperature::cancel() {
    held_ = TemperatureSession{};
}

}
