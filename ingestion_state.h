// ingestion_state.h — Live input surface (frontier)
//
// The state of a turn while it is being composed — before commit lifts the
// output gate. This is the one piece of frontend state that a pipeline agent
// (Architect-Ingest) and the UI both write to concurrently, which is exactly
// why it is shared state and not a UI widget's private value.
//
// WHO WRITES WHAT (SPEC_Live_Input_Processing §3.1, §4; HANDOVER_Session22 §3.2)
//   composition  — the UI writes keystrokes/transcription; Architect-Ingest
//                  writes autocorrect back INTO the same buffer in place. Two
//                  writers, one buffer, live. Not a suggestion overlay — the
//                  text changes under the user as meaning develops.
//   questions    — Architect-Ingest writes; the UI renders and, on answer,
//                  the entry is removed. Presence IS visibility: an empty list
//                  is nothing shown above the input box. There is no separate
//                  "visible" flag and no answered-but-retained state.
//   turn_active  — the flush gate. True from turn open; while true the
//                  Architect does not evict resident context. See the note on
//                  the field for the overlap rule.
//
// WHAT THIS IS NOT
//   Not history. This is bounded turn state, scoped to a single intent, cleared
//   on turn close (not per-commit, not per-rebuttal — HANDOVER_Session22 §3.6).
//   Cross-turn carry is resident_context, a separate concern with a separate
//   owner.
//
// GROWTH
//   The vocal interface is the fastest-growing surface in the frontend
//   (SPEC_Prime_Vocal_Interface). Inflection maturity, per-user calibration,
//   and passive-context handling all land here over time. This header is scoped
//   to hold that growth without the rest of the state struct having to know.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prime::frontend {

struct IngestQuestion {
    uint32_t    number = 0;
    std::string text;
};

struct IngestionState {

    std::string composition;

    std::vector<IngestQuestion> questions;

    bool turn_active = false;

    bool voice_mode = false;
    bool mic_on     = false;
    bool tts_on     = false;

    void end_turn() {
        composition.clear();
        questions.clear();
        turn_active = false;
    }
};

}
