// context_matcher.h — ContextMatcher
//
// ===========================================================================
// FUNCTION
//
//   Owns the matching of prompt IDs to Shared Context pools: from a prompt
//   agent's delivered Class 1 attention, it decides which pools each prompt
//   ID draws on and tells Masking as it does. It is the only place that
//   decides a prompt ID's pools.
//
//   It is told by Watcher when an input pool appears, takes that pool's
//   prompt ID from the pool map, and evaluates each generation step's
//   attention against it. The step that produced the stop token is not
//   evaluated: on it, the prompt ID's pools are posted to LiveRegistry and
//   everything held for that prompt ID is cleared. At interaction end, the
//   prompt IDs reported to it are released: their rows are removed from
//   LiveRegistry.
//
// ===========================================================================
// OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by
// definition. It is raised with the user, never made.
//
// INVARIANTS
//
//   CM-2  MUST FIND PROMPT AGENTS FROM THE PIPELINE'S PERMISSION TABLE, BY
//         AGENT NAME, AND MUST NOT NAME THEM HERE OR KEY THEM BY MODEL,
//         because pipelines differ and agents share models.
//
//   CM-3  MUST NOT NOTE A POOL ON A SINGLE READING, because the first tokens
//         of a generation spread attention and one reading is noise. Not by
//         lowering the settle count to react faster.
//
//   CM-4  WHILE A PROMPT ID IS BEING EVALUATED, MUST TELL MASKING OF EVERY
//         NOTING AND EVERY TAKING OFF, IN THE SAME MOTION, because otherwise
//         the downstream agent's view goes out of step with the decision.
//         Not batched, and no taking off left untold. Once the prompt ID's
//         pools are posted, its context is fixed as far as this file is
//         concerned.
//
//   CM-5  MUST NOT WRITE A PROMPT ID'S POOLS TO LIVEREGISTRY WHILE IT IS
//         BEING EVALUATED, because readers would act on a partial set. Not
//         each step to keep it current.
// ===========================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prime {

// One generated token's attention to one Class 1 pool, and the pool's
// length in tokens.
struct PoolAttention {
    std::string   pool_id;
    std::uint64_t tokens = 0;
    float         weight = 0.0f;
};

// One generated token's Class 1 attention: its total to Class 1, its
// attention to each Class 1 pool, and whether this step produced the stop
// token.
struct AttentionStep {
    float                      class_total = 0.0f;
    std::vector<PoolAttention> pools;
    bool                       stop_token  = false;
};

// Registers one Watcher request per input pool the registry knows.
void ContextMatcher_Watch();

// Watcher's recipient: the request's name and message. The mechanical
// receipt goes back at once.
bool ContextMatcher_Receive(const std::string& name, const std::string& message);

// One generation step's Class 1 attention, delivered as it is produced and
// evaluated per token.
void ContextMatcher_Evaluate(const AttentionStep& step);

// The prompt IDs of an ended interaction, as read off its migrating output
// pool. Each one's row is removed from LiveRegistry's prompt links.
void ContextMatcher_Release(const std::vector<std::string>& prompt_ids);

} // namespace prime
