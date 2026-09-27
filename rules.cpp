// rules.cpp — Rules and Directives implementation
//
// The one docked operation with its two branches, save, and cancel. This
// file's own stack composes one set of instructions and docks; every path,
// read, decision, the teardown, and every mint happen inside the docked
// call. No name is cut, built, or tested here: every naming fact is asked
// of the naming service and read back.

#include "rules.h"

#include "file_loader.h"
#include "live_registry.h"      // the live pipeline name and roster
#include "name_match.h"         // every question asked of a name
#include "pipeline_loader.h"    // payload category names
#include "pool_maintenance.h"   // the class down; every pool up
#include "text_file.h"          // find_block, read_string_list

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

// ---------------------------------------------------------------------------
// FORWARD DECLARATIONS — called exactly as if they exist; each real header
// replaces its declaration here outright, with the calls below unchanged.
//
// THE ROOTS — supplied by the OS. Nothing beyond the formula is fixed until
// it exists (Ruling 13).
//
// THE ONE LIVE POOLMAINTENANCE — reached the same way the live registry is:
// one instance, one accessor. The class-wide teardown and the one-pool mint
// are its own operations, called on it directly below.
//
// >>> TEMPORARY — MASKING IS NOT BUILT (Ruling 14) <<<
// The pool being minted now needs system-driven masking on, by default.
// That is the whole of what Rules has to say about it. This call is
// replaced outright when Masking exists. It is not a quiet no-op and must
// not become one.
// ---------------------------------------------------------------------------
namespace prime {

std::string os_rules_root();
std::string os_directives_root();

PoolMaintenance& pool_maintenance();

void MASKING_TEMPORARY_pool_being_minted_needs_mask(const std::string& id_type, int id,
                                                    const std::string& shared_directive_name);

} // namespace prime

namespace prime {

namespace {

// ---- shared directive names — one table ------------------------------------

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

// One acquired item, inside the dock only. The pool is what either branch
// receives. For a shared directive, the readers were the reason it was read
// at all, so they ride with it to the mint and never leave the dock.
struct Acquired {
    RulesPool                pool;
    std::vector<std::string> readers;   // shared directives only
    bool                     masked = false;
};

} // namespace

// ===========================================================================
// THE STANDING DECLARATION — data, read directly by whoever hands over.
// ===========================================================================

const std::vector<std::string> Rules::declared_needs = {
    payload_categories::kPipelineName,
    payload_categories::kRoster,
};

const char* shared_directive_name(SharedDirective kind) {
    for (const auto& s : kSharedNames)
        if (s.kind == kind) return s.name;
    return "";
}

// ===========================================================================
// THE ONE DOCKED OPERATION — acquire, then branch. Everything inside.
// ===========================================================================

void Rules::dock(const std::string& pipeline_name,
                 const std::vector<std::string>& roster,
                 Branch branch) {
    LoadRequest request;
    request.entries.push_back({ [this, pipeline_name, roster, branch](const Disk& disk) {
        EntryOutcome eo;

        // The roots. Not a file failure: without them nothing that looks
        // like a path may be derived (Ruling 13), so this is reported and
        // the operation goes no further.
        const std::string rules_root      = os_rules_root();
        const std::string directives_root = os_directives_root();
        if (rules_root.empty() || directives_root.empty()) {
            eo.failure = "roots not supplied by OS";
            return eo;
        }

        // ===================================================================
        // ACQUISITION — identical for both branches. Every path, every read.
        // A read that is not Ok is FileLoader's own report; the item simply
        // does not exist here. Nothing stops.
        // ===================================================================
        std::vector<Acquired> acquired;

        // ---- the Rules file: one read, already block-shaped ----------------
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

            // Read fine, produced nothing — a distinct state, posted.
            const bool wellness_check_rules_file_produced_blocks =
                read != FileRead::Ok || blocks != 0;
            (void)wellness_check_rules_file_produced_blocks;
        }

        // ---- the roster, asked once: who is what ---------------------------
        const std::vector<NameParts>   parts    = break_down(roster);
        const std::vector<SplitFamily> families = split_families(roster);
        const Arbiters                 arb      = arbiters(roster);

        // ---- directives: one per base, one after another ------------------
        {
            std::set<std::string> done;
            for (const auto& p : parts) {
                if (!done.insert(p.base).second) continue;   // read once, shared

                const std::string path = directives_root + "/" + p.department
                                       + "/" + p.agent + kDirectiveFileSuffix;

                std::string text;
                const FileRead read = disk.read(path, text);

                const bool wellness_check_directive_present = read == FileRead::Ok;
                (void)wellness_check_directive_present;
                if (read != FileRead::Ok) continue;

                // Alarm for any non-Arbiter; silent for an Arbiter's own.
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

        // ---- the six shared directives: readers exist, or nothing to read -
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

        // ===================================================================
        // THE BRANCH (Ruling 9). Nothing above this line knows which.
        // ===================================================================

        // ---- EDIT: hold exactly what was acquired, nothing else -----------
        if (branch == Branch::Edit) {
            held_pipeline_   = pipeline_name;
            held_rules_path_ = rules_path;
            held_.clear();
            for (auto& a : acquired) held_.push_back(std::move(a.pool));
            eo.ok = true;
            return eo;
        }

        // ---- LOAD: the class down, once, now (Ruling 6) -------------------
        // The action, the id type, the value. Four id types exist on a pool;
        // this names which.
        pool_maintenance().destroy("Class ID", 2);
        const bool wellness_check_rules_class_torn_down = true;
        (void)wellness_check_rules_class_torn_down;

        // ---- then one item at a time: resolve, mint, move on --------------
        for (auto& a : acquired) {
            std::vector<std::string> readers;
            std::string content = std::move(a.pool.content);

            switch (a.pool.kind) {
                case RulesPoolKind::Rules: {
                    // The one field read from a block: who it is for.
                    std::vector<std::string> audience;
                    read_string_list(content, 1, content.size() - 1, "audience", audience);

                    // First pass: agents. The whole-roster name is everyone;
                    // any other name is every roster agent sharing its base.
                    std::set<std::string> set;
                    for (const auto& declared : break_down(audience)) {
                        if (declared.name == kAiRulesList) {
                            set.insert(roster.begin(), roster.end());
                            continue;
                        }
                        for (const auto& p : parts)
                            if (p.base == declared.base) set.insert(p.name);
                    }

                    // Second pass, over exactly those: each one's Arbiter, if
                    // the roster has it (Ruling 4).
                    const std::vector<std::string> agents(set.begin(), set.end());
                    for (const auto& agent : agents) {
                        const std::string arbiter = arbiter_for(roster, agent);
                        if (!arbiter.empty()) set.insert(arbiter);
                    }

                    readers.assign(set.begin(), set.end());

                    // TRIM. The audience field routed this block here; it is
                    // not content. Cut the field — name, colon, list, and the
                    // comma joining it to its neighbour — and carry the rest
                    // of the block verbatim.
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
                    // The agent and its Split cohort: every roster agent with
                    // this base. Never an Arbiter.
                    for (const auto& p : parts)
                        if (p.base == a.pool.name) readers.push_back(p.name);
                    break;
                case RulesPoolKind::SharedDirective:
                    readers = std::move(a.readers);
                    if (a.masked)
                        MASKING_TEMPORARY_pool_being_minted_needs_mask("Class ID", 2, a.pool.name);
                    break;
            }

            // This pool, visible to these agents. Handed over; whether it
            // stood is PoolMaintenance's business, never read here.
            pool_maintenance().mint("Class ID", 2, readers, content);
        }

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

std::vector<RulesPool>& Rules::held() {
    return held_;
}

// ===========================================================================
// SAVE — hand over what is held and forget it in the same motion; then the
// live check; then the ordinary Load branch if this is the running pipeline.
// ===========================================================================

void Rules::save() {
    // Handed over and gone. From here Rules holds nothing.
    std::string            pipeline   = std::move(held_pipeline_);
    std::string            rules_path = std::move(held_rules_path_);
    std::vector<RulesPool> pools      = std::move(held_);
    held_pipeline_.clear();
    held_rules_path_.clear();
    held_.clear();

    LoadRequest request;
    request.entries.push_back({ [rules_path, pools = std::move(pools)](const Disk& disk) {
        EntryOutcome eo;

        // The Rules content, as the ONE file it always was: every block,
        // in the order held, inside the file's own envelope.
        std::string rules = "{\n  \"rules\": [\n";
        bool first = true;
        for (const auto& p : pools) {
            if (p.kind != RulesPoolKind::Rules) continue;
            if (!first) rules += ",\n";
            rules += p.content;
            first = false;
        }
        rules += "\n  ]\n}\n";
        disk.save(rules_path, rules);           // FileLoader's own result; not read here

        // Every directive, to the path it came from.
        for (const auto& p : pools) {
            if (p.kind == RulesPoolKind::Rules) continue;
            disk.save(p.path, p.content);       // FileLoader's own result; not read here
        }

        eo.ok = true;
        return eo;
    } });

    FileLoader loader;
    const LoaderReport written = loader.load(request);
    (void)written;   // FileLoader's own report; Wellness reads it there

    // ---- the loaded pipeline: one direct read, then the ordinary load ------
    if (live_registry().pipeline_name() != pipeline) return;
    dock(pipeline, live_registry().agent_names(), Branch::Load);
}

// ===========================================================================
// CANCEL — what is held is gone. Nothing is written anywhere.
// ===========================================================================

void Rules::cancel() {
    held_pipeline_.clear();
    held_rules_path_.clear();
    held_.clear();
}

} // namespace prime
