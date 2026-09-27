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

#include "../prime_types.h"   // PrimeToken — the unit a TokenSink carries

#include <cstdint>
#include <functional>
#include <string>

namespace prime {

// ---------------------------------------------------------------------------
// Department I/O contract — fixed per department (SPEC_Prime_Specialist_Dispatch
// §3). Determines the shape of input and output, never changes with the model.
// ---------------------------------------------------------------------------
enum class Contract {
    TextToText,    // all text departments (Analyst, Architect, Arbiter, ...)
    TextToImage,   // Artist
    TextToVideo,   // Artisan
    ImageToText,   // Aperture
    AudioToText,   // Accord
    TextToAudio,   // Announcer
    Unknown
};

// Resolve a department name to its contract. Unlisted names are text
// departments (text->text) — the engine holds no opinion on what a text
// department is for. Aether is not handled here; it has its own quarantine path.
Contract contract_from_department(const std::string& department);
const char* contract_name(Contract c);

// ---------------------------------------------------------------------------
// Model format — how the weights are packaged. Detected from the model path
// (SPEC_Prime_Specialist_Dispatch §5).
// ---------------------------------------------------------------------------
enum class ModelFormat {
    Gguf,     // single .gguf file
    Onnx,     // .onnx file or package directory
    Hybrid,   // NPU+RDNA partitioned package
    Pipeline, // multi-stage specialist package — diffusion, Whisper, TTS.
              // NOT a single weight file: a directory of components the kernel
              // layer runs as a pipeline (scheduler/UNet/VAE/CLIP, encoder/
              // decoder, etc). The weight store does not GGUF-map these; the
              // dispatch layer hands the package path to the specialist kernel.
    Unknown
};

// Detect format from a resolved model path. Inspects extension, then directory
// contents for a directory path. Returns Unknown for an unrecognised package;
// the caller rejects the load with a diagnostic rather than guessing.
ModelFormat detect_format(const std::string& model_path);
const char* format_name(ModelFormat f);

// ---------------------------------------------------------------------------
// Compute target — where the model runs. Declared by the frontend per slot.
// The engine enforces no format-to-target constraint; the kernel layer is
// written to support all formats on all targets.
// ---------------------------------------------------------------------------
enum class ComputeTarget {
    RDNA,     // gfx1151 GPU compute
    XDNA,     // XDNA 2 NPU
    Hybrid,   // NPU + RDNA simultaneously
    Unknown
};

ComputeTarget target_from_string(const std::string& s);
const char* target_name(ComputeTarget t);

// ---------------------------------------------------------------------------
// Dispatch status — the result class of any kernel call. BadInput remains
// available for a SPECIFIC KernelBackend implementation to return about ITS OWN
// requirements (e.g. a VLM implementation that genuinely cannot proceed without
// a file path) — it is no longer imposed generically before the kernel is ever
// reached.
// ---------------------------------------------------------------------------
enum class DispatchStatus {
    Ok,
    KernelUnavailable,    // no kernel implementation bound for this path yet
    UnsupportedContract,  // backend does not implement this contract
    BadInput,             // this model's own requirements were not met
    ModelError            // model failed to execute
};

const char* status_name(DispatchStatus s);

// ---------------------------------------------------------------------------
// Request and result payloads.
//
// NO PROMPT STRING. A resident agent's text input is not carried here and never
// was meant to be: the agent reads whatever its pools currently expose to it,
// under the access mask compiled for the loaded pipeline. `agent_name` and
// `token_handle` identify WHICH resident agent is stepping; the kernel resolves
// that agent's readable pools through the bound access table.
//
// `media_input_path` remains for the models that use it — a real file on disk
// handed to a specific specialist is an external artefact, not conversation.
// Its presence or absence is that model's own business, not a rule enforced
// here (see header note).
// ---------------------------------------------------------------------------
struct DispatchRequest {
    Contract      contract = Contract::Unknown;
    ModelFormat   format   = ModelFormat::Unknown;
    ComputeTarget target   = ComputeTarget::RDNA;

    std::string   model_path;        // resolved, validated path to the model
    std::string   agent_name;        // resident agent this step belongs to
    uint32_t      token_handle = 0;  // its slot handle; 0 == unset (invalid)
    std::string   media_input_path;  // for the models that use one

    // Generation parameters. Zero temperature means "use the model/agent default"
    // resolved upstream — the engine hardcodes nothing (SPEC_Prime_Specialist_
    // Dispatch §7.1). There is no token cap: an agent generates what it needs and
    // extends its own pool on demand; generation is bounded by the work, not by a
    // count the engine imposes.
    double        temperature = 0.0; // 0 == model/agent default applies upstream
};

// For streaming text, tokens are delivered through this sink as they generate.
// The sink receives the full PrimeToken (id, confidence, all seven inflection
// axes, source slot) AND its decoded text. Both travel together — the coordinate
// data is never dropped at this boundary. Returns false to abort generation
// (client gone). Calls that don't stream ignore the sink.
using TokenSink = std::function<bool(const PrimeToken& token, const std::string& text)>;

struct DispatchResult {
    DispatchStatus status = DispatchStatus::KernelUnavailable;
    std::string    text_output;        // text-out contracts
    std::string    media_output_path;  // media-out contracts (image/video/audio)
    std::string    detail;             // human-readable status detail / error
};

// ---------------------------------------------------------------------------
// KernelBackend — THE SEAM. Prime_Power implements this. Each method covers one
// family of contracts. A backend implements what it covers and returns
// KernelUnavailable / UnsupportedContract for the rest.
//
//   generate_text     TextToText            (streaming via TokenSink)
//   generate_media    TextToImage/Video     (one-shot -> media_output_path)
//   understand        ImageToText/AudioToText (one-shot -> text_output)
//   synthesize_speech TextToAudio           (one-shot -> media_output_path)
// ---------------------------------------------------------------------------
class KernelBackend {
public:
    virtual ~KernelBackend() = default;

    virtual DispatchResult generate_text(const DispatchRequest& req, const TokenSink& sink) = 0;
    virtual DispatchResult generate_media(const DispatchRequest& req) = 0;
    virtual DispatchResult understand(const DispatchRequest& req) = 0;
    virtual DispatchResult synthesize_speech(const DispatchRequest& req) = 0;
};

// ---------------------------------------------------------------------------
// StubKernelBackend — the placeholder bound until Prime_Power is ready. Every
// call returns KernelUnavailable with a clear detail string naming the contract,
// format, and target that was requested.
// ---------------------------------------------------------------------------
class StubKernelBackend : public KernelBackend {
public:
    DispatchResult generate_text(const DispatchRequest& req, const TokenSink& sink) override;
    DispatchResult generate_media(const DispatchRequest& req) override;
    DispatchResult understand(const DispatchRequest& req) override;
    DispatchResult synthesize_speech(const DispatchRequest& req) override;
};

// ---------------------------------------------------------------------------
// KernelCall — a resolved, uniform-signature call onto ONE KernelBackend
// method. Every contract maps to exactly one of these, always; the mapping is
// a fixed fact, not a per-request decision. `sink` is ignored by calls that
// don't stream — that is the adapter's job, not the caller's.
// ---------------------------------------------------------------------------
using KernelCall = DispatchResult(*)(KernelBackend&, const DispatchRequest&, const TokenSink&);

// Resolve which KernelBackend method a contract binds to. Called ONCE per slot,
// at pipeline_routes.cpp's bind_fleet() — never at generation time. Returns
// nullptr for Contract::Unknown; a slot bound with an unknown contract is an
// unresolved slot, and the caller must treat a null kernel_call as loud, not
// silently skip the call.
KernelCall kernel_call_for(Contract c);

} // namespace prime
