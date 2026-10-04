// rules.cpp — Rules and Directives implementation
//
// The one docked operation with its two branches, save, and cancel. This
// file's own stack composes one set of instructions and docks; every path,
// read, decision, the teardown, and every mint happen inside the docked
// call. No name is cut, built, or tested here: every naming fact is asked
// of the naming service and read back.

#include "rules.h"

#include "file_loader.h"
#include "live_registry.h"
#include "name_match.h"
#include "pipeline_loader.h"
#include "pool_maintenance.h"
#include "text_file.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace prime {

std::string os_rules_root();
std::string os_directives_root();

PoolMaintenance& pool_maintenance();

void MASKING_TEMPORARY_pool_being_minted_needs_mask(const std::string& id_type, int id,
                                                    const std::string& shared_directive_name);

}

namespace prime {

namespace {

struct SharedName {
    SharedDirective kind;
    const char*     name;
};

constexpr SharedName kSharedNames[] = {
    { SharedDirective::ArbiterDeterministic,       "Arbiter-Deterministic"       },
    { SharedDirective::ArbiterCoT,                 "Arbiter-CoT"                 },
    { SharedDirective::SplitRole,                  "Split-Role"                  },
    { SharedDirective::Ideation,                   "Ideation"                    },
    { SharedDirective::ProblemSolvingParent,       "ProblemSolving-Parent"       },
    { SharedDirective::ProblemSolvingConstituents, "ProblemSolving-Constituents" },
};

struct Acquired {
    RulesPool                pool;
    std::vector<std::string> readers;
    bool                     masked = false;
};

}

const std::vector<std::string> Rules::declared_needs = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
};

const char* shared_directive_name(SharedDirective kind) {
    for (const auto& s : kSharedNames)
        if (s.kind == kind) return s.name;
    return "";
}

void Rules::dock(const std::string& pipeline_name,
                 const std::vector<std::string>& roster,
                 Branch branch) {
    LoadRequest request;
    request.entries.push_back({ [this, pipeline_name, roster, branch](const Disk& disk) {
        EntryOutcome eo;

        const std::string rules_root      = os_rules_root();
        const std::string directives_root = os_directives_root();
        if (rules_root.empty() || directives_root.empty()) {
            eo.failure = "roots not supplied by OS";
            return eo;
        }

        std::vector<Acquired> acquired;

        const std::string rules_path = rules_root + "/" + pipeline_name + kRulesFileSuffix;
        {
            std::string text;
            const FileRead read = disk.read(rules_path, text);

            const bool wellness_check_rules_file_present = read == FileRead::Ok;
            (void)wellness_check_rules_file_present;

            size_t blocks = 0;
            if (read == FileRead::Ok) {
                size_t lb = 0, le = 0;
                if (find_block(text, 0, '[', ']', lb, le)) {
                    size_t pos = lb;
                    while (pos < le) {
                        size_t bb = 0, be = 0;
                        if (!find_block(text, pos, '{', '}', bb, be)) break;
                        if (bb - 1 >= le) break;

                        Acquired a;
                        a.pool.kind    = RulesPoolKind::Rules;
                        a.pool.name    = pipeline_name;
                        a.pool.path    = rules_path;
                        a.pool.content = text.substr(bb - 1, be - (bb - 1) + 1);
                        acquired.push_back(std::move(a));
                        ++blocks;

                        pos = be + 1;
                    }
                }
            }

            const bool wellness_check_rules_file_produced_blocks =
                read != FileRead::Ok || blocks != 0;
            (void)wellness_check_rules_file_produced_blocks;
        }

        const std::vector<NameParts>   parts    = break_down(roster);
        const std::vector<SplitFamily> families = split_families(roster);
        const Arbiters                 arb      = arbiters(roster);

        {
            std::set<std::string> done;
            for (const auto& p : parts) {
                if (!done.insert(p.base).second) continue;

                const std::string path = directives_root + "/" + p.department
                                       + "/" + p.agent + kDirectiveFileSuffix;

                std::string text;
                const FileRead read = disk.read(path, text);

                const bool wellness_check_directive_present = read == FileRead::Ok;
                (void)wellness_check_directive_present;
                if (read != FileRead::Ok) continue;

                const bool wellness_check_directive_populated =
                    !text.empty() ||
                    std::find(arb.deterministic.begin(), arb.deterministic.end(), p.base)
                        != arb.deterministic.end() ||
                    std::find(arb.cot.begin(), arb.cot.end(), p.base) != arb.cot.end();
                (void)wellness_check_directive_populated;

                Acquired a;
                a.pool.kind    = RulesPoolKind::AgentDirective;
                a.pool.name    = p.base;
                a.pool.path    = path;
                a.pool.content = std::move(text);
                acquired.push_back(std::move(a));
            }
        }

        std::vector<std::string> constituents;
        std::vector<std::string> parents;
        for (const auto& f : families) {
            constituents.insert(constituents.end(), f.constituents.begin(), f.constituents.end());
            parents.push_back(f.parent);
        }
        const bool eligible = !families.empty();

        for (const auto& s : kSharedNames) {
            std::vector<std::string> readers;
            bool masked = false;

            switch (s.kind) {
                case SharedDirective::ArbiterDeterministic:
                    readers = arb.deterministic;
                    break;
                case SharedDirective::ArbiterCoT:
                    readers = arb.cot;
                    break;
                case SharedDirective::SplitRole:
                    if (eligible) readers = constituents;
                    break;
                case SharedDirective::Ideation:
                    if (eligible) {
                        readers = constituents;
                        readers.insert(readers.end(), parents.begin(), parents.end());
                    }
                    masked = true;
                    break;
                case SharedDirective::ProblemSolvingParent:
                    if (eligible) readers = parents;
                    masked = true;
                    break;
                case SharedDirective::ProblemSolvingConstituents:
                    if (eligible) readers = constituents;
                    masked = true;
                    break;
            }
            if (readers.empty()) continue;

            const std::string path = directives_root + "/" + s.name + kDirectiveFileSuffix;

            std::string text;
            const FileRead read = disk.read(path, text);

            const bool wellness_check_shared_directive_present = read == FileRead::Ok;
            (void)wellness_check_shared_directive_present;
            if (read != FileRead::Ok) continue;

            const bool wellness_check_shared_directive_populated = !text.empty();
            (void)wellness_check_shared_directive_populated;

            Acquired a;
            a.pool.kind    = RulesPoolKind::SharedDirective;
            a.pool.name    = s.name;
            a.pool.path    = path;
            a.pool.content = std::move(text);
            a.readers      = std::move(readers);
            a.masked       = masked;
            acquired.push_back(std::move(a));
        }

        if (branch == Branch::Edit) {
            held_pipeline_   = pipeline_name;
            held_rules_path_ = rules_path;
            held_.clear();
            for (auto& a : acquired) held_.push_back(std::move(a.pool));
            eo.ok = true;
            return eo;
        }

        pool_maintenance().destroy("Class ID", 2);
        const bool wellness_check_rules_class_torn_down = true;
        (void)wellness_check_rules_class_torn_down;

        for (auto& a : acquired) {
            std::vector<std::string> readers;
            std::string content = std::move(a.pool.content);

            switch (a.pool.kind) {
                case RulesPoolKind::Rules: {

                    std::vector<std::string> audience;
                    read_string_list(content, 1, content.size() - 1, "audience", audience);

                    std::set<std::string> set;
                    for (const auto& declared : break_down(audience)) {
                        if (declared.name == kAiRulesList) {
                            set.insert(roster.begin(), roster.end());
                            continue;
                        }
                        for (const auto& p : parts)
                            if (p.base == declared.base) set.insert(p.name);
                    }

                    const std::vector<std::string> agents(set.begin(), set.end());
                    for (const auto& agent : agents) {
                        const std::string arbiter = arbiter_for(roster, agent);
                        if (!arbiter.empty()) set.insert(arbiter);
                    }

                    readers.assign(set.begin(), set.end());

                    const size_t name_at = content.find("\"audience\"");
                    size_t lb = 0, le = 0;
                    if (name_at != std::string::npos &&
                        find_block(content, name_at, '[', ']', lb, le)) {
                        size_t cut_from = name_at;
                        size_t cut_to   = le + 1;
                        while (cut_to < content.size() &&
                               std::isspace(static_cast<unsigned char>(content[cut_to])))
                            ++cut_to;
                        if (cut_to < content.size() && content[cut_to] == ',') {
                            ++cut_to;
                        } else {
                            size_t back = cut_from;
                            while (back > 0 &&
                                   std::isspace(static_cast<unsigned char>(content[back - 1])))
                                --back;
                            if (back > 0 && content[back - 1] == ',') cut_from = back - 1;
                        }
                        content.erase(cut_from, cut_to - cut_from);
                    }
                    break;
                }
                case RulesPoolKind::AgentDirective:

                    for (const auto& p : parts)
                        if (p.base == a.pool.name) readers.push_back(p.name);
                    break;
                case RulesPoolKind::SharedDirective:
                    readers = std::move(a.readers);
                    if (a.masked)
                        MASKING_TEMPORARY_pool_being_minted_needs_mask("Class ID", 2, a.pool.name);
                    break;
            }

            pool_maintenance().mint("Class ID", 2, readers, content);
        }

        eo.ok = true;
        return eo;
    } });

    FileLoader loader;
    const LoaderReport report = loader.load(request);
    (void)report;
}

std::vector<RulesPool>& Rules::held() {
    return held_;
}

void Rules::save() {

    std::string            pipeline   = std::move(held_pipeline_);
    std::string            rules_path = std::move(held_rules_path_);
    std::vector<RulesPool> pools      = std::move(held_);
    held_pipeline_.clear();
    held_rules_path_.clear();
    held_.clear();

    LoadRequest request;
    request.entries.push_back({ [rules_path, pools = std::move(pools)](const Disk& disk) {
        EntryOutcome eo;

        std::string rules = "{\n  \"rules\": [\n";
        bool first = true;
        for (const auto& p : pools) {
            if (p.kind != RulesPoolKind::Rules) continue;
            if (!first) rules += ",\n";
            rules += p.content;
            first = false;
        }
        rules += "\n  ]\n}\n";
        disk.save(rules_path, rules);

        for (const auto& p : pools) {
            if (p.kind == RulesPoolKind::Rules) continue;
            disk.save(p.path, p.content);
        }

        eo.ok = true;
        return eo;
    } });

    FileLoader loader;
    const LoaderReport written = loader.load(request);
    (void)written;

    if (live_registry().pipeline_name() != pipeline) return;
    dock(pipeline, live_registry().agent_names(), Branch::Load);
}

void Rules::cancel() {
    held_pipeline_.clear();
    held_rules_path_.clear();
    held_.clear();
}

}
