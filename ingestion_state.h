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

// One unanswered clarifying question, surfaced by Architect-Ingest during
// composition. Numbered so the UI can present an ordered list; the number is
// the Architect's, stable for the life of the question. Removal from the list
// is the only "answered" signal — there is no bool here by design.
struct IngestQuestion {
    uint32_t    number = 0;
    std::string text;
};

// The live composition buffer plus the concurrent question list and the turn
// gate. Held as plain data — the modules that act on it (UI, the inference
// layer, the Architect bridge) mutate it directly; this struct enforces no
// policy beyond the small invariants its own methods carry.
struct IngestionState {
    // --- LIVE INPUT (two writers: UI + Architect-Ingest autocorrect) ---
    // The text currently being composed. The UI appends input; Architect-Ingest
    // rewrites spans in place. Whatever is here is the best available signal at
    // any instant — partial input is expected, not an error state.
    std::string composition;

    // --- CONCURRENT QUESTIONS (Architect writes, UI renders, answer removes) ---
    // Ordered, unanswered. Empty when there is nothing to ask. The goal state at
    // commit is empty.
    std::vector<IngestQuestion> questions;

    // --- TURN GATE (flush control) ---
    // True while a turn is open. The Architect withholds resident-context
    // eviction while true.
    //
    // OVERLAP RULE (the simple form, per your ruling): input+rebuttal can leave
    // two turns momentarily open. This flag is not a per-turn identity and not a
    // count — it is simply "is any turn open." Already-emitted output from the
    // earlier turn migrates to shared context; no flush happens while this is
    // true. Flush is withheld until a genuine turn end with no incoming input,
    // at which point the Architect flushes under its standard ruling. Keeping
    // this a plain bool is deliberate: a counter would invite the desync the
    // legacy file already suffered from.
    bool turn_active = false;

    // --- VOICE MODE ---
    // Voice mode is a session state, not a per-prompt toggle
    // (SPEC_Prime_Vocal_Interface §5.1). mic and tts are the two independently
    // controllable dimensions (§3): all four combinations are valid. These carry
    // the live toggle state only; the stop button and indicators are UI concerns
    // that read these, not additional state.
    bool voice_mode = false;   // session is in voice mode at all
    bool mic_on     = false;   // microphone input enabled
    bool tts_on     = false;   // TTS output enabled

    // Clear bounded turn state. Called on turn close ONLY — never per-commit,
    // never per-rebuttal. Voice/mic/tts session toggles are NOT cleared here:
    // they outlive a single turn.
    void end_turn() {
        composition.clear();
        questions.clear();
        turn_active = false;
    }
};

} // namespace prime::frontend
