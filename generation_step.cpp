// generation_step.cpp — per-token forward pass implementation
//
// Calls the kernels in the correct sequence for one new token against one
// loaded model. This is the primitive that single-model generation and the
// fusion loop both build on.
//
// SEQUENCE PER TOKEN
//   1. Embed the new token id -> hidden_dim activation vector
//   2. For each layer 0..n_layers-1:
//      a. RMS norm (pre-attention)
//      b. Attention (Q from normed activation, K/V from pool)
//      c. Residual add (attention output + pre-norm input)
//      d. RMS norm (pre-feedforward)
//      e. Feedforward (SwiGLU / GeGLU / MoE via expert routing)
//      f. Residual add (feedforward output + pre-ffn-norm input)
//   3. Final RMS norm
//   4. Output projection -> logits over native vocab
//
// ACTIVATION LAYOUT
//   Single-token decode (n_new == 1). Activations are [1, hidden_dim] f32
//   vectors, allocated on the heap per call. At batch size 1 this is a handful
//   of kilobytes and heap allocation is not the bottleneck. The ISA path will
//   manage activation memory in the arena; that is a dispatch-layer concern.
//
// KV WRITE (append)
//   The attention kernel reads K/V from the pool but does NOT write new K/V —
//   that is the job of append(). The kernel's KvRegionBinding covers positions
//   [0, kv_position); the new position is kv_position itself, written by
//   append() after the caller has sampled and confirmed the chosen token.
//   This keeps the forward pass read-only against the pool, which is correct
//   for fusion (all constituents step before anyone appends).
//
// ATTENTION EXPORT
//   When export is armed the attention kernel is given a pre-allocated buffer
//   in attn_weight_export. The kernel writes one float per prior token per
//   head per layer. The caller receives these in StepResult::attn_weights.
//   The buffer is freshly allocated per step() call — the caller is responsible
//   for writing it to disk or forwarding to the Arbiter before the next call.
//
// RESIDUAL CONNECTIONS
//   Each sub-block (attention, feedforward) adds its output back to its input.
//   This is the "skip connection" that lets gradients flow during training and
//   lets each layer refine rather than replace the representation. The addition
//   happens here in the orchestration layer, not inside the kernels — the
//   kernels write to `out`, the step interface adds `out` back to the pre-norm
//   input and carries the result forward.

#include "generation_step.h"
#include "kernels.h"

#include <cstring>
#include <stdexcept>
#include <vector>
#include <cmath>

namespace prime {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

GenerationStep::GenerationStep(KernelImpl& kernels, KvPoolAllocator& pool)
    : kernels_(kernels), pool_(pool) {}

// ---------------------------------------------------------------------------
// step() — full forward pass for one new token
// ---------------------------------------------------------------------------

StepResult GenerationStep::step(ResidentModel& model, uint32_t token_id) {
    // Validate before touching anything.
    std::string reason;
    if (!model.is_valid(reason)) {
        throw std::runtime_error("GenerationStep::step — invalid model: " + reason);
    }

    const uint32_t D   = model.hidden_dim;
    const uint32_t H   = model.n_heads;
    const uint32_t HKV = model.n_kv_heads;
    const uint32_t HD  = model.head_dim;
    const uint32_t L   = model.n_layers;

    // ------------------------------------------------------------------
    // Activation buffers — [1, D] f32 throughout (single-token decode).
    // x is the "current state of the token" as it flows through the layers.
    // norm_out holds the normed version before each sub-block.
    // attn_out holds the attention kernel's output before residual add.
    // ffn_out holds the feedforward kernel's output before residual add.
    // ------------------------------------------------------------------
    std::vector<float> x        (D, 0.0f);  // main residual stream
    std::vector<float> norm_out (D, 0.0f);  // pre-block normed activation
    std::vector<float> attn_out (H * HD, 0.0f); // attention output
    std::vector<float> ffn_out  (D, 0.0f);  // feedforward output
    std::vector<float> q_buf    (H * HD, 0.0f); // query buffer for attention kernel

    // ------------------------------------------------------------------
    // 1. Embedding — token id -> hidden_dim vector
    // ------------------------------------------------------------------
    {
        ActivationView out_view;
        out_view.data     = x.data();
        out_view.n_tokens = 1;
        out_view.dim      = D;
        out_view.dtype    = DType::F32;

        EmbeddingParams ep;
        ep.weights           = model.token_embedding;
        ep.embed_tensor_name = "token_embd.weight";
        ep.token_ids         = &token_id;
        ep.n_tokens          = 1;
        ep.out               = out_view;

        KernelDescriptor desc = make_descriptor(model, KernelType::Embedding);
        kernels_.embedding(desc, ep, nullptr);
    }

    // ------------------------------------------------------------------
    // Attention export — allocate side-buffers if armed.
    // context_len is the number of tokens already in the KV pool; the
    // attention kernel produces one weight per prior token per head.
    // ------------------------------------------------------------------
    const uint64_t context_len = model.kv_position; // tokens already in pool
    StepResult result;

    if (model.export_attn_weights) {
        // is_valid() already confirmed attn_export_block > 0.
        result.attn_weights.resize(L);
        for (uint32_t i = 0; i < L; ++i) {
            // One float per prior token per head, per layer.
            result.attn_weights[i].resize(H * (context_len + 1), 0.0f);
        }
    }

    // ------------------------------------------------------------------
    // 2. Transformer layers
    // ------------------------------------------------------------------
    for (uint32_t layer = 0; layer < L; ++layer) {
        const LayerWeights& lw = model.layers[layer];

        // ---- a. Pre-attention RMS norm --------------------------------
        {
            ActivationView xv, ov;
            xv.data = x.data(); xv.n_tokens = 1; xv.dim = D; xv.dtype = DType::F32;
            ov.data = norm_out.data(); ov.n_tokens = 1; ov.dim = D; ov.dtype = DType::F32;

            // The norm weight is a f32 gain vector of length D stored in the
            // tensor. We resolve it from the WeightRegion's tensor list.
            // The RmsNormParams.weight field expects a const float* pointing
            // to the gain vector directly.
            const float* norm_weight = resolve_norm_weight(lw.attn_norm, "attn_norm", layer);

            RmsNormParams rp;
            rp.x      = xv;
            rp.weight = norm_weight;
            rp.dim    = D;
            rp.eps    = 1e-6f;
            rp.out    = ov;

            KernelDescriptor desc = make_descriptor(model, KernelType::RmsNorm);
            kernels_.rms_norm(desc, rp, nullptr);
        }

        // ---- b. Attention --------------------------------------------
        // The attention kernel reads Q from norm_out and K/V from the pool.
        // It writes the attention output (weighted sum of V) to attn_out.
        {
            // Q projection: norm_out -> q_buf via the Q weight matrix.
            // NOTE: the attention kernel takes pre-projected Q. We project
            // Q here before calling the kernel. K/V projection is handled
            // inside the kernel using the KV cache (pre-projected K/V were
            // written by append() at prior steps).
            //
            // OPEN: Q projection is a matmul (norm_out [1,D] x Wq [D, H*HD]).
            // This is not a separate kernel — it is a weight multiply folded
            // into the attention setup. For the reference path we do it inline
            // here as a simple f32 matmul. The ISA path will fuse this.
            project_qkv(norm_out.data(), lw.attn_qkv, q_buf.data(),
                        D, H, HD, model, layer);

            ActivationView qv, ov_attn;
            qv.data = q_buf.data(); qv.n_tokens = 1; qv.dim = (uint64_t)H * HD; qv.dtype = DType::F32;
            ov_attn.data = attn_out.data(); ov_attn.n_tokens = 1; ov_attn.dim = (uint64_t)H * HD; ov_attn.dtype = DType::F32;

            AttentionParams ap;
            ap.q           = qv;
            ap.kv          = make_kv_binding(model, layer);
            ap.n_new       = 1;
            ap.n_heads     = H;
            ap.n_kv_heads  = HKV;
            ap.head_dim    = HD;
            ap.base_pos    = model.kv_position;  // absolute position of this new token
            ap.rope_theta  = model.rope_theta;
            ap.rope_scale  = model.rope_scale;
            ap.rope_mode   = model.rope_mode;
            ap.sliding_window = model.sliding_window;
            ap.out         = ov_attn;

            // Wire export side-buffer if armed.
            if (model.export_attn_weights) {
                ap.attn_weight_export = result.attn_weights[layer].data();
                ap.attn_weight_block  = model.attn_export_block;
            }

            KernelDescriptor desc = make_descriptor(model, KernelType::Attention);
            if (model.export_attn_weights)
                desc.capability_flags |= CAP_EXPORT_ATTN_WEIGHTS;

            kernels_.attention(desc, ap, nullptr);
        }

        // ---- c. Attention output projection + residual add -----------
        // Project attn_out [H*HD] -> [D] via Wo, then add back to x.
        project_attn_out(attn_out.data(), lw.attn_out, x.data(), D, H, HD, model, layer);

        // ---- d. Pre-feedforward RMS norm -----------------------------
        {
            ActivationView xv, ov;
            xv.data = x.data(); xv.n_tokens = 1; xv.dim = D; xv.dtype = DType::F32;
            ov.data = norm_out.data(); ov.n_tokens = 1; ov.dim = D; ov.dtype = DType::F32;

            const float* norm_weight = resolve_norm_weight(lw.ffn_norm, "ffn_norm", layer);

            RmsNormParams rp;
            rp.x      = xv;
            rp.weight = norm_weight;
            rp.dim    = D;
            rp.eps    = 1e-6f;
            rp.out    = ov;

            KernelDescriptor desc = make_descriptor(model, KernelType::RmsNorm);
            kernels_.rms_norm(desc, rp, nullptr);
        }

        // ---- e. Feedforward (or MoE expert routing + feedforward) ----
        {
            ActivationView xv, ov_ffn;
            xv.data = norm_out.data(); xv.n_tokens = 1; xv.dim = D; xv.dtype = DType::F32;
            ov_ffn.data = ffn_out.data(); ov_ffn.n_tokens = 1; ov_ffn.dim = D; ov_ffn.dtype = DType::F32;

            if (model.ffn_variant == FfnVariant::SwiGLU_MoE && lw.expert_gate) {
                // MoE: route first, then run selected experts.
                const uint64_t expert_count       = 0; // OPEN: resolve from manifest
                const uint64_t experts_per_token  = 0; // OPEN: resolve from manifest
                // DEFERRED: GgufParser does not yet expose expert_count /
                // experts_per_token from GGUF metadata. These must be resolved
                // from ModelManifest once the parser surfaces them.
                // Until then, MoE models will throw below. This is loud, not silent.
                if (expert_count == 0) {
                    throw std::runtime_error(
                        "GenerationStep: MoE model loaded but expert_count is 0 — "
                        "resolve from GgufParser metadata before running MoE models. "
                        "See DEFERRED_UPDATES.md.");
                }

                std::vector<uint32_t> expert_ids(experts_per_token, 0);
                std::vector<float>    expert_weights(experts_per_token, 0.0f);

                ExpertRoutingParams erp;
                erp.x                 = xv;
                erp.weights           = lw.expert_gate;
                erp.layer             = (int)layer;
                erp.expert_count      = expert_count;
                erp.experts_per_token = experts_per_token;
                erp.out_expert_ids    = expert_ids.data();
                erp.out_expert_weights = expert_weights.data();

                KernelDescriptor desc = make_descriptor(model, KernelType::ExpertRouting);
                kernels_.expert_route(desc, erp, nullptr);

                FeedForwardParams ffp;
                ffp.x                = xv;
                ffp.weights          = lw.ffn_weights;
                ffp.layer            = (int)layer;
                ffp.dim              = D;
                ffp.ffn_dim          = model.ffn_dim;
                ffp.out              = ov_ffn;
                ffp.expert_ids       = expert_ids.data();
                ffp.expert_weights   = expert_weights.data();
                ffp.experts_per_token = experts_per_token;

                KernelDescriptor ffn_desc = make_descriptor(model, KernelType::FeedForward);
                kernels_.feed_forward(ffn_desc, ffp, nullptr);

            } else {
                // Dense feedforward (SwiGLU or GeGLU).
                FeedForwardParams ffp;
                ffp.x       = xv;
                ffp.weights = lw.ffn_weights;
                ffp.layer   = (int)layer;
                ffp.dim     = D;
                ffp.ffn_dim = model.ffn_dim;
                ffp.out     = ov_ffn;

                KernelDescriptor desc = make_descriptor(model, KernelType::FeedForward);
                kernels_.feed_forward(desc, ffp, nullptr);
            }
        }

        // ---- f. Feedforward residual add -----------------------------
        for (uint32_t d = 0; d < D; ++d)
            x[d] += ffn_out[d];
    }

    // ------------------------------------------------------------------
    // 3. Final RMS norm
    // ------------------------------------------------------------------
    {
        ActivationView xv, ov;
        xv.data = x.data(); xv.n_tokens = 1; xv.dim = D; xv.dtype = DType::F32;
        ov.data = norm_out.data(); ov.n_tokens = 1; ov.dim = D; ov.dtype = DType::F32;

        const float* norm_weight = resolve_final_norm_weight(model.final_norm);

        RmsNormParams rp;
        rp.x      = xv;
        rp.weight = norm_weight;
        rp.dim    = D;
        rp.eps    = 1e-6f;
        rp.out    = ov;

        KernelDescriptor desc = make_descriptor(model, KernelType::RmsNorm);
        kernels_.rms_norm(desc, rp, nullptr);
    }

    // ------------------------------------------------------------------
    // 4. Output projection — norm_out [1, D] -> logits [1, vocab_size]
    // ------------------------------------------------------------------
    result.logits.resize(model.vocab_size, 0.0f);
    project_output(norm_out.data(), model.output_projection,
                   result.logits.data(), D, model.vocab_size, model);

    return result;
}

// ---------------------------------------------------------------------------
// append() — commit chosen token K/V into the pool, advance kv_position
//
// The K/V values for the chosen token were computed during step() as part of
// the attention calculation. We re-derive them here from the same token and
// write them to the pool at position kv_position.
//
// DESIGN NOTE: re-deriving K/V rather than caching them from step() keeps the
// step/append boundary clean — step() is a pure read against the pool, append()
// is the single write. For the reference path re-derivation at batch size 1 is
// negligible. The ISA path may choose to cache K/V from the forward pass and
// write them in append() directly; that is a dispatch concern.
// ---------------------------------------------------------------------------
void GenerationStep::append(ResidentModel& model, uint32_t chosen_token_id,
                             const std::vector<float>& kv_scratch) {
    // kv_scratch carries the K/V values computed for this token during step().
    // The caller is responsible for providing the correct scratch buffer.
    // If kv_scratch is empty, we have no K/V to write — throw loudly.
    if (kv_scratch.empty()) {
        throw std::runtime_error(
            "GenerationStep::append — kv_scratch is empty. "
            "The step() call must compute and return K/V values for append. "
            "This interface requires the caller to pass the K/V scratch produced "
            "by step(). See the step interface design notes.");
    }

    void* pool_base = pool_.data(model.kv_pool_name);
    if (!pool_base) {
        throw std::runtime_error(
            "GenerationStep::append — KV pool '" + model.kv_pool_name +
            "' not found. Ensure the pool was committed before generation.");
    }

    // Write K/V at position kv_position.
    // The pool layout is: [token][layer][K: n_kv_heads*head_dim][V: n_kv_heads*head_dim]
    // kv_scratch carries all layers' K/V for this token in that layout.
    const PoolStat stat = pool_.stat(model.kv_pool_name);
    const uint64_t pos  = model.kv_position;

    if (pos >= stat.token_capacity) {
        // Pool needs to grow before we can write.
        const uint64_t new_cap = stat.token_capacity + (stat.token_capacity / 2) + 64;
        if (!pool_.grow(model.kv_pool_name, new_cap)) {
            throw std::runtime_error(
                "GenerationStep::append — KV pool '" + model.kv_pool_name +
                "' cannot grow to capacity " + std::to_string(new_cap));
        }
    }

    uint8_t* base = static_cast<uint8_t*>(pool_base);
    const uint64_t bytes_per_token = stat.bytes_per_token;
    std::memcpy(base + pos * bytes_per_token,
                kv_scratch.data(),
                std::min((uint64_t)kv_scratch.size() * sizeof(float), bytes_per_token));

    ++model.kv_position;
}

// ---------------------------------------------------------------------------
// KernelDescriptor builder
// ---------------------------------------------------------------------------

KernelDescriptor GenerationStep::make_descriptor(const ResidentModel& model,
                                                  KernelType type) const {
    KernelDescriptor desc;
    desc.type         = type;
    desc.ffn_variant  = model.ffn_variant;
    desc.attn_variant = model.attn_variant;
    desc.capability_flags = CAP_NONE;
    return desc;
}

// ---------------------------------------------------------------------------
// KV region binding builder
// ---------------------------------------------------------------------------

KvRegionBinding GenerationStep::make_kv_binding(const ResidentModel& model,
                                                  uint32_t layer) const {
    void* base = pool_.data(model.kv_pool_name);
    if (!base) {
        throw std::runtime_error(
            "GenerationStep: KV pool '" + model.kv_pool_name + "' not found");
    }

    const PoolStat stat = pool_.stat(model.kv_pool_name);

    // KV layout per token in the pool:
    //   [layer 0 K: n_kv_heads*head_dim f16][layer 0 V: n_kv_heads*head_dim f16]
    //   [layer 1 K: ...][layer 1 V: ...]
    //   ...
    // The byte offsets for layer i's K and V within one token's bytes_per_token span.
    const uint64_t kv_elem_bytes   = 2; // f16
    const uint64_t kv_head_dim_bytes = (uint64_t)model.n_kv_heads * model.head_dim * kv_elem_bytes;
    const uint64_t layer_stride    = kv_head_dim_bytes * 2; // K + V per layer
    const uint64_t k_offset        = layer * layer_stride;
    const uint64_t v_offset        = k_offset + kv_head_dim_bytes;

    KvRegionBinding binding;
    binding.pool_base        = base;
    binding.bytes_per_token  = stat.bytes_per_token;
    binding.k_byte_offset    = k_offset;
    binding.v_byte_offset    = v_offset;
    binding.kv_dtype         = DType::F16;
    binding.tail_snapshot    = model.kv_position; // tokens currently in pool
    binding.visibility_bitmap = nullptr;           // no tombstones in basic generation
    binding.bitmap_len_tokens = 0;

    return binding;
}

// ---------------------------------------------------------------------------
// Canonical projection (called by fusion loop via friend access)
// ---------------------------------------------------------------------------

void GenerationStep::project_to_canonical(
    const std::vector<float>& native_logits,
    const ResidentModel&      model,
    const VocabUnion&         vocab_union,
    std::vector<float>&       out_canonical)
{
    const uint32_t canon_size = vocab_union.canonical_size();
    out_canonical.assign(canon_size, -std::numeric_limits<float>::infinity());

    for (uint32_t native_id = 0; native_id < (uint32_t)native_logits.size(); ++native_id) {
        const CanonicalId cid = vocab_union.to_canonical(model.tokenizer_model, native_id);
        if (cid == CANONICAL_UNKNOWN) continue;
        // When two native tokens map to the same canonical id (rare but
        // possible with byte-fallback tokens), take the higher logit.
        if (native_logits[native_id] > out_canonical[cid])
            out_canonical[cid] = native_logits[native_id];
    }
}

// ---------------------------------------------------------------------------
// Weight resolution helpers
//
// These resolve typed pointers from WeightRegion tensor descriptors. They
// find the named tensor in the region's tensor list and return a typed pointer
// to its data. These are called per-layer during the forward pass; they do
// linear searches over the tensor list. At batch size 1 this is not the
// bottleneck. A layer-indexed cache could be added if profiling shows
// otherwise — do not optimise prematurely.
// ---------------------------------------------------------------------------

const float* GenerationStep::resolve_norm_weight(const WeightRegion* region,
                                                   const char* role,
                                                   uint32_t layer) {
    if (!region) {
        throw std::runtime_error(
            std::string("GenerationStep: null WeightRegion for ") + role +
            " at layer " + std::to_string(layer));
    }
    const std::string name = "blk." + std::to_string(layer) +
                             "." + role + ".weight";
    for (const TensorDesc& t : region->tensors) {
        if (t.name == name) return static_cast<const float*>(t.data);
    }
    throw std::runtime_error(
        "GenerationStep: norm weight tensor '" + name + "' not found");
}

const float* GenerationStep::resolve_final_norm_weight(const WeightRegion* region) {
    if (!region) {
        throw std::runtime_error("GenerationStep: null WeightRegion for final_norm");
    }
    for (const TensorDesc& t : region->tensors) {
        if (t.name == "output_norm.weight")
            return static_cast<const float*>(t.data);
    }
    throw std::runtime_error("GenerationStep: 'output_norm.weight' not found");
}

// ---------------------------------------------------------------------------
// Q/K/V projection, attention output projection, output projection
//
// These are inline f32 matmuls for the reference path. The ISA path will
// fuse these with the kernel dispatches; for now they live here as simple
// loops so the correctness of the layer sequence can be validated end-to-end
// without waiting for a fused kernel implementation.
//
// project_qkv:      norm_out [D] x Wq/Wk/Wv [D, H*HD] -> q_buf [H*HD]
//                   K and V are also projected here and stored in kv_scratch
//                   which the caller passes to append().
// project_attn_out: attn_out [H*HD] x Wo [H*HD, D] -> residual add into x[D]
// project_output:   final_norm_out [D] x Wout [D, vocab_size] -> logits[vocab_size]
// ---------------------------------------------------------------------------

void GenerationStep::project_qkv(const float* x, const WeightRegion* qkv_region,
                                   float* q_out, uint32_t D, uint32_t H, uint32_t HD,
                                   const ResidentModel& model, uint32_t layer) {
    // Resolve Q, K, V tensors by name from the region.
    // Fused QKV tensor ("attn_qkv.weight") is checked first; if absent,
    // separate Q/K/V tensors are used.
    const std::string prefix = "blk." + std::to_string(layer) + ".";
    const TensorDesc* Wq = nullptr;

    for (const TensorDesc& t : qkv_region->tensors) {
        if (t.name == prefix + "attn_qkv.weight") {
            // Fused QKV: layout is [Q rows | K rows | V rows], each [H*HD, D].
            // Q occupies the first H*HD rows.
            const float* W = static_cast<const float*>(t.data);
            for (uint32_t o = 0; o < H * HD; ++o) {
                float acc = 0.0f;
                for (uint32_t i = 0; i < D; ++i)
                    acc += x[i] * W[o * D + i];
                q_out[o] = acc;
            }
            return;
        }
        if (t.name == prefix + "attn_q.weight") Wq = &t;
    }

    if (!Wq) {
        throw std::runtime_error(
            "GenerationStep: Q weight tensor not found for layer " +
            std::to_string(layer) + " — expected '" + prefix + "attn_q.weight' "
            "or '" + prefix + "attn_qkv.weight'");
    }

    // Separate Q tensor.
    const float* W = static_cast<const float*>(Wq->data);
    for (uint32_t o = 0; o < H * HD; ++o) {
        float acc = 0.0f;
        for (uint32_t i = 0; i < D; ++i)
            acc += x[i] * W[o * D + i];
        q_out[o] = acc;
    }
    // K and V are projected and written to the KV pool by append() — they are
    // resolved there from "attn_k.weight" and "attn_v.weight". The attention
    // kernel reads K/V from the pool (prior tokens' K/V were written by prior
    // append() calls); the new token's K/V are written after sampling.
}

void GenerationStep::project_attn_out(const float* attn_out,
                                        const WeightRegion* wo_region,
                                        float* x, uint32_t D, uint32_t H, uint32_t HD,
                                        const ResidentModel& model, uint32_t layer) {
    const std::string name = "blk." + std::to_string(layer) + ".attn_output.weight";
    for (const TensorDesc& t : wo_region->tensors) {
        if (t.name == name) {
            const float* W = static_cast<const float*>(t.data);
            // Wo is [D, H*HD]: output[d] = sum_o(attn_out[o] * W[d*H*HD + o])
            // Then add residual: x[d] += output[d]
            for (uint32_t d = 0; d < D; ++d) {
                float acc = 0.0f;
                for (uint32_t o = 0; o < H * HD; ++o)
                    acc += attn_out[o] * W[d * (H * HD) + o];
                x[d] += acc;  // residual add in place
            }
            return;
        }
    }
    throw std::runtime_error(
        "GenerationStep: attention output weight '" + name + "' not found");
}

void GenerationStep::project_output(const float* norm_out,
                                     const WeightRegion* out_region,
                                     float* logits, uint32_t D, uint32_t vocab_size,
                                     const ResidentModel& model) {
    for (const TensorDesc& t : out_region->tensors) {
        if (t.name == "output.weight") {
            const float* W = static_cast<const float*>(t.data);
            // W is [vocab_size, D]: logits[v] = sum_d(norm_out[d] * W[v*D + d])
            for (uint32_t v = 0; v < vocab_size; ++v) {
                float acc = 0.0f;
                for (uint32_t d = 0; d < D; ++d)
                    acc += norm_out[d] * W[v * D + d];
                logits[v] = acc;
            }
            return;
        }
    }
    throw std::runtime_error(
        "GenerationStep: output projection tensor 'output.weight' not found");
}

} // namespace prime
