// coder.h — THE CODER PIPELINE FILE
//
// ONE JOB: be the Coder pipeline's declaration. It holds the Coder fleet
// roster and the Coder pool table, complete and final, and it holds the one
// wake that hands them over and fires Coder's own bespoke Layer file. That
// is the whole file.
//
// It is the FIRST pipeline file built to this shape. Every pipeline file
// after it matches it exactly: the same rules below, copied in verbatim;
// one name constant; one function; the roster and the pool table as
// constant data. A pipeline file that differs in shape from this one is
// wrong by definition.
//
// ===========================================================================
// PIPELINE FILE RULES — OFFICIAL RULINGS FOR EVERY PIPELINE FILE.
//
// These are not conventions or defaults a good enough reason can override.
// A change that would break one of them is wrong by definition and is raised
// with the user instead of made. Every pipeline file carries this block,
// verbatim. One set of rules, no drift, no exceptions accreting file by file.
//
// 1. THE FORMAT OF THE PAYLOAD TO PIPELINELOADER MUST NEVER CHANGE.
//    The payload is PipelineLoader's shape (pipeline_loader.h), settled
//    once, carried identically by every pipeline. A pipeline never adds to
//    it, never bends it, never carries anything alongside it. The mask
//    ceiling is PipelineLoader's, not this file's: a pool that needs more
//    is raised with the user, never squeezed in.
//
// 2. EACH PIPELINE FILE IS RESPONSIBLE FOR WAKING ITS OWN BESPOKE FUNCTIONS
//    AND, IF RELEVANT, PROVIDING THEM PIPELINE DATA.
//    A Layer file unique to one pipeline is that pipeline's business, not
//    PipelineLoader's. The pipeline file fires it, from inside its own
//    wake, and PipelineLoader neither knows nor cares.
//
// 3. WELLNESS CHECKS ARE READ BY THE WELLNESS SYSTEM, NOT ACTED UPON BY ANY
//    OTHER FILE.
//    Every check is a bare boolean named wellness_check_<what it answers>,
//    set at the instant it is answered, never combined, never handed off,
//    never read again by this file. Wellness sees it because it is there.
//
// 4. EVERY PIPELINE FILE LOADS THE SAME WAY, EVERY TIME.
//    Same shape, same sequence, no pipeline-specific structuring of HOW it
//    fires. Only WHAT it hands over differs. Loading a pipeline is never a
//    matter of designing anything — the mechanism is the one in front of
//    you, in every sibling, identical.
//
// 5. A PIPELINE FILE NEVER WAITS ON ANYTHING IT WAKES.
//    The payload hand-over and every bespoke wake are detached and
//    unjoined. Nothing is waited on, individually or as a group.
//
// 6. A PIPELINE FILE NEVER RECEIVES DATA BACK FROM WHAT IT WAKES.
//    No report, no return value read, nothing retained after firing. What
//    happens after the hand-over is the receiver's business and Wellness's
//    fact.
//
// 7. PAYLOAD DATA IS HANDED OVER COMPLETE AND FINAL — NEVER ASSEMBLED OR
//    DECIDED AT WAKE TIME.
//    The roster and the pool table exist in their finished form before any
//    wake is ever fired. The wake copies and hands over. No build step, no
//    transformation, no decision lives in the wake path.
//
// 8. A BESPOKE WAKE GETS ITS OWN FRESH PRESENCE CHECK, ON ITS OWN THREAD,
//    SAME AS ANY OTHER WAKE IN THIS SYSTEM.
//    Never assumed alive, never skipped. Presence is answered from OUTSIDE
//    the target, before it is touched; silence is N. The check is posted,
//    and the wake fires regardless — judging is Wellness's, not this
//    file's. A bespoke wake that carries nothing has no contract check:
//    there is nothing real to compare, and a check on nothing real is a
//    manufactured failure.
//
// 9. THE FLEET ROSTER AND THE POOL TABLE ARE STATED ONCE, IN FULL, WITH
//    NOTHING INHERITED OR DEFAULTED FROM ANY OTHER PIPELINE.
//    A second pipeline is a sibling file declaring its own roster and pools
//    from scratch. There is no shared core, no default fleet, no template
//    to opt out of. Two pipelines that agree are two independent
//    declarations that happen to agree; correcting one never touches the
//    other.
// ===========================================================================
//
// THE SHAPE OF ONE WAKE
//
//   PipelineLoader calls pipeline(). Inside that one call, simultaneously:
//
//     - ProjectIngest's presence is posted (Rule 8) and ProjectIngest is
//       woken — a bare wake, nothing passed: ProjectIngest reads the active
//       project straight off the Cylon Bar itself. Detached, unjoined.
//
//     - The payload — the roster and the pool table, verbatim — is handed
//       back to PipelineLoader as the return value. No processing, no
//       decisions.
//
//   Then this file is dormant until the next load.
//
// THE CALLER hands PipelineLoader exactly two things from this header:
// kPipelineName and pipeline. Both are stated here once, so the name and
// the function can never be mismatched at the point of the call.

#pragma once

#include "pipeline_loader.h"

namespace prime::pipelines::coder {

inline constexpr const char* kPipelineName = "Coder";

PipelinePayload pipeline();

}
