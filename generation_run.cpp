// generation_run.cpp — Step one resident agent forward

#include "generation_run.h"
#include "engine_context.h"
#include "slot_registry.h"

namespace prime {

const char* generation_status_name(GenerationStatus s) {
    switch (s) {
        case GenerationStatus::Ok:               return "ok";
        case GenerationStatus::NoPipeline:       return "no_pipeline";
        case GenerationStatus::AgentNotResident: return "agent_not_resident";
        case GenerationStatus::KernelCallUnbound: return "kernel_call_unbound";
        case GenerationStatus::BadTemperature:   return "bad_temperature";
        case GenerationStatus::Aborted:          return "aborted";
        case GenerationStatus::KernelFailed:     return "kernel_failed";
        default:                                 return "unknown";
    }
}

namespace {

GenerationOutcome fail(GenerationStatus s, std::string detail) {
    GenerationOutcome o;
    o.status = s;
    o.detail = std::move(detail);
    return o;
}

constexpr double kMinTemperature = 0.0;
constexpr double kMaxTemperature = 2.0;

}

GenerationOutcome run_generation(EngineContext&      engine,
                                 const std::string&  agent_name,
                                 double              temperature,
                                 const TokenSink&    sink,
                                 std::atomic<bool>&  abort) {
    if (agent_name.empty())
        return fail(GenerationStatus::AgentNotResident, "no agent named");

    if (!engine.has_pipeline())
        return fail(GenerationStatus::NoPipeline,
                    "no pipeline resident — nothing is loaded to step");

    if (!(temperature >= kMinTemperature && temperature <= kMaxTemperature))
        return fail(GenerationStatus::BadTemperature,
                    agent_name + ": temperature " + std::to_string(temperature) +
                    " is outside [0.0, 2.0]");

    auto slot = engine.slots().by_name(agent_name);
    if (!slot)
        return fail(GenerationStatus::AgentNotResident,
                    "not resident in the current pipeline: " + agent_name);

    if (slot->token_handle == 0)
        return fail(GenerationStatus::AgentNotResident,
                    agent_name + ": bound without a token handle");

    if (slot->kernel_call == nullptr)
        return fail(GenerationStatus::KernelCallUnbound,
                    agent_name + ": contract " + contract_name(slot->contract) +
                    " has no bound kernel call — check contract_from_department()");

    DispatchRequest req;
    req.contract     = slot->contract;
    req.format       = slot->format;
    req.target       = slot->target;
    req.model_path   = slot->agent.gguf_path;
    req.agent_name   = slot->agent.name;
    req.token_handle = slot->token_handle;
    req.temperature  = temperature;

    bool aborted = false;
    TokenSink guarded = [&](const PrimeToken& tok, const std::string& text) -> bool {
        if (abort.load()) { aborted = true; return false; }
        return sink ? sink(tok, text) : false;
    };

    const DispatchResult result = slot->kernel_call(engine.kernel_backend(), req, guarded);

    GenerationOutcome out;
    out.dispatch_status = result.status;
    out.detail          = result.detail;

    if (aborted) {
        out.status = GenerationStatus::Aborted;
        if (out.detail.empty()) out.detail = agent_name + ": aborted";
        return out;
    }

    out.status = (result.status == DispatchStatus::Ok)
                     ? GenerationStatus::Ok
                     : GenerationStatus::KernelFailed;
    return out;
}

}
