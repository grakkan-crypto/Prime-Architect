// app_state.cpp — Frontend application state implementation (frontier)
//
// Thin by design, exactly as the umbrella pattern implies. AppState owns its
// pieces by value and exposes them by reference; the pieces carry their own
// behaviour in their own translation units. Construction binds the non-owning
// engine reference; there is nothing to allocate, map, or resolve here — the
// state pieces are empty until a pipeline load populates them.
//
// This unit exists to anchor the umbrella in the build and to hold cross-piece
// wiring if any is later needed (e.g. a pipeline-load helper that populates
// project, roster, and resident together). None is required yet — pipeline load
// orchestration is a separate concern and not part of the state definition.
//
// PATH NOTE — app_state.h only forward-declares EngineContext, so this .cpp is
// the first point that would ever need the real header, once a method is
// actually called on engine_. Layout: frontend (Prime_Architect) and backend
// (Prime_Engine) are SIBLINGS under a common parent, not nested. From this file
// (Prime_Architect/frontier/app_state.cpp), the path to the engine header is:
//     #include "../../Prime_Engine/engine_context.h"
// Two levels up (out of frontier/, out of Prime_Architect/) then into
// Prime_Engine/ — not one level, because sibling placement sits one folder
// higher than nesting would. Not included below because nothing here calls
// engine_ yet; add it at the same time the first real call does.

#include "app_state.h"

namespace prime::frontend {

// (No out-of-line definitions required at present. Accessors are inline in the
// header; state pieces define their own behaviour in their own units.)

} // namespace prime::frontend
