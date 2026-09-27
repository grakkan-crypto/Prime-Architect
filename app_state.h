// app_state.h — Frontend application state (frontier)
//
// The single object every frontend module holds a reference to. It is the
// counterpart to EngineContext on the frontend side: a thin OWNER that holds the
// narrow, single-concern state pieces and hands references to the modules above
// it. It defines none of their internals — each piece owns its own header, the
// way EngineContext owns MemoryAllocator, KvPoolAllocator, SlotRegistry, etc. as
// separate concerns rather than one monolith.
//
// SAME-PROCESS, NO TRANSPORT
//   The native frontend shares one process and address space with the engine
//   (SPEC_Connection_Architecture §3.1). AppState is not a client that talks to
//   the engine over a channel — it holds a direct reference to EngineContext and
//   a call into the engine is a method call. There is no IEngine, no
//   IDiagnostics, no shim, no HTTP (HANDOVER_Session24 §3.1, §3.3, §3.4). The
//   inference layer writes into the diagnostics/journey logs here directly.
//
// WHAT IT OWNS
//   ingestion  — live input surface (composition, questions, turn gate, TTS)
//   temperatures — authored per (team, agent), UI-owned; engine holds none
//   diagnostics/journey — direct-write panel backing data
//   project    — pipeline identity + active project/file
//   resident   — the Architect's cross-turn carrier (frontend custodies)
//
//   Plus the one loose field that does not warrant its own pair: the Auxiliary
//   socket path used to reach the Watchdog for undo/backup
//   (HANDOVER_Session24 §3.8; SPEC_Prime_Recovery_Watchdog §4). Undo reads the
//   Watchdog replacement log and requests restore through the Auxiliary; this
//   holds the path to reach it, nothing more — no undo pointer, no backup group,
//   no preview endpoint (the old pointer model is gone).
//
// WHAT IT DOES NOT DO
//   No orchestration, no policy. It is the container the frontend reaches through
//   to get at its state, exactly as EngineContext is for the engine's
//   foundation. The reference to EngineContext is non-owning: the engine outlives
//   any particular AppState wiring and is constructed elsewhere.

#pragma once

#include "ingestion_state.h"
#include "temperature_store.h"
#include "diagnostics_state.h"
#include "project_state.h"
#include "resident_context.h"

#include <atomic>
#include <string>

namespace prime {
class EngineContext; // non-owning reference; engine is constructed elsewhere.
                      // Forward-declared only — no path dependency here. When a
                      // .cpp needs the real definition: frontend (Prime_Architect)
                      // and backend (Prime_Engine) are SIBLING folders, so the
                      // real include from anywhere in frontier/ is
                      // "../../Prime_Engine/engine_context.h" (two levels up,
                      // not one — sibling, not nested).
}

namespace prime::frontend {

class AppState {
public:
    // Bound to the engine it shares a process with. The reference is non-owning
    // and must outlive this AppState.
    explicit AppState(prime::EngineContext& engine) : engine_(engine) {}

    AppState(const AppState&)            = delete;
    AppState& operator=(const AppState&) = delete;

    // Direct engine access — a method call, not a transport. Rules, pipeline
    // load/unload, and generation go through this reference. No interface sits
    // between the frontend and the engine.
    //
    // Temperature is NOT among them: it never crosses this boundary in either
    // direction. It is held above, in this object, and read from there.
    prime::EngineContext& engine() { return engine_; }

    // State pieces. Each is a distinct concern with its own header; AppState only
    // owns and exposes them.
    IngestionState&   ingestion()    { return ingestion_; }

    // Authored temperatures. UI-OWNED — this is where they live, and the only
    // place. The engine stores none, mirrors none, and is never asked for one.
    // Replaces the old AgentRoster, whose two-values-per-agent model was wrong.
    TemperatureStore& temperatures()  { return temperatures_; }
    DiagnosticsLog&   diagnostics()   { return diagnostics_; }
    JourneyLog&       journey()       { return journey_; }
    ProjectState&     project()        { return project_; }
    ResidentContext&  resident()       { return resident_; }

    // The currently-addressed agent (the frontend's human-facing selection).
    const std::string& current_agent() const { return current_agent_; }
    void set_current_agent(const std::string& name) { current_agent_ = name; }

    // Abort flag for the in-flight turn. Held here because the inference layer
    // takes it as std::atomic<bool>& (inference.h) and checks it per token; the
    // UI raises it to abort. Lives on the umbrella because it is turn-scoped
    // process state, not a property of any single piece above.
    std::atomic<bool>& abort_requested() { return abort_requested_; }

    // Path to reach Prime Auxiliary / the Watchdog for undo and backup. Set at
    // startup/pipeline load; read by the undo path. See header note.
    const std::string& auxiliary_socket_path() const { return auxiliary_socket_path_; }
    void set_auxiliary_socket_path(const std::string& path) {
        auxiliary_socket_path_ = path;
    }

private:
    prime::EngineContext& engine_;   // non-owning

    IngestionState   ingestion_;
    TemperatureStore temperatures_;
    DiagnosticsLog   diagnostics_;
    JourneyLog       journey_;
    ProjectState     project_;
    ResidentContext  resident_;

    std::string       current_agent_;
    std::atomic<bool> abort_requested_{false};
    std::string       auxiliary_socket_path_;
};

} // namespace prime::frontend
