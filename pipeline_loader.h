// pipeline_loader.h — THE PIPELINE TRIGGER POINT
//
// PipelineLoader is NOT a Layer file. It is its own standalone, generic
// mechanism — the single point of contact for any pipeline load. The Layer
// Rules live in LAYER_RULES.md; none of them govern this file.
//
// ONE JOB: run the pipeline function it is handed, take the payload that
// comes back, and hand each part of it to the files that need it. Nothing
// else. It does not read disk. It does not parse anything. It does not know
// any pipeline by name.
//
// ===========================================================================
// OFFICIAL RULINGS — PIPELINELOADER'S OWN FIXED POINTS.
//
// These are not conventions or defaults a good enough reason can override.
// A change that would break one of them is wrong by definition and is raised
// with the user instead of made.
//
// 1. GENERIC FOREVER. NEVER EDITED FOR A PIPELINE.
//    This file contains no pipeline's name, no pipeline-specific branch, and
//    no knowledge of what any pipeline does. Adding a new pipeline to the
//    system changes what a caller hands in — never one character of this
//    file. A pipeline name appearing anywhere in this file is itself the
//    proof that something has drifted into the wrong place.
//
// 2. THE FUNCTION IS HANDED IN. NOTHING IS LOOKED UP OR DERIVED.
//    Whatever decides a pipeline must load already knows which pipeline it
//    means, so it hands the pipeline's own function in directly, alongside
//    the pipeline's name, in the same call. Both come from the pipeline
//    file's own header, where each is stated exactly once. The name is
//    carried for the files downstream that need it; this file does nothing
//    with it itself. No table, no registry, no name-to-function resolution,
//    no derivation exists here.
//
// 3. NOTHING IS HELD BETWEEN CALLS.
//    No member state, no cache, no resolved record, no "last loaded"
//    anything. Each call arrives complete and leaves nothing behind.
//
// 4. ONE WAIT, AND ONLY ONE: THE PIPELINE FUNCTION ITSELF.
//    The payload it returns is the input to everything downstream — a
//    genuine data dependency, the one stated functional reason to wait.
//    Nothing else in this file is ever waited on: not the downstream wakes,
//    not the store, not anything those go on to do.
//
// 5. EVERY CHECK IS FRESH. NOTHING IS EVER CACHED OR ASSUMED.
//    This system edits and rebuilds parts of itself while running, near
//    permanently, between rare restarts. A target present on the last call
//    can be absent on this one. Presence and contract are asked again, from
//    scratch, on every single call — silence is an answer, and the answer
//    is N, immediately, never a wait-and-hope.
//
// 6. SHAPE IS CHECKED ONCE, AT RECEIPT — AND SHAPE IS THE ONE CHECK THAT
//    GATES.
//    The one moment the payload's shape is checked is the moment it arrives
//    from the pipeline function: each category present, and each category
//    in its correct structural format — one presence check and one format
//    check per category, never combined, each posted for Wellness. If any
//    of them is N, the load STOPS THERE. Nothing downstream is fired.
//
//    WHY THIS GATES WHEN NOTHING ELSE IN THIS FILE DOES. Not because the
//    data is pre-authored, and not because it is load-bearing — both of
//    those describe half the checks in this system, including the four
//    wakes below, and neither is the reason. The reason is what THIS FILE
//    IS: the single point where an entire pipeline's identity is
//    established, in one call. Before this call every downstream system is
//    generic — the same files, the same behaviour, the same agents,
//    whatever the pipeline. This payload is what makes the system THIS
//    pipeline; everything after it is a fragment of it. Nowhere else in
//    the system does so much rely on one file load being correct — boot
//    alone compares, and even boot has Aux standing behind it. Nothing
//    stands behind this. A malformed payload let through here is not one
//    subsystem's error to fix while its siblings carry on; it is the
//    foundation every subsequent action builds on, and every movement
//    after this moment can be pure cascade. So it stops, here, at the one
//    point where stopping is cheaper than any motion.
//
//    WHAT THIS IS NOT. It is not "loads are dangerous" — argue that and a
//    single model failing to load cancels a whole pipeline, which is the
//    WRONG shape, especially when a reload fixes it; and another
//    pipeline's Rules failing to load would halt everything, which is
//    wrong by default. Those are individual subsystems recovering on
//    their own terms while their siblings keep moving — the shape the
//    rest of this system is built on. The four wakes below stay ungated
//    for exactly that reason: a target absent or mismatched today can be
//    present on the next call, and everything else can be setting up
//    while that one error is worked out.
//
//    THE TEST, FOR ANY CHECK THAT WANTS TO FOLLOW SUIT: it must sit at a
//    point where the ENTIRE system's identity rests on one file load
//    being correct, with nothing standing behind it if it is wrong. That
//    is not a class of check — it is, outside boot, this one. A check
//    that cannot honestly make that claim stays posted-only. Having this
//    exception properly scoped is one thing; using it as an excuse to do
//    the same elsewhere without that scope is exactly what this wording
//    exists to prevent.
//
//    The shape checks are deliberately structural — a roster absent is
//    knowable; a roster short is not; bits wider than the pool allows is
//    knowable; what the bits MEAN is not this file's business. Content-
//    level judgement (sufficiency, meaning) belongs to the files that own
//    the content, never here. Shape is never re-checked downstream.
//
// 7. DOWNSTREAM IS FIRE-AND-FORGET. NO JOINS. OWN DATA PER WAKE.
//    Rules, Temperature, ModelWeights, and the store all fire together the
//    moment the payload is in hand, and none is waited on — not
//    individually, not as a group. Each fired wake carries its OWN data,
//    owned outright, free to process as it wishes and discarded when it
//    finishes — never a reference back into this file's call, which may be
//    long gone.
//
// 8. NO REPORT. EVER. TO ANYONE.
//    The caller is a messenger — a button, a tab switch, an automation. It
//    has no possible reaction to an outcome, so an outcome handed to it is
//    noise, not information. This call returns nothing. Whether the load
//    worked is a fact WellnessSystem holds from the Wellness Checks; the
//    thing that acts on failure IS Wellness, and anything that wants to
//    know asks Wellness.
//
// 9. WELLNESS CHECK CONVENTION — UNCHANGED AND ABSOLUTE.
//    Every check is a bare boolean named wellness_check_<what it answers>,
//    set at the exact instant it is answered, never combined, never held
//    for a sibling, never handed off. WellnessSystem sees it because it is
//    already there. The gating checks are read once, for the gate, and
//    never again.
//
// 10. THE PAYLOAD FORMAT NEVER CHANGES.
//    The shape below is the system-wide contract, settled once. Every
//    pipeline carries it; this file reads it identically regardless of
//    which pipeline produced it. It is ONE struct, declared here and
//    nowhere else — a pipeline cannot carry a wider, narrower, or different
//    version. A pipeline that needs something the payload does not carry
//    has a bespoke need, and bespoke needs are the pipeline file's own
//    business (see the pipeline file rules) — they never become a new
//    payload category. Raising the mask ceiling below is an edit to this
//    header and nothing else already built; it is a system-wide fact, not
//    a per-pipeline one.
// ===========================================================================
//
// THE SHAPE OF ONE LOAD
//
//   1. HANDED IN — pipeline name plus the pipeline's own function, both
//      supplied by the caller from the pipeline file's header. Name missing
//      or function missing: that check is N and nothing further happens,
//      because there is nothing to run. The function's presence IS the
//      answer to "is anybody there": it is the only handle this file has
//      on a pipeline, decided by the caller before the pipeline is touched.
//
//   2. THE ONE WAIT — the pipeline function runs and returns the payload:
//      the fleet roster and the pool table, pre-shaped, complete, final.
//      Nothing is parsed here. The pipeline's own bespoke wake fires inside
//      that function, on its own clock — this file has no knowledge of it
//      and no business with it.
//
//   3. SHAPE AT RECEIPT — per category, one presence check and one format
//      check, each its own boolean, each posted. Any N stops the load
//      (Ruling 6).
//
//   4. CROSS-CHECK — the roster and the pool table agree with each other:
//      every agent in any pool is in the roster; every agent in the roster
//      is in at least one pool. Two booleans, posted, gating nothing. This
//      is the one job PoolMatrix had left, moved here because this is the
//      only moment both lists sit together.
//
//   5. FIRE — for each target (Rules, Temperature, ModelWeights, the
//      LiveRegistry store), two fresh boundary checks are posted, then the
//      wake fires — regardless. The checks mean nothing to this file: it
//      reads neither, decides nothing on either.
//        - presence: is this file here, is this file running — answered
//          from outside the target, instantly; silence is N.
//        - contract: "here is what I am handing you — is this what you
//          need?" against the target's CURRENT declaration. The declaration
//          is the authority, not this file's memory of it.
//      Each wake fires with its own owned data and sets its actioned check
//      the instant it resolves, on its own clock.
//
//   6. RETURN — immediately, the moment everything is fired. Nothing
//      retained, nothing reported, nobody waited on.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace prime {

// ===========================================================================
// THE PAYLOAD — what EVERY pipeline returns. Two categories, settled:
//
//   ROSTER — every agent the pipeline fields. A flat list of agent names,
//   stated once, in full. Its own list on purpose: deriving it from the pool
//   table is trivial for a search and a nightmare for a human check, and a
//   newborn pipeline needs a clean fleet list for adding to the fleet.
//
//   POOL TABLE — every pool the pipeline declares, and for each pool: how
//   many masks it has, the trigger bits of every mask, and every agent with
//   access, each with its access-and-mask bits. Access, permissions and
//   masking are ONE table, not three; an agent's presence in a pool's list
//   IS its read access.
//
// STORAGE — bits are native machine words, because the system reads this
// table constantly and a word is what the system reads. In the pipeline
// file they are written as binary literals at their full width, aligned in
// a column, so the human-readable layout is the source itself and no
// translation step exists anywhere. Every literal is written at full width:
// a short literal is the same number but it is not the same line, and the
// column is what a human checks.
//
// BIT ORDER — written left to right in table order; the leftmost written
// digit is the highest bit. An agent's bits are the 2 access bits followed
// by 3 bits per mask, in mask order. A pool's triggers are 3 bits per mask,
// in mask order. What each digit MEANS is LiveRegistry's key, not this
// file's.
//
// STRUCTURAL FORMAT (this is exactly what the format checks answer):
//   - every pool name and every agent name is non-empty
//   - mask_count is at most kMaxMasksPerPool
//   - mask_triggers fits in 3 × mask_count bits (no bit set above that)
//   - every agent's bits fit in 2 + 3 × mask_count bits (no bit set above)
// ===========================================================================

inline constexpr std::uint32_t kMaxMasksPerPool = 10;

struct PoolAgent {
    std::string   agent;
    std::uint32_t bits;
};

struct PoolDeclaration {
    std::string            name;
    std::uint32_t          mask_count;
    std::uint32_t          mask_triggers;
    std::vector<PoolAgent> agents;
};

struct PipelinePayload {
    std::vector<std::string>     roster;
    std::vector<PoolDeclaration> pools;
};

// ===========================================================================
// THE HANDED-IN FUNCTION — the pipeline's own wake. The caller supplies it
// directly; this file never resolves or stores it. A bare wake: nothing is
// passed in, because the pipeline already knows everything about itself.
// Out: the payload. Its own bespoke onward wake fires inside this call,
// invisibly to this file.
// ===========================================================================
using PipelineFn = std::function<PipelinePayload()>;

// ===========================================================================
// THE CATEGORY NAMES — the fixed vocabulary of the payload contract. Used by
// the contract check here and by every target's declaration of what it
// needs. One vocabulary, stated once. "pipeline_name" names the identity
// itself, which travels alongside the payload.
// ===========================================================================
namespace payload_categories {
    inline constexpr const char* kPipelineName = "pipeline_name";
    inline constexpr const char* kRoster       = "roster";
    inline constexpr const char* kPools        = "pools";
}

// ===========================================================================
// THE LOADER
//
// No members, no construction arguments, nothing held. The class exists so
// the mechanism has one name; structurally it CANNOT retain anything.
// ===========================================================================
class PipelineLoader {
public:
    PipelineLoader() = default;

    PipelineLoader(const PipelineLoader&)            = delete;
    PipelineLoader& operator=(const PipelineLoader&) = delete;

    void load(const std::string& pipeline_name, const PipelineFn& pipeline_fn);
};

}
