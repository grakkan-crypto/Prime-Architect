// dispatch.h — Prime Engine kernel interface and the contract-to-call binding
//
// The boundary between the engine and the kernel layer (Prime_Power). This file
// owns two things and nothing else:
//   1. KernelBackend — the interface Prime_Power implements.
//   2. kernel_call_for() — resolves a Contract to the one KernelBackend method
//      that handles it. Resolved ONCE, at pipeline load, and bound onto the
//      slot (SlotRegistry::Slot::kernel_call). Nothing re-derives this at
//      generation time — there is no per-request routing step left anywhere.
//
// THERE IS NO Dispatcher CLASS ANY MORE
//   A contract has always meant exactly one kernel call — that mapping never
//   changes at runtime and never varied per request. Re-deriving it on every
//   generation was re-answering a question already answered at load. The
//   generic "does this request carry what its contract needs" check is also
//   gone: it assumed a department implies a fixed input shape, which does not
//   hold — an ingestion-class agent may handle basic image work itself, while a
//   dedicated specialist handles heavier cases under the same contract. What a
//   specific model needs is that model's own concern, enforced by its own
//   KernelBackend implementation if it needs enforcing at all, never imposed
//   centrally on every contract alike.
//
// DESIGN — three axes, each resolved once, at load
//   Department contract: WHAT the I/O shape is — contract_from_department().
//   Model format:        HOW the weights are packaged — detect_format().
//   Compute target:      WHERE it runs — target_from_string(), frontend-declared.
//   Kernel call:          WHICH KernelBackend method — kernel_call_for(contract).
//   All four are bound onto the slot at pipeline_routes.cpp's bind_fleet(), all
//   four are read-only facts about the slot from then on.

#pragma once

#include "../prime_types.h"

#include <cstdint>
#include <functional>
#include <string>

namespace prime {

enum class Contract {
    TextToText,
    TextToImage,
    TextToVideo,
    ImageToText,
    AudioToText,
    TextToAudio,
    Unknown
};

Contract contract_from_department(const std::string& department);
const char* contract_name(Contract c);

enum class ModelFormat {
    Gguf,
    Onnx,
    Hybrid,
    Pipeline,

    Unknown
};

ModelFormat detect_format(const std::string& model_path);
const char* format_name(ModelFormat f);

enum class ComputeTarget {
    RDNA,
    XDNA,
    Hybrid,
    Unknown
};

ComputeTarget target_from_string(const std::string& s);
const char* target_name(ComputeTarget t);

enum class DispatchStatus {
    Ok,
    KernelUnavailable,
    UnsupportedContract,
    BadInput,
    ModelError
};

const char* status_name(DispatchStatus s);

struct DispatchRequest {
    Contract      contract = Contract::Unknown;
    ModelFormat   format   = ModelFormat::Unknown;
    ComputeTarget target   = ComputeTarget::RDNA;

    std::string   model_path;
    std::string   agent_name;
    uint32_t      token_handle = 0;
    std::string   media_input_path;

    double        temperature = 0.0;
};

using TokenSink = std::function<bool(const PrimeToken& token, const std::string& text)>;

struct DispatchResult {
    DispatchStatus status = DispatchStatus::KernelUnavailable;
    std::string    text_output;
    std::string    media_output_path;
    std::string    detail;
};

class KernelBackend {
public:
    virtual ~KernelBackend() = default;

    virtual DispatchResult generate_text(const DispatchRequest& req, const TokenSink& sink) = 0;
    virtual DispatchResult generate_media(const DispatchRequest& req) = 0;
    virtual DispatchResult understand(const DispatchRequest& req) = 0;
    virtual DispatchResult synthesize_speech(const DispatchRequest& req) = 0;
};

class StubKernelBackend : public KernelBackend {
public:
    DispatchResult generate_text(const DispatchRequest& req, const TokenSink& sink) override;
    DispatchResult generate_media(const DispatchRequest& req) override;
    DispatchResult understand(const DispatchRequest& req) override;
    DispatchResult synthesize_speech(const DispatchRequest& req) override;
};

using KernelCall = DispatchResult(*)(KernelBackend&, const DispatchRequest&, const TokenSink&);

KernelCall kernel_call_for(Contract c);

}
