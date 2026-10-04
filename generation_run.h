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

#include "dispatch.h"

#include <atomic>
#include <string>

namespace prime {

class EngineContext;

enum class GenerationStatus {
    Ok,
    NoPipeline,
    AgentNotResident,
    KernelCallUnbound,

    BadTemperature,
    Aborted,
    KernelFailed
};

const char* generation_status_name(GenerationStatus s);

struct GenerationOutcome {
    GenerationStatus status          = GenerationStatus::KernelFailed;
    DispatchStatus   dispatch_status = DispatchStatus::KernelUnavailable;
    std::string      detail;
};

GenerationOutcome run_generation(EngineContext&      engine,
                                 const std::string&  agent_name,
                                 double              temperature,
                                 const TokenSink&    sink,
                                 std::atomic<bool>&  abort);

}
