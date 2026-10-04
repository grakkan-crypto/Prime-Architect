// aether_fetch.h — Quarantined external fetch, initiated by the agent that needs it
//
// ONE JOB: an Analyst-department agent has recognised, mid-reasoning, that it
// needs something external. It calls this. That is the whole file.
//
// NOT A COMMAND, NOT A ROUTE, NOT A HANDLER
//   Nothing outside the process asks for this. There is no request to decode, no
//   EventSink to stream at, no JSON anywhere. The Analyst reads the live CoT
//   stream and fires the moment an external data need surfaces — it does not
//   wait for the reasoning to finish and it does not wait to be told
//   (SPEC_Ingest_Updates §1: "The Analyst does not wait to be told. It reads the
//   intent in the CoT and acts.").
//
// THE GATE STAYS
//   Only Analyst-department agents reach external services. That is checked here
//   against the slot registry — the engine's own authority on department — and
//   NOT against the agent's own claim about itself. An agent cannot name its way
//   into this by calling itself "Analyst-something": the registry decides.
//
// QUARANTINE ORDER (unchanged, load-bearing)
//   raw content lands in the Aether input pool FIRST, then the Aether agent runs
//   over it and sanitises. Nothing raw is ever read by the requesting agent.
//
// THE NETWORK LAYER IS NOT BOUND
//   Prime OS's network layer does not exist yet, so no fetch can actually
//   happen. This returns NetworkNotBound and NOTHING ELSE. It does not fabricate
//   content, does not report "fetched", and does not report "ready". The
//   previous implementation streamed a fake success path over stub content —
//   that is exactly the silent fallback this codebase forbids, and it is gone.

#pragma once

#include <string>

namespace prime {

class EngineContext;

enum class AetherStatus {
    Ready,
    Denied,
    NoPipeline,
    NoAetherSlot,
    InvalidUrl,
    NetworkNotBound,
    SanitiseFailed
};

const char* aether_status_name(AetherStatus s);

struct AetherOutcome {
    AetherStatus status = AetherStatus::NetworkNotBound;
    std::string  detail;
};

AetherOutcome aether_fetch(EngineContext&     engine,
                           const std::string& source_agent,
                           const std::string& url);

}
