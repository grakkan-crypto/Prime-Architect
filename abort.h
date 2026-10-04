// abort.h — Abort: user kill switch bound to the active turn
//            (Prime_Architect/pipeline)
//
// GENERIC MECHANISM. Pipeline-agnostic. Every pipeline's turns are abortable the
// same way; nothing here names an agent or a pipeline.
//
// WHAT ABORT TARGETS
//   Always the currently ACTIVE turn — the most-recently-opened live turn, which
//   is what TurnBook::current() reports. It is not a per-turn array of
//   controllers the caller has to maintain and index. There is one live target
//   at a time; Abort hits it.
//
// REBINDING ACROSS A REBUTTAL CHAIN
//   A rebuttal opens a new turn that becomes "current" (turn.h). When it does,
//   Abort's target rebinds to it automatically — because Abort reads "current"
//   live, it does not hold a stale handle to whatever was active a moment ago. So
//   if the original turn is still producing output when a rebuttal opens, Abort
//   now targets the rebuttal; when that rebuttal ends and the next is opened,
//   Abort follows again. A chain of rebuttals can keep Abort perpetually live —
//   that is a human-pacing limit, not a system one, and needs no controller
//   bookkeeping to support.
//
// WHAT ABORT IS NOT
//   Not a watchdog, not a stream halt an Arbiter raises, not a correction
//   mechanism. Those do not exist in this system (the annotate-live model
//   replaced them). Abort is exactly one thing: the user deciding to stop the
//   active turn. It signals; it does not reach into an agent's generation itself.
//
// THE SIGNAL, NOT THE PLUMBING
//   This file owns the DECISION of what to abort and the raising of the signal.
//   The actual cancellation of an in-flight engine call is the inference layer's
//   concern (it holds whatever cancellation primitive the same-process engine
//   exposes). Abort here flips the shared abort flag the inference layer already
//   watches (app_state's abort_requested, an atomic) and marks which turn was
//   aborted so bookkeeping stays honest. It does not itself know how a call is
//   torn down.

#pragma once

#include <atomic>

#include "turn.h"

namespace prime::frontend {

class Abort {
public:
    Abort(TurnBook& turns, std::atomic<bool>& abort_flag)
        : turns_(turns), abort_flag_(abort_flag) {}

    TurnId request();

    bool raised() const { return abort_flag_.load(); }

    void reset();

private:
    TurnBook&          turns_;
    std::atomic<bool>& abort_flag_;
};

}
