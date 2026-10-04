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

struct PrimeToken {
    uint32_t token_id     = 0;
    float    confidence   = 0.0f;
    uint8_t  inflection[7] = {0};
    uint32_t source_slot  = 0;

    uint8_t vitality()     const { return inflection[0]; }
    uint8_t weight()       const { return inflection[1]; }
    uint8_t serious()      const { return inflection[2]; }
    uint8_t questioning()  const { return inflection[3]; }
    uint8_t urgency()      const { return inflection[4]; }
    uint8_t conviction()   const { return inflection[5]; }
    uint8_t authenticity() const { return inflection[6]; }
};

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

struct TensorDesc {
    std::string          name;
    std::vector<uint64_t> dims;
    uint32_t             ggml_type;
    uint64_t             offset;
    const void*          data;
};

struct ModelManifest {
    std::string  architecture;
    ModelFamily  family = ModelFamily::Unknown;

    uint64_t context_length   = 0;
    uint64_t embedding_length  = 0;
    uint64_t block_count       = 0;
    uint64_t head_count        = 0;
    uint64_t head_count_kv     = 0;
    uint64_t feed_forward_length = 0;
    uint64_t vocab_size        = 0;

    bool     is_moe = false;
    uint64_t expert_count = 0;
    uint64_t expert_used_count = 0;

    uint64_t eos_token_id      = 0;
    bool     eos_token_present = false;
};

struct VocabMap {
    ModelFamily              family = ModelFamily::Unknown;
    std::string              tokenizer_model;
    std::vector<std::string> id_to_text;

    std::unordered_map<std::string, uint32_t> text_to_id;
};

struct WeightRegion {
    std::string  source_path;
    const void*  base = nullptr;
    uint64_t     extent = 0;

    ModelManifest            manifest;
    std::vector<TensorDesc>  tensors;
    VocabMap                 vocab;

    uint32_t     ref_count = 0;
};

}
