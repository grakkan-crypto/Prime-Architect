// generation_step.h — per-token forward pass interface
//
// This is the single primitive every generation path builds on — single-model,
// fused team, CoT multi-run, TTS-interrupted resumption. Given a ResidentModel
// with its KV state and one new token id, it runs the full transformer forward
// pass (embedding -> N x (norm -> attention -> norm -> feedforward) -> norm ->
// logits) and returns the raw logit vector over the model's native vocabulary.
//
// FOR FUSION
//   The fusion loop calls step() once per constituent per token instead of
//   once, collects the logit vectors, projects each through VocabUnion into
//   canonical space, then hands all of them to kernel_logit_fusion. The step
//   interface itself knows nothing about fusion — it processes one model at a
//   time. Fusion is a caller concern.
//
// FOR SINGLE-MODEL GENERATION
//   Call step(), receive logits, sample or argmax, call append() with the
//   chosen token id to commit it to the model's KV state, repeat.
//
// KV APPEND
//   append() writes the new token's K/V values into the KV pool at
//   kv_position and increments kv_position. It is separate from step() so
//   that fusion can step N models, merge, pick one token, then append the
//   SAME chosen token to all N models' KV states — not each model's own
//   argmax, which would diverge the contexts.
//
// ATTENTION EXPORT
//   When model.export_attn_weights is true and model.attn_export_block > 0,
//   step() populates the attention weight side-channel per layer. The buffer
//   is owned by the caller and passed in via StepContext. If export is armed
//   but attn_export_block is 0, step() throws — this is the loud precondition
//   enforced by ResidentModel::is_valid().
//
// ACTIVATION MEMORY
//   step() allocates its activation buffers per call on the CPU heap (f32,
//   hidden_dim wide, 1 token deep for single-token decode). This is the
//   correct choice for the reference path — cheap at batch size 1, and no
//   shared state between calls. The ISA path will manage activation memory
//   differently; that is a dispatch concern, not a contract change here.
//
// THREAD SAFETY
//   One step() at a time per ResidentModel. The KV pool write in append() is
//   not locked — single-writer-per-pool is the system invariant (enforced by
//   the slot registry, not here).

#pragma once

#include "resident_model.h"
#include "kernels.h"
#include "kv_pool.h"
#include "vocab_translation.h"

#include <cstdint>
#include <vector>

namespace prime {

struct StepResult {
    std::vector<float> logits;
    std::vector<std::vector<float>> attn_weights;
};

class GenerationStep {
public:
    explicit GenerationStep(KernelImpl& kernels, KvPoolAllocator& pool);

    StepResult step(ResidentModel& model, uint32_t token_id);

    void append(ResidentModel& model, uint32_t chosen_token_id,
                const std::vector<float>& kv_scratch);

private:
    KernelImpl&      kernels_;
    KvPoolAllocator& pool_;

    KernelDescriptor make_descriptor(const ResidentModel& model,
                                     KernelType type) const;

    KvRegionBinding make_kv_binding(const ResidentModel& model,
                                    uint32_t layer) const;

    static void project_to_canonical(
        const std::vector<float>& native_logits,
        const ResidentModel&      model,
        const VocabUnion&         vocab_union,
        std::vector<float>&       out_canonical
    );

    friend class FusionLoop;
};

}
