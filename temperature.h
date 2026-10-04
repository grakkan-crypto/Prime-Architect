// temperature.h — Temperature: the loader of the live temperature set, the
//                 holder of its one edit session, and its saver. Nothing else
//                 builds it; nothing else writes it.
//
// ===========================================================================
// WHAT THIS FILE IS
//
//   One value per controllable generating position in the loaded pipeline.
//   Temperature is generation configuration and nothing else: it is never
//   pooled, never masked, never resident. The values live on LiveRegistry;
//   this file is the one thing that puts them there, and the one thing that
//   reads and writes the per-pipeline file they are kept in.
//
//   This file holds nothing between calls, with ONE stated exception: for the
//   open duration of an Edit session it holds the pipeline name, the rows as
//   acquired with the real path attached to them, and the expected set it
//   worked out — until Save or Cancel. Outside that session LiveRegistry IS
//   the record.
//
// ===========================================================================
// OFFICIAL RULINGS — FIXED POINTS. A change that would break one of these is
// wrong by definition and is raised with the user instead of made. Where a
// ruling below amends an earlier one, the amendment was deliberate and
// agreed; the earlier wording is not to be reinstated.
//
// 1. ONLY A HUMAN EVER CHANGES A VALUE.
//    DISK -> LIVEREGISTRY -> UI -> DISK -> LIVEREGISTRY, and no other path.
//    No agent, no process, no automated step writes a temperature.
//
// 2. THIS FILE AND FILELOADER DOCK, THEN ACT. NEITHER ACTS ALONE.
//    This file is a closed block awaiting a pipeline name and a roster. It
//    does NOTHING with either — derives no path, opens nothing, decides
//    nothing, checks nothing — until the two are docked with FileLoader,
//    and it stays docked until the operation ends. No exceptions. The
//    validity of what it is handed is not its question: PipelineLoader and
//    the Host guarantee their inputs before this file sees them, and
//    FileLoader owns every read and write result. There is no pre-dock
//    check of any kind in this file.
//
// 3. THE CONFIG IS READ DIRECTLY, AND ANSWERS ONE QUESTION.
//    The system-wide agent config is data this file consumes and never
//    writes. It is a plain read — the same standing as reading a value off
//    LiveRegistry — never docked, never pooled, never held, and nothing is
//    asked of it. It is consulted for one kind of name only: a whitelisted
//    name that is in no Split family. For that name it answers exactly one
//    thing: is this a team, and if so how many of its members are
//    generating. Nothing else is taken from it. No member's name is lifted
//    from it. Nothing Split-related — not a parent, not a constituent — is
//    ever looked for in it, because nothing Split-related needs it.
//
// 4. NOTHING ABOUT A NAME IS WORKED OUT HERE.
//    This file never cuts a name, builds a name, or tests a name.
//    Department, whitelist membership, the Split families, who is an
//    Arbiter — every naming fact is asked of the naming service, with the
//    list in hand, and the answer is read and nothing more. The one policy
//    sitting on top of a naming answer is this file's own and stays here:
//    of the Architect department, one member only is controllable.
//
// 5. EVERY WHITELISTED NAME IS KEPT. NO EXCEPTIONS. EVER.
//    A whitelisted name in no Split family is a config agent or a config
//    team. Any variation between the pipeline's roster and the config — a
//    name in one and not the other — is posted for Wellness, which is
//    already catching the same mismatch off ModelWeights' load and works to
//    resolve it. The name is NOT ignored: it stands as a standalone entry
//    at default, so there is always a value to run on and a row to edit.
//
// 6. A TEAM'S ROWS ARE NUMBERED, NEVER NAMED.
//    An agent name is not one identity across the roster. The same name
//    standing solo and sitting inside a team are two structurally different
//    things doing two different functions, and a member's name written
//    under a team would claim an identity it does not have. The team name
//    is the identity that matters for that function. So a team's generating
//    positions are rows under the team's name, numbered 1..N, and carry no
//    agent name at all. Only a solo agent's row carries a name — its own.
//
// 7. A SPLIT CONSTITUENT IS NEVER A ROW. ITS NAME SITS ON ONE OF ITS
//    PARENT'S.
//    A Split family is read off the roster by the naming service: the
//    parent, and its constituents. The parent is a team — one numbered row
//    per generating position — and the number of generating positions IS
//    the number of constituents: always, mechanically, and never counted
//    from anywhere else. Each constituent pairs onto exactly one of those
//    rows, one to one, and its name lands on that row as a second
//    identifier the row answers to. A lookup by the position number and a
//    lookup by the Split name find the same one row and the same one value.
//    There is nothing to keep in step because there is only one thing.
//    Which constituent lands on which position changes nothing — pairing is
//    by count alone. Numerals lining up (Split on 1, Split2 on 2) is
//    preferred because it reads neater, and for no other reason.
//
// 8. NO VALIDATION. NOTHING IS RANGE-CHECKED, CLAMPED, OR REFUSED HERE.
//    The UI is the only source of a value and already constrains it. A
//    value goes to disk and to LiveRegistry exactly as handed.
//
// 9. THIS FILE HAS NO AUTHORITY TO ABORT. EVER.
//    A read that fails costs exactly that one thing and nothing else. A
//    config that cannot be read is posted, and this dock proceeds with no
//    team structure known for the names that needed it: every whitelisted
//    name in no Split family stands as a standalone entry at default, the
//    file is still read, values still land, and Wellness or a manual reset
//    of the temperatures puts it right. Split families are untouched by
//    that — they never needed the config. An unreadable temperature file is
//    FileLoader's own reported failure and is not restated here. The one
//    stop that remains is not a file failure: when the OS has not supplied
//    the root (Ruling 14) nothing that looks like a path may be derived, so
//    the operation reports that and does not proceed.
//
// 10. ONE ACQUISITION, ONE RECONCILE, TWO BRANCHES. THERE IS NO BUILD.
//    Fed a pipeline name and a roster — handed in, never fetched — the one
//    docked operation asks the naming service, reads the config, works out
//    the expected set, reads the file, and reconciles, identically for both
//    branches: every expected row takes its saved value and marker where
//    the file has a match and stands at default, marked default, otherwise.
//    Then, and only then, the branch, which decides ONE thing: whether a
//    saved row nobody expected survives. Load does not carry it and commits
//    the result onto LiveRegistry, replacing the live set outright. Edit
//    keeps it, exactly as saved, and holds everything until save() or
//    cancel(). Removing that row is the Host's call, made with the user,
//    never this file's.
//
// 11. SAVE HANDS OVER WHAT IT HOLDS, FORGETS IT IN THE SAME MOTION, THEN
//     CHECKS THE LIVE PIPELINE AND LOADS AGAIN, ITSELF.
//     The rows go back as the ONE file they always were, to the path
//     attached to them when the session was docked. Nothing is re-derived.
//     Every row written has its default marker cleared in that same write —
//     nothing reaches disk before Save, so the write IS the acknowledgement,
//     and there is no separate acknowledge step. The held copy is gone the
//     instant it is handed to FileLoader — not after the write is
//     confirmed. Whether the write landed is FileLoader's own business,
//     never read here. Then one direct read of the live registry's pipeline
//     name — live state, not disk — says whether the pipeline just written
//     is the one running. If it is, the live registry's roster feeds the
//     ordinary Load branch, so what is live always comes from a real file
//     read and never from the edit session's copy carried over by hand.
//
// 12. WHAT CROSSES THE UI BOUNDARY IS THE ROWS WITH THEIR PATH, AND THE
//     EXPECTED SET. NOTHING ELSE.
//     The rows say team, the identifiers the row answers to, value, marker,
//     and carry the path they came from as data. A Split name rides ON its
//     row, as one of that row's identifiers; no separate list of Split
//     names travels, because the row already says it. The expected set says
//     who should have a value, at default — eligibility, for labelling; it
//     never adds to, removes from, or alters the rows. The roster does not
//     travel: the Host already has it. Cancel, or docking Edit again,
//     destroys the held copy and nothing else.
//
// 13. WELLNESS CHECK CONVENTION — UNCHANGED.
//     A bare boolean named wellness_check_<what it answers>, set the
//     instant it is answered, never combined, never handed off, never used
//     as a gate. Wellness is a contextually reasoning system: the trigger
//     is enough, and it investigates for itself. This file never assembles
//     a diagnosis for it.
//
// 14. THE OS DOES NOT EXIST YET.
//     The root every temperature path hangs off, and the path of the one
//     system config, are OS calls, declared and not yet defined — loud
//     stubs. Nothing beyond the formula is fixed until the OS supplies
//     them; nothing that looks like a real path is written anywhere.
//
// ===========================================================================
// THE EXPECTED SET — worked out fresh on every dock, for both branches.
//
//   THE WHITELIST     The naming service is handed the roster and the
//                     department list below and hands back the names in
//                     those departments. Of the Architect department, the
//                     one member named below survives; every other
//                     Architect-family name is dropped here, by this file's
//                     own policy. What survives is the whitelist. Every name
//                     on it produces something below. None is dropped.
//
//   SPLIT FAMILY      The naming service is handed the whitelist and hands
//                     back every Split family in it: a parent and its
//                     constituents. Per family: one row per constituent,
//                     team = the parent's name, numbered 1..N. The i-th
//                     constituent's name is placed on row i as its second
//                     identifier. The constituent is not a row. Nothing
//                     here touches the config.
//
//   EVERYONE ELSE     A whitelisted name in no Split family is looked up in
//                     the config, directly:
//
//     TEAM            A config team -> one row per generating member, team
//                     = the name, numbered 1..N, where N is the member
//                     count less the Arbiters among them (the naming
//                     service says which). No member's name is used.
//
//     SOLO            A config agent -> one row, team empty, its own name.
//
//     NOT IN CONFIG   Neither -> one row, team empty, its own name, and
//                     Ruling 5 fires.
//
// THE ROW
//   Team; then the identifiers this one row answers to, its own identity
//   first — a solo agent's name, or a team position's number — followed by
//   any Split name paired onto it; then value; then the default marker.
//   A saved row is matched to an expected row on team and first identifier
//   only: the value belongs to the position, never to the Split name
//   riding on it.
//
// THE FILE
//   <root>/<pipeline>-temp.bin. Binary, length-prefixed, no parser: a row
//   count, then per row: team, identifier count, each identifier, value as
//   a native double, default flag. This system wrote it and reads it back
//   into the same layout. A file that ends mid-row yields the rows read
//   before the fault; the fault is posted.
// ===========================================================================

#pragma once

#include "live_registry.h"

#include <string>
#include <vector>

namespace prime {

inline const std::vector<std::string> kTemperatureDepartments = {
    "Adept", "Artist", "Artisan", "Aperture", "Architect",
};

inline constexpr const char* kTemperatureArchitectMember = "Architect-Ingest";

inline constexpr double      kTemperatureDefault    = 0.7;
inline constexpr const char* kTemperatureFileSuffix = "-temp.bin";

struct TemperatureSession {
    std::string                   pipeline;
    std::string                   path;
    std::vector<LiveTemperature>  entries;
    std::vector<LiveTemperature>  expected;
};

class Temperature {
public:
    Temperature() = default;

    Temperature(const Temperature&)            = delete;
    Temperature& operator=(const Temperature&) = delete;

    static const std::vector<std::string> declared_needs;

    enum class Branch { Load, Edit };

    void dock(const std::string& pipeline_name,
              const std::vector<std::string>& roster,
              Branch branch);

    TemperatureSession& held();

    void save();

    void cancel();

private:

    TemperatureSession held_;
};

}
