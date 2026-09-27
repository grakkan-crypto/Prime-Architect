// abort.cpp — Abort implementation (Prime_Architect/pipeline)

#include "abort.h"

namespace prime::frontend {

TurnId Abort::request() {
    const Turn* active = turns_.current();
    if (!active) {
        // Nothing live to abort. Visible no-op: the flag stays clear, no turn is
        // closed, and the caller gets back an invalid id so it logs "nothing
        // aborted" rather than a fabricated turn.
        return TurnId{};
    }

    TurnId aborted_id = active->id;

    // Raise the signal the inference layer watches. The same-process inference
    // layer polls/reads this atomic to tear down its in-flight call; that
    // teardown is its concern, not this file's.
    abort_flag_.store(true);

    // Close the aborted turn so the book does not leave it open. A following turn
    // (including a rebuttal that would have rebound "current") opens clean.
    turns_.close_current();

    return aborted_id;
}

void Abort::reset() {
    abort_flag_.store(false);
}

} // namespace prime::frontend
