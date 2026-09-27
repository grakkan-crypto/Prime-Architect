// temperature.cpp — Temperature implementation
//
// The one docked operation with its two branches, save, and cancel. This
// file's own stack composes one set of instructions and docks; the root, the
// naming questions, the config read, the expected set, the file read, the
// reconcile, and the commit all happen inside the docked call.

#include "temperature.h"

#include "file_loader.h"
#include "live_registry.h"      // the live set, the live pipeline name and roster
#include "name_match.h"         // every naming fact — asked, never worked out here
#include "pipeline_loader.h"    // payload category names
#include "text_file.h"          // read_text_file — the plain config read

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

// ---------------------------------------------------------------------------
// FORWARD DECLARATIONS — called exactly as if they exist; each real header
// replaces its declaration here outright, with the calls below unchanged.
//
// THE ROOT AND THE CONFIG PATH — supplied by the OS. Loud stubs: declared,
// not defined. Nothing beyond the formula is fixed until it exists
// (Ruling 14).
// ---------------------------------------------------------------------------
namespace prime {

std::string os_temperature_root();
std::string os_config_path();

} // namespace prime

namespace prime {

namespace {

// ---------------------------------------------------------------------------
// Direct byte reading and writing — the length-prefixed layout this system
// writes. A fixed-size value is itself; anything else is preceded by its
// byte length. No parser, nothing to escape. A read past what is there marks
// the cursor bad and yields nothing; the caller posts that, once, at the end.
// ---------------------------------------------------------------------------
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

// One config team, as this file needs it and nothing more: its name, and its
// members so the Arbiters among them can be discounted. Nothing else the
// config says about a team is kept.
struct ConfigTeam {
    std::string              name;
    std::vector<std::string> members;
};

// A row is matched to another on team and its own identity — the first
// identifier — and nothing else. A Split name riding on a row never decides
// a match.
bool same_row(const LiveTemperature& a, const LiveTemperature& b) {
    return a.team == b.team &&
           !a.answers_to.empty() && !b.answers_to.empty() &&
           a.answers_to.front() == b.answers_to.front();
}

} // namespace

// ===========================================================================
// THE STANDING DECLARATION — data, read directly by whoever hands over.
// ===========================================================================

const std::vector<std::string> Temperature::declared_needs = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
};

// ===========================================================================
// THE ONE DOCKED OPERATION — naming, config, expected set, file, reconcile,
// branch.
// ===========================================================================

void Temperature::dock(const std::string& pipeline_name,
                       const std::vector<std::string>& roster,
                       Branch branch) {
    LoadRequest request;
    request.entries.push_back({ [this, pipeline_name, roster, branch](const Disk& disk) {
        EntryOutcome eo;

        // The root. Not a file failure: without it nothing that looks like a
        // path may be derived (Ruling 14), so this is reported and the
        // operation goes no further (Ruling 9).
        const std::string root = os_temperature_root();
        if (root.empty()) {
            eo.failure = "root not supplied by OS";
            return eo;
        }
        const std::string path = root + "/" + pipeline_name + kTemperatureFileSuffix;

        // ===================================================================
        // THE CONFIG — a plain read (Ruling 3). The agent list is what a
        // standalone name is checked against; a team is a name and its
        // members. Unreadable is not a stop (Ruling 9): posted, and both
        // stand empty for this dock. Every other field is stepped over.
        // ===================================================================
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
                    c.str();                                // id
                    config_agents.push_back(c.str());       // name
                    c.str(); c.str();                       // path, target
                }

                const std::uint32_t count = c.u32();
                for (std::uint32_t i = 0; i < count; ++i) {
                    ConfigTeam t;
                    c.str();                                // id
                    t.name = c.str();
                    c.str();                                // parent: stepped over, not this file's
                    c.flag();                               // split toggle: stepped over, not this file's
                    const std::uint32_t members = c.u32();
                    for (std::uint32_t m = 0; m < members; ++m) t.members.push_back(c.str());
                    teams.push_back(std::move(t));
                }

                const bool wellness_check_temperature_config_well_formed = c.ok();
                (void)wellness_check_temperature_config_well_formed;
            }
        }

        // ===================================================================
        // THE WHITELIST (Ruling 4) — the naming service says who is in the
        // departments; this file's one policy says which Architect. What
        // survives is kept, every name of it (Ruling 5).
        // ===================================================================
        const std::string architect_department =
            break_down({ std::string(kTemperatureArchitectMember) }).front().department;

        std::vector<std::string> whitelisted;
        for (const auto& m : in_departments(roster, kTemperatureDepartments)) {
            if (m.department == architect_department && m.name != kTemperatureArchitectMember) continue;
            whitelisted.push_back(m.name);
        }

        // ===================================================================
        // THE EXPECTED SET — identical for both branches.
        // ===================================================================
        std::vector<LiveTemperature> expected;

        // ---- the Split families (Ruling 7) — nothing here touches config --
        // Per family: the parent is the team, one numbered row per
        // constituent, and the i-th constituent's name rides on row i.
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

        // ---- everyone else (Ruling 3) — the config, read directly -------
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
                // A config agent, or a variation between roster and config.
                // Posted only when there was a config to check against;
                // either way the name stands as a standalone entry at
                // default — never ignored (Ruling 5).
                const bool wellness_check_temperature_roster_name_in_config =
                    !config_read ||
                    std::find(config_agents.begin(), config_agents.end(), name) !=
                        config_agents.end();
                (void)wellness_check_temperature_roster_name_in_config;

                LiveTemperature e;
                e.answers_to = { name };                // team stays empty
                e.value      = kTemperatureDefault;
                e.is_default = true;
                expected.push_back(std::move(e));
                continue;
            }

            // A team: numbered rows, one per generating member. The naming
            // service says which members are Arbiters; they are discounted
            // (Ruling 6). No member's name is used.
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

        // ===================================================================
        // THE FILE — the one read (Ruling 2). Present, absent, unreadable:
        // FileLoader's own report. A file that ends mid-row yields the rows
        // read before the fault; that it did is posted. A row with no
        // identifier is not a row: dropped, and that it was is posted.
        // ===================================================================
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

        // ===================================================================
        // THE ONE RECONCILE (Ruling 10) — identical for both branches. Every
        // expected row: the saved value and marker where the file has a
        // match, default otherwise. The identifiers are the expected row's:
        // the Split name on a row is this dock's pairing, not the file's.
        // ===================================================================
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

        // ===================================================================
        // THE BRANCH — one thing only: does a saved row nobody expected
        // survive? Nothing above this line knows which.
        // ===================================================================

        // ---- EDIT: it survives, exactly as saved; everything is held -------
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
            held_ = std::move(s);                       // docking again replaces (Ruling 12)
            eo.ok = true;
            return eo;
        }

        // ---- LOAD: it is not carried. The rows, landed in one motion,
        // replacing the live set outright. Landing correctly is the
        // registry's own job; nothing is read back.
        live_registry().commit_temperatures(std::move(rows));

        eo.ok = true;
        return eo;
    } });

    FileLoader loader;
    const LoaderReport report = loader.load(request);
    (void)report;   // FileLoader's own report; Wellness reads it there
}

// ===========================================================================
// WHAT AN EDIT SESSION HOLDS
// ===========================================================================

TemperatureSession& Temperature::held() {
    return held_;
}

// ===========================================================================
// SAVE — hand over what is held and forget it in the same motion; then the
// live check; then the ordinary Load branch if this is the running pipeline.
// ===========================================================================

void Temperature::save() {
    // Handed over and gone. From here Temperature holds nothing.
    TemperatureSession session = std::move(held_);
    held_ = TemperatureSession{};

    LoadRequest request;
    request.entries.push_back({ [path = session.path,
                                 entries = std::move(session.entries)](const Disk& disk) {
        EntryOutcome eo;

        // The rows as they stand, to the path attached to them. Every
        // marker clears in this write (Ruling 11).
        std::string blob;
        put_u32(blob, static_cast<std::uint32_t>(entries.size()));
        for (const auto& e : entries) {
            put_str(blob, e.team);
            put_u32(blob, static_cast<std::uint32_t>(e.answers_to.size()));
            for (const auto& id : e.answers_to) put_str(blob, id);
            blob.append(reinterpret_cast<const char*>(&e.value), sizeof(e.value));
            blob.push_back(0);                          // is_default: cleared
        }
        disk.save(path, blob);                          // FileLoader's own result; not read here

        eo.ok = true;
        return eo;
    } });

    FileLoader loader;
    const LoaderReport written = loader.load(request);
    (void)written;   // FileLoader's own report; Wellness reads it there

    // ---- the loaded pipeline: one direct read, then the ordinary load ------
    if (live_registry().pipeline_name() != session.pipeline) return;
    dock(session.pipeline, live_registry().agent_names(), Branch::Load);
}

// ===========================================================================
// CANCEL — what is held is gone. Nothing is written anywhere.
// ===========================================================================

void Temperature::cancel() {
    held_ = TemperatureSession{};
}

} // namespace prime
