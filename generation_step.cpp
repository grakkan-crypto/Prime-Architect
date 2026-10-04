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

GenerationStep::GenerationStep(KernelImpl& kernels, KvPoolAllocator& pool)
    : kernels_(kernels), pool_(pool) {}

StepResult GenerationStep::step(ResidentModel& model, uint32_t token_id) {

    std::string reason;
    if (!model.is_valid(reason)) {
        throw std::runtime_error("GenerationStep::step — invalid model: " + reason);
    }

    const uint32_t D   = model.hidden_dim;
    const uint32_t H   = model.n_heads;
    const uint32_t HKV = model.n_kv_heads;
    const uint32_t HD  = model.head_dim;
    const uint32_t L   = model.n_layers;

    std::vector<float> x        (D, 0.0f);
    std::vector<float> norm_out (D, 0.0f);
    std::vector<float> attn_out (H * HD, 0.0f);
    std::vector<float> ffn_out  (D, 0.0f);
    std::vector<float> q_buf    (H * HD, 0.0f);

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

    const uint64_t context_len = model.kv_position;
    StepResult result;

    if (model.export_attn_weights) {

        result.attn_weights.resize(L);
        for (uint32_t i = 0; i < L; ++i) {

            result.attn_weights[i].resize(H * (context_len + 1), 0.0f);
        }
    }

    for (uint32_t layer = 0; layer < L; ++layer) {
        const LayerWeights& lw = model.layers[layer];

        {
            ActivationView xv, ov;
            xv.data = x.data(); xv.n_tokens = 1; xv.dim = D; xv.dtype = DType::F32;
            ov.data = norm_out.data(); ov.n_tokens = 1; ov.dim = D; ov.dtype = DType::F32;

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

        {

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
            ap.base_pos    = model.kv_position;
            ap.rope_theta  = model.rope_theta;
            ap.rope_scale  = model.rope_scale;
            ap.rope_mode   = model.rope_mode;
            ap.sliding_window = model.sliding_window;
            ap.out         = ov_attn;

            if (model.export_attn_weights) {
                ap.attn_weight_export = result.attn_weights[layer].data();
                ap.attn_weight_block  = model.attn_export_block;
            }

            KernelDescriptor desc = make_descriptor(model, KernelType::Attention);
            if (model.export_attn_weights)
                desc.capability_flags |= CAP_EXPORT_ATTN_WEIGHTS;

            kernels_.attention(desc, ap, nullptr);
        }

        project_attn_out(attn_out.data(), lw.attn_out, x.data(), D, H, HD, model, layer);

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

        {
            ActivationView xv, ov_ffn;
            xv.data = norm_out.data(); xv.n_tokens = 1; xv.dim = D; xv.dtype = DType::F32;
            ov_ffn.data = ffn_out.data(); ov_ffn.n_tokens = 1; ov_ffn.dim = D; ov_ffn.dtype = DType::F32;

            if (model.ffn_variant == FfnVariant::SwiGLU_MoE && lw.expert_gate) {

                const uint64_t expert_count       = 0;
                const uint64_t experts_per_token  = 0;

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

        for (uint32_t d = 0; d < D; ++d)
            x[d] += ffn_out[d];
    }

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

    result.logits.resize(model.vocab_size, 0.0f);
    project_output(norm_out.data(), model.output_projection,
                   result.logits.data(), D, model.vocab_size, model);

    return result;
}

void GenerationStep::append(ResidentModel& model, uint32_t chosen_token_id,
                             const std::vector<float>& kv_scratch) {

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

    const PoolStat stat = pool_.stat(model.kv_pool_name);
    const uint64_t pos  = model.kv_position;

    if (pos >= stat.token_capacity) {

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

KernelDescriptor GenerationStep::make_descriptor(const ResidentModel& model,
                                                  KernelType type) const {
    KernelDescriptor desc;
    desc.type         = type;
    desc.ffn_variant  = model.ffn_variant;
    desc.attn_variant = model.attn_variant;
    desc.capability_flags = CAP_NONE;
    return desc;
}

KvRegionBinding GenerationStep::make_kv_binding(const ResidentModel& model,
                                                  uint32_t layer) const {
    void* base = pool_.data(model.kv_pool_name);
    if (!base) {
        throw std::runtime_error(
            "GenerationStep: KV pool '" + model.kv_pool_name + "' not found");
    }

    const PoolStat stat = pool_.stat(model.kv_pool_name);

    const uint64_t kv_elem_bytes   = 2;
    const uint64_t kv_head_dim_bytes = (uint64_t)model.n_kv_heads * model.head_dim * kv_elem_bytes;
    const uint64_t layer_stride    = kv_head_dim_bytes * 2;
    const uint64_t k_offset        = layer * layer_stride;
    const uint64_t v_offset        = k_offset + kv_head_dim_bytes;

    KvRegionBinding binding;
    binding.pool_base        = base;
    binding.bytes_per_token  = stat.bytes_per_token;
    binding.k_byte_offset    = k_offset;
    binding.v_byte_offset    = v_offset;
    binding.kv_dtype         = DType::F16;
    binding.tail_snapshot    = model.kv_position;
    binding.visibility_bitmap = nullptr;
    binding.bitmap_len_tokens = 0;

    return binding;
}

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

        if (native_logits[native_id] > out_canonical[cid])
            out_canonical[cid] = native_logits[native_id];
    }
}

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

void GenerationStep::project_qkv(const float* x, const WeightRegion* qkv_region,
                                   float* q_out, uint32_t D, uint32_t H, uint32_t HD,
                                   const ResidentModel& model, uint32_t layer) {

    const std::string prefix = "blk." + std::to_string(layer) + ".";
    const TensorDesc* Wq = nullptr;

    for (const TensorDesc& t : qkv_region->tensors) {
        if (t.name == prefix + "attn_qkv.weight") {

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

    const float* W = static_cast<const float*>(Wq->data);
    for (uint32_t o = 0; o < H * HD; ++o) {
        float acc = 0.0f;
        for (uint32_t i = 0; i < D; ++i)
            acc += x[i] * W[o * D + i];
        q_out[o] = acc;
    }

}

void GenerationStep::project_attn_out(const float* attn_out,
                                        const WeightRegion* wo_region,
                                        float* x, uint32_t D, uint32_t H, uint32_t HD,
                                        const ResidentModel& model, uint32_t layer) {
    const std::string name = "blk." + std::to_string(layer) + ".attn_output.weight";
    for (const TensorDesc& t : wo_region->tensors) {
        if (t.name == name) {
            const float* W = static_cast<const float*>(t.data);

            for (uint32_t d = 0; d < D; ++d) {
                float acc = 0.0f;
                for (uint32_t o = 0; o < H * HD; ++o)
                    acc += attn_out[o] * W[d * (H * HD) + o];
                x[d] += acc;
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

}
