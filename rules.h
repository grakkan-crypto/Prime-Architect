// rules.h — Rules and Directives: the loader of this class of pool, the
//           holder of its one edit session, and its saver. Nothing else
//           builds it; nothing else writes it.
//
// ===========================================================================
// WHAT THIS FILE IS
//
//   Two rulings, one class. RULES say how THIS PIPELINE functions; a
//   DIRECTIVE says how THIS AGENT functions and travels with the agent
//   across pipelines. Both land in the Rules/Directives class of pool.
//
//   This file holds nothing between calls, with ONE stated exception: for
//   the open duration of an Edit session it holds the pipeline name, the
//   content as acquired, and the real path attached to every piece of it,
//   until Save or Cancel. Outside that session the standing pools ARE the
//   record.
//
// ===========================================================================
// OFFICIAL RULINGS — FIXED POINTS. A change that would break one of these is
// wrong by definition and is raised with the user instead of made. Where a
// ruling below amends an earlier one, the amendment was deliberate and
// agreed; the earlier wording is not to be reinstated.
//
// 1. ONLY A HUMAN EVER CHANGES THIS CONTENT.
//    DISK -> VRAM -> UI -> DISK -> VRAM, and no other path. No agent, no
//    process, no automated step writes into it.
//
// 2. A LAYER FILE AND FILELOADER DOCK, THEN ACT. NEITHER ACTS ALONE.
//    This file is a closed block awaiting a pipeline name and a roster. It
//    does NOTHING with either — derives no path, opens nothing, decides
//    nothing, checks nothing — until the two are docked with FileLoader,
//    and it stays docked until the operation ends. No exceptions. The
//    validity of what it is handed is not its question: PipelineLoader and
//    the Host guarantee their inputs before this file sees them, and
//    FileLoader owns every read and write result. There is no pre-dock
//    check of any kind in this file.
//
// 3. POOLS ARE GROUPED BY WHO READS THEM, NEVER BY AGENT. THE FILE ALREADY
//    IS THAT SHAPE.
//    The Rules file on disk is a set of BLOCKS, each carrying its own
//    declared audience and its own entries, verbatim. A block IS a pool
//    once its audience is resolved against the roster. The audience field
//    routes the block and is trimmed off at mint; the rest of the block is
//    the pool, verbatim. Nothing else is done to it for any destination:
//    the pools and the UI both read this one shape. "This agent's rules"
//    is a question the Host answers by walking the blocks; it is not
//    stored and not built here. The one deliberate duplication — a rule
//    shared by everyone AND restated verbatim inside a smaller group's own
//    block for emphasis — is two blocks because the file says so, marked
//    inside the entry, never detected or reasoned about here.
//
// 4. AN ARBITER SEES EVERY RULES POOL ITS AGENT SEES, AND NO DIRECTIVE
//    POOL BUT ITS OWN.
//    Any agent among a Rules block's readers brings its Arbiter in, if the
//    roster has one for it. A directive pool's readers are the agent it
//    was fetched for and its Split cohort. An Arbiter never reads another
//    agent's functional directive; that would be a catastrophic failure.
//
// 5. THE READER LIST IS THE PERMISSIONS. IT IS DERIVED FROM THE ROSTER,
//    INSIDE THE DOCKED CALL, AND HANDED TO CREATION ONCE.
//    Arithmetic on the roster and the audience a block names, done at the
//    moment of minting, handed to PoolMaintenance with the content, and
//    never carried anywhere afterward. No lever exists afterward.
//
// 6. THE CLASS COMES DOWN ONCE, UNCONDITIONALLY, THE INSTANT THE LOAD
//    BRANCH IS ENTERED. POOLS COME UP ONE AT A TIME, EACH THE MOMENT IT IS
//    RESOLVED.
//    Teardown is PoolMaintenance's class-wide operation, told what to act
//    on as: the action, the id type, the value — Destroy, Class ID, 2. There
//    are four id types on a pool; the call says which, and it is this one.
//    It fires before a single acquired item is processed and does not wait
//    for anything to have succeeded; Wellness is told directly, as its own
//    fact, that it happened. There is no collect-everything-then-mint
//    phase: each item is resolved into its reader list and minted at once,
//    then the next. A load that produces nothing leaves the class standing
//    empty — the correct outcome, not a state to guard against.
//
// 7. EVERYTHING IS DERIVED FROM A NAME. NONE OF IT IS DERIVED HERE.
//    This file never cuts a name, builds a name, or tests a name. Every
//    naming fact it needs it asks of the naming service — the roster and
//    the question go over, the answer comes back — and it does nothing
//    with that answer but read it and compare it. The sole field this file
//    reads from a Rules block is who it is for; the rest of the block is
//    carried verbatim.
//
// 8. THIS FILE HAS NO AUTHORITY TO ABORT. EVER.
//    A read that fails — the Rules file, one directive, one shared
//    directive, it does not matter which — costs exactly that one item and
//    nothing else. The item is never seen by the processing loop; every
//    other item proceeds. Recovery is Wellness's job, not this file's. An
//    unreadable file is FileLoader's own reported failure and is not
//    restated here. The one stop that remains is not a file failure: when
//    the OS has not supplied the roots (Ruling 13) nothing that looks like
//    a path may be derived, so the operation reports that and does not
//    proceed.
//
// 9. ONE ACQUISITION, TWO BRANCHES. THERE IS NO BUILD.
//    Fed a pipeline name and a roster — handed in, never fetched — the one
//    docked operation derives every path and reads everything, identically
//    for both branches. Every result carries its real path. Then, and only
//    then, the branch: Load tears the class down and mints one item at a
//    time; Edit holds what was acquired, exactly as acquired. Reads are
//    issued one after another inside the one dock because that is the dock
//    this file has — a single entry with the whole operation inside it —
//    and each read is independent and self-tagged with its own path, so
//    nothing about order is relied upon.
//
// 10. SAVE HANDS OVER WHAT IT HOLDS, FORGETS IT IN THE SAME MOTION, THEN
//     CHECKS THE LIVE PIPELINE AND LOADS AGAIN, ITSELF.
//     The Rules content goes back as the ONE file it always was, to the one
//     path it came from; each directive goes back to its own attached path.
//     Nothing is re-derived. The held copy is gone the instant it is handed
//     to FileLoader — not after the write is confirmed. Whether the write
//     landed is FileLoader's own business, never read here. Then one direct
//     read of the live registry's pipeline name — live state, not disk —
//     says whether the pipeline just written is the one running. If it is,
//     the live registry's roster feeds the ordinary Load branch, so what
//     stands in VRAM always comes from a real file read.
//
// 11. WHAT CROSSES THE UI BOUNDARY IS KIND, NAME, PATH, AND CONTENT.
//     NOTHING ELSE.
//     Each pool says what it is, whose it is, where it came from, and
//     carries its content verbatim. The path is a fact of the file and
//     travels as data. No reader lists travel: they are derived, not data.
//     The roster does not travel: the Host already has it.
//
// 12. WELLNESS CHECK CONVENTION — UNCHANGED.
//     A bare boolean named wellness_check_<what it answers>, set the
//     instant it is answered, never combined, never handed off, never used
//     as a gate. Wellness is a contextually reasoning system: the trigger
//     is enough, and it investigates for itself. This file never assembles
//     a diagnosis for it.
//
// 13. THE OS DOES NOT EXIST YET.
//     The two roots every path hangs off are OS calls, declared and not yet
//     defined. Nothing beyond the formula is fixed until the OS supplies
//     them; nothing that looks like a real path is written anywhere.
//
// 14. MASKING IS NOT BUILT. THE CALL IS LOUD AND TEMPORARY.
//     Three of the six shared directives — Ideation and the two
//     ProblemSolving ones — are masked by default, uniformly across the
//     pool, flipped later by a system mechanism that does not yet exist.
//     Minting one of them makes one loud, explicitly temporary call saying
//     so. It is replaced outright when Masking exists; it is not a quiet
//     no-op and must not become one.
//
// ===========================================================================
// THE QUESTIONS THIS FILE ASKS OF A NAME — and where the answers go.
// Every one is asked of the naming service with the roster; none is
// worked out here.
//
//   BREAK DOWN THIS ROSTER
//                   Every agent as department, agent, and base (the first
//                   two parts as one). The base is a directive's pool name
//                   and the thing two agents share when they are one
//                   reader; department and agent are the two pieces of a
//                   directive's path. Asked once, used for both.
//
//   BREAK DOWN THIS DECLARED AUDIENCE
//                   Each name a Rules block says it is for, to its base.
//                   Every roster agent with the same base is a reader. The
//                   whole-roster name is not broken down: it is everyone.
//
//   THE SPLIT FAMILIES IN THIS ROSTER
//                   Every parent with its constituents. Anything back at
//                   all is Brainstorming eligibility. The constituents read
//                   the Split-role directive; parents and constituents
//                   read the three Brainstorming ones as each says.
//
//   THIS ROSTER'S ARBITER FOR THIS AGENT
//                   Asked for every reader of a Rules block. A name back
//                   is one more reader; nothing back is nothing.
//
//   THIS ROSTER'S ARBITERS, DETERMINISTIC AND CoT
//                   The readers of the two shared Arbiter directives, and
//                   who is exempt from the empty-directive alarm.
//
//   DIRECTIVE PATH  Assembled here from the breakdown, never derived here:
//                   <root>/<department>/<agent><directive suffix>.
//                   "Analyst-Coder"     -> <root>/Analyst/Coder-directive.json
//                   "Adept-Coder-Split" -> <root>/Adept/Coder-directive.json
//                   "Auditor"           -> <root>/Auditor/Auditor-directive.json
//
// ===========================================================================
// EMPTY IS NOT ABSENT
//
//   A directive that exists and is empty is an ordinary outcome of the UI's
//   own editing. For any agent that is not an Arbiter it is ALARM BELLS —
//   with no dispatch, the directive is all the instruction that agent gets.
//   For an Arbiter's PERSONAL directive it is silent. ABSENT is different
//   for everyone: nothing here ever deletes a file, so a missing one is
//   posted for Wellness to weigh, not judged here, and is not held or
//   pooled.
// ===========================================================================

#pragma once

#include <string>
#include <vector>

namespace prime {

// The audience name meaning "the whole roster". A real list every agent
// reads, without exception.
inline constexpr const char* kAiRulesList = "AI Rules";

inline constexpr const char* kDirectiveFileSuffix = "-directive.json";
inline constexpr const char* kRulesFileSuffix     = "-rules.json";

// The six shared, role-based directives. Singular by count — one of each in
// the whole system.
enum class SharedDirective {
    ArbiterDeterministic,       // every non-CoT Arbiter
    ArbiterCoT,                 // every CoT Arbiter
    SplitRole,                  // every constituent                 (not masked)
    Ideation,                   // every parent and constituent      (masked)
    ProblemSolvingParent,       // every parent                      (masked)
    ProblemSolvingConstituents, // every constituent                 (masked)
};

const char* shared_directive_name(SharedDirective kind);

// ---- what crosses the UI boundary, either direction (Ruling 11) -------------

enum class RulesPoolKind {
    Rules,             // one block of the pipeline's Rules file
    AgentDirective,    // one agent's (one split set's) own directive
    SharedDirective,   // one of the six
};

// Kind, name, path, content — nothing else. For a Rules block: name is the
// pipeline, path is the one Rules file, content is the block verbatim. For
// a directive: name is the agent's base or the shared name, path is that
// file, content is that file's text verbatim.
struct RulesPool {
    RulesPoolKind kind = RulesPoolKind::Rules;
    std::string   name;
    std::string   path;
    std::string   content;
};

// ---- the mechanism ----------------------------------------------------------
class Rules {
public:
    Rules() = default;

    Rules(const Rules&)            = delete;
    Rules& operator=(const Rules&) = delete;

    // This file's standing declaration of the payload categories it needs
    // (payload_categories vocabulary). Data, read directly by whoever
    // hands the payload over.
    static const std::vector<std::string> declared_needs;

    // The two things that can follow acquisition. The branch is data handed
    // to the one operation, not two operations.
    enum class Branch { Load, Edit };

    // THE ONE DOCKED OPERATION. Docks, derives every path, reads
    // everything, then branches (Ruling 9). Load: the class down, every
    // item minted one at a time. Edit: everything acquired is held, exactly
    // as acquired, until save() or cancel(). Nothing is handed back:
    // whether pools stand is not this file's to assert, and what an Edit
    // session holds is reached through held().
    void dock(const std::string& pipeline_name,
              const std::vector<std::string>& roster,
              Branch branch);

    // What the open Edit session holds. The Host edits this in place — one
    // copy, this one. Empty when no session is open.
    std::vector<RulesPool>& held();

    // SAVE. Hands what is held to FileLoader and forgets it in the same
    // motion; then the live check, then the ordinary Load branch if this is
    // the running pipeline (Ruling 10).
    void save();

    // CANCEL. What is held is gone. Nothing is written anywhere.
    void cancel();

private:
    // Held only for the open duration of an Edit session.
    std::string            held_pipeline_;
    std::string            held_rules_path_;
    std::vector<RulesPool> held_;
};

} // namespace prime
