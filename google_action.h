// google_action.h — Google Workspace action, initiated by the agent that needs it
//
// ONE JOB: an Analyst-department agent has recognised, mid-reasoning, that it
// needs a Gmail/Drive/Calendar/Tasks/Sheets action. It calls this.
//
// NOT A COMMAND, NOT A ROUTE, NOT A HANDLER
//   Same correction as aether_fetch.h. Nothing outside asks for this; the agent
//   reads its own live reasoning and acts the moment it has enough to act on. No
//   request to decode, no JSON body, no Result. The credential lifecycle below
//   is unchanged and was never the problem — only the shell around it was.
//
// THE GATE STAYS
//   Analyst-department agents only, verified against the slot registry rather
//   than the agent's own claim about its name.
//
// CREDENTIAL LIFECYCLE (SPEC_PrimeEngine_v2_2 §10.6) — unchanged
//   1. credentials.json absent  -> SetupRequired (a human supplies OAuth secrets)
//   2. token.json absent        -> AuthRequired  (interactive consent, human-gated)
//   3. token.json expired       -> refresh from the stored refresh token
//   4. valid                    -> execute the requested service action
//
//   Steps 1 and 2 are the same category of waiting as the HITL write gate: a
//   human genuinely has to act, and the AI correctly stops. That is not the
//   reactive pattern being removed anywhere else.
//
// THE CLIENT LIBRARY IS NOT BOUND
//   Returns ClientNotBound. It does not return a shaped success acknowledgement
//   describing a call it never made. The previous implementation did exactly
//   that, and a caller could not tell the difference between a real result and
//   the seam.

#pragma once

#include <string>

namespace prime {

class EngineContext;

enum class GoogleStatus {
    Ok,
    Denied,
    NoPipeline,
    UnknownService,
    MissingAction,
    SetupRequired,
    AuthRequired,
    ClientNotBound
};

const char* google_status_name(GoogleStatus s);

struct GoogleOutcome {
    GoogleStatus status = GoogleStatus::ClientNotBound;
    std::string  detail;
};

GoogleOutcome google_action(EngineContext&     engine,
                            const std::string& source_agent,
                            const std::string& service,
                            const std::string& action,
                            const std::string& data);

}
