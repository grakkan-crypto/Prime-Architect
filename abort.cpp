// abort.cpp — Abort implementation (Prime_Architect/pipeline)

#include "abort.h"

namespace prime::frontend {

TurnId Abort::request() {
    const Turn* active = turns_.current();
    if (!active) {

        return TurnId{};
    }

    TurnId aborted_id = active->id;

    abort_flag_.store(true);

    turns_.close_current();

    return aborted_id;
}

void Abort::reset() {
    abort_flag_.store(false);
}

}
