// prime_types.h — Prime Engine core types
//
// Shared definitions for the Phase 1 foundation: weight regions, model
// manifests, vocabulary maps, and the model-family enumeration that drives
// the vocabulary translation layer.
//
// No behaviour lives here. These are the contracts the foundation components
// hand to one another.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace prime {

// ---------------------------------------------------------------------------
// PrimeToken — the atomic unit of generation in this system.
//
// A token IS its coordinates. The text/id is inseparable from its confidence
// and its seven inflection axes; they travel together everywhere a token goes —
// in the KV pool, across the SSE stream, into every watching agent's context.
// They are NOT side-channel metadata that can be dropped to save space. Nothing
// in the engine strips a token down to bare text. An agent that does not need
// the coordinate data ignores it; it is never withheld at the source.
//
// Inflection axis order is canonical and fixed (SPEC + Session 18 §3.6, §9):
//   [0] Vitality      (was "Happy" — renamed; do not revert)
//   [1] Weight        (was "Sad"   — renamed; do not revert)
//   [2] Serious       humour/sarcasm axis; 0xF == full sincerity, NOT gravity
//   [3] Questioning
//   [4] Urgency
//   [5] Conviction
//   [6] Authenticity
//
// inflection[] holds the per-axis value as emitted by the kernel. In natural
// (non-Prime) generation every non-space token carries a full coordinate set.
// In Prime speech the alternating-token convention is a GENERATION concern the
// kernel owns; the transport and dispatch layers above never assume it and
// never thin the payload — whatever the kernel emits is carried verbatim.
// ---------------------------------------------------------------------------
struct PrimeToken {
    uint32_t token_id     = 0;     // native token id from the emitting model
    float    confidence   = 0.0f;  // post-veto, post-fusion confidence [0..1]
    uint8_t  inflection[7] = {0};  // seven axes, canonical order above
    uint32_t source_slot  = 0;     // slot_id that produced this token

    // Convenience accessors by canonical name. Index discipline lives in ONE
    // place so call sites never hand-index the array and drift out of order.
    uint8_t vitality()     const { return inflection[0]; }
    uint8_t weight()       const { return inflection[1]; }
    uint8_t serious()      const { return inflection[2]; }
    uint8_t questioning()  const { return inflection[3]; }
    uint8_t urgency()      const { return inflection[4]; }
    uint8_t conviction()   const { return inflection[5]; }
    uint8_t authenticity() const { return inflection[6]; }
};

// ---------------------------------------------------------------------------
// Model family — derived from general.architecture in the GGUF metadata.
// Drives tokenizer behaviour and is the key the vocabulary translation layer
// uses to decide how a model's logits project into canonical space.
// ---------------------------------------------------------------------------
enum class ModelFamily {
    Unknown,
    Qwen2,
    Qwen3,
    Llama,
    Gemma,
    Phi,
    Mistral,
};

ModelFamily family_from_arch(const std::string& arch);
const char*  family_name(ModelFamily f);

// ---------------------------------------------------------------------------
// Tensor descriptor — one entry per tensor in the GGUF file. The data pointer
// addresses directly into the memory-mapped file; nothing is copied.
// ---------------------------------------------------------------------------
struct TensorDesc {
    std::string          name;
    std::vector<uint64_t> dims;     // row-major extents
    uint32_t             ggml_type; // raw GGML quantisation type id
    uint64_t             offset;    // offset from the tensor-data base
    const void*          data;      // resolved absolute address into the mapping
};

// ---------------------------------------------------------------------------
// Model manifest — everything the Phase 2 kernel dispatcher needs to know
// about a model before it touches the weights. Extracted from metadata only;
// no forward pass, no data read.
// ---------------------------------------------------------------------------
struct ModelManifest {
    std::string  architecture;     // raw arch string, e.g. "qwen2"
    ModelFamily  family = ModelFamily::Unknown;

    uint64_t context_length   = 0;
    uint64_t embedding_length  = 0; // hidden dimension
    uint64_t block_count       = 0; // transformer layers
    uint64_t head_count        = 0;
    uint64_t head_count_kv     = 0; // GQA: kv heads (== head_count if MHA)
    uint64_t feed_forward_length = 0;
    uint64_t vocab_size        = 0;

    bool     is_moe = false;        // mixture-of-experts present
    uint64_t expert_count = 0;
    uint64_t expert_used_count = 0;

    // The model's stop token, surfaced from tokenizer.ggml.eos_token_id.
    // MEANINGLESS unless eos_token_present is true: 0 is a valid token id,
    // so absence is a stated fact, never a default.
    uint64_t eos_token_id      = 0;
    bool     eos_token_present = false;
};

// ---------------------------------------------------------------------------
// Vocabulary map — token-id -> text, pinned at load for the model's full
// lifecycle. The translation layer reads these to build the canonical union.
// ---------------------------------------------------------------------------
struct VocabMap {
    ModelFamily              family = ModelFamily::Unknown;
    std::string              tokenizer_model;  // tokenizer.ggml.model
    std::vector<std::string> id_to_text;       // index == native token id
    // text -> native id, built lazily by the translation layer when needed.
    std::unordered_map<std::string, uint32_t> text_to_id;
};

// ---------------------------------------------------------------------------
// WeightRegion — the handle the allocator tracks. One per unique file path.
// base/extent describe the mapped tensor-data span in the flat address space.
// Multiple agent slots referencing the same path share one WeightRegion.
// ---------------------------------------------------------------------------
struct WeightRegion {
    std::string  source_path;     // canonical path; the dedup key
    const void*  base = nullptr;   // start of the mapping
    uint64_t     extent = 0;       // bytes mapped

    ModelManifest            manifest;
    std::vector<TensorDesc>  tensors;
    VocabMap                 vocab;

    uint32_t     ref_count = 0;    // how many slots reference this region
};

} // namespace prime
