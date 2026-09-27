// ingestion_state.cpp — Live input surface implementation (frontier)
//
// Deliberately thin. IngestionState is plain turn data mutated in place by the
// UI and the Architect bridge; its only behaviour is the small set of
// invariants declared inline in the header (end_turn). This translation unit
// exists to anchor the header in the build and to hold any non-trivial helper
// that grows here later — question insertion ordering, autocorrect span
// accounting — without forcing those into the header. Nothing yet requires an
// out-of-line definition. (Rebuttal state lives with rebuttal — nothing of it
// is here.)

#include "ingestion_state.h"

namespace prime::frontend {

// (No out-of-line definitions required at present. See header for the inline
// invariant: IngestionState::end_turn.)

} // namespace prime::frontend
