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
class EngineContext;

}

namespace prime::frontend {

class AppState {
public:

    explicit AppState(prime::EngineContext& engine) : engine_(engine) {}

    AppState(const AppState&)            = delete;
    AppState& operator=(const AppState&) = delete;

    prime::EngineContext& engine() { return engine_; }

    IngestionState&   ingestion()    { return ingestion_; }

    TemperatureStore& temperatures()  { return temperatures_; }
    DiagnosticsLog&   diagnostics()   { return diagnostics_; }
    JourneyLog&       journey()       { return journey_; }
    ProjectState&     project()        { return project_; }
    ResidentContext&  resident()       { return resident_; }

    const std::string& current_agent() const { return current_agent_; }
    void set_current_agent(const std::string& name) { current_agent_ = name; }

    std::atomic<bool>& abort_requested() { return abort_requested_; }

    const std::string& auxiliary_socket_path() const { return auxiliary_socket_path_; }
    void set_auxiliary_socket_path(const std::string& path) {
        auxiliary_socket_path_ = path;
    }

private:
    prime::EngineContext& engine_;

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

}
