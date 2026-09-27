// generation_run.h — Step one resident agent forward
//
// ONE JOB: given the name of an agent that is ALREADY RESIDENT, run its
// generation and hand its native PrimeTokens to a sink as they emerge. That is
// the whole file. It does not decide when to run, does not decide what the agent
// may see, does not assemble anything, and does not know what a transport is.
//
// THERE IS NO "TURN DISPATCH"
//   Nothing here is a request handler and nothing here waits to be handed a
//   package. Agents are loaded into memory once, at pipeline load — that is
//   their only "start". After that they are resident and watching their own
//   pools. This function is the compute that happens when a resident agent has
//   something to act on; it is not permission for it to begin. The old model
//   (build a request, send it in, get a finished answer back) is deleted, not
//   relocated: see CONVERSION_LEDGER.md.
//
// NO CONTEXT TRAVELS THROUGH HERE
//   No conversation, no message list, no directive, no rules. An agent's context
//   is whatever its pools currently expose to it under the compiled access mask,
//   which varies prompt to prompt and turn to turn and is not this file's
//   business. Nothing is re-sent, nothing is re-read from disk, nothing is
//   rebuilt. The agent reads what is in front of it.
//
//   In particular: the per-agent directive is NOT loaded here. It is resident,
//   pinned at pipeline load alongside the rules, and read by the agent every
//   parse. `PromptResolver` has exactly one caller now (pipeline load) and it is
//   not this one.
//
// NO FUSION, NO SPLIT
//   This steps ONE resident agent. It never forks, never duplicates, never
//   spawns a thread. Fusion constituents and duplicated fusion instances are
//   each resident agents in their own right, addressed by their own name, and
//   each arrives here separately. Split mode is a masking decision made
//   elsewhere — instances are already standing and dormant, not manufactured on
//   demand. Nothing about that decision is visible or reachable from this file.
//
// TEMPERATURE IS NOT CLAMPED
//   An out-of-range temperature is a hard error, not something quietly pulled
//   back to the nearest legal value. A silently corrected temperature is a
//   silently different agent.

#pragma once

#include "dispatch.h"          // TokenSink, DispatchStatus

#include <atomic>
#include <string>

namespace prime {

class EngineContext;

// Why a run ended. Distinct from DispatchStatus, which describes what the kernel
// did; these describe what THIS layer found before or around the kernel call.
enum class GenerationStatus {
    Ok,                  // ran to completion
    NoPipeline,          // nothing is resident — no agent exists to step
    AgentNotResident,    // that name is not bound in the current pipeline
    KernelCallUnbound,   // resident, but its slot's contract never resolved to
                         // a kernel call — a load-time binding gap, not a
                         // per-request failure. See dispatch.h / model_discovery.h.
    BadTemperature,      // outside [0.0, 2.0]; not clamped, refused
    Aborted,             // abort was raised; the sink stopped accepting tokens
    KernelFailed         // the kernel returned a non-Ok status; see dispatch_status
};

const char* generation_status_name(GenerationStatus s);

struct GenerationOutcome {
    GenerationStatus status          = GenerationStatus::KernelFailed;
    DispatchStatus   dispatch_status = DispatchStatus::KernelUnavailable;
    std::string      detail;         // human-readable; never a wire format
};

// Step the resident agent named `agent_name` forward, delivering each PrimeToken
// and its decoded text to `sink` as it is produced.
//
// temperature: the value authored for this agent in the roster. 0.0 means "the
//   agent's own default applies downstream" (the roster invariant), NOT "no
//   temperature was supplied". Anything outside [0.0, 2.0] is refused.
//
// abort: checked before every token is handed on. When raised, the sink is not
//   called again and the run unwinds with Aborted. Nothing is discarded or
//   rolled back — tokens already emitted stand.
//
// Returns what happened. Never throws. Never partially succeeds silently: an
// unresolved agent, an unloaded pipeline, or an illegal temperature all return
// before the kernel is touched.
GenerationOutcome run_generation(EngineContext&      engine,
                                 const std::string&  agent_name,
                                 double              temperature,
                                 const TokenSink&    sink,
                                 std::atomic<bool>&  abort);

} // namespace prime
