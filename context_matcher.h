// context_matcher.h — ContextMatcher: context matching. It links prompt IDs
//                     to pool IDs for Shared Context revealing.
//
// ===========================================================================
// WHAT THIS FILE IS
//
//   One function, context matching, reached by two routes. The first is
//   ContextMatcher_Watch: it asks Watcher to watch for a pool arriving in
//   each input pool the matching serves. The second is
//   ContextMatcher_Receive: the recipient Watcher reaches when one arrives.
//
//   Arriving is a pool entering the class. It is created into it or it
//   migrates into it; the map shows both the same way, and one request per
//   class catches both. There is one request per input pool, so each
//   delivery says which input pool it concerns and nothing has to be
//   untangled.
//
//   The name and the message of each request are the input pool's registry
//   name, exactly as the pipeline declares it. The name is prefixed with
//   ContextMatcher so no other registrant can collide with it. The message
//   is the bare name, which is the token LiveRegistry answers to.
//
//   At registration the prompt agents are found: every agent with write
//   access on an input pool, by name, as many as there are. They are held
//   for the pipeline's lifespan.
//
//   On a delivery, the input pool named by the message is turned back into
//   its class ID through LiveRegistry, and the pool map is read on the
//   screen for the newest pool of that class: the one with the greatest
//   pool ID, pool IDs being sequential. Its one prompt ID is held, and a new
//   row for it, with no pools, is written to LiveRegistry's prompt links.
//   The work runs apart from the receipt; Watcher is never kept waiting.
//
//   Every generation step of a prompt agent delivers its Class 1 attention,
//   split by KVBuilder's overlay of the pool sections: the step's total to
//   Class 1, and its attention to each Class 1 pool, labelled with the
//   pool's ID and token length. Each delivered step is evaluated as it
//   arrives, against the held prompt ID.
//
//   Each step gives every pool a lift: its share of the step's Class 1
//   attention divided by its share of Class 1 tokens. One is background. A
//   pool qualifies once it has enough lifts to judge and either the mean of
//   its most recent ones is sustained, or it stood out sharply on several of
//   the most recent steps. A qualifying pool is noted against the held
//   prompt ID. A noted pool that no longer qualifies is taken off once the
//   mean of all its lifts falls below background. Every noting and every
//   taking off tells Masking, in the same motion, the prompt IDs and the
//   pool IDs concerned. The pools noted are held here and not written to
//   LiveRegistry while the prompt ID is being evaluated. The evaluation
//   starts afresh with each new held prompt ID.
//
//   The map is read only through the map key. The key is asked of Pool
//   Maintenance once, before this file's first visit to the screen in the
//   session, and held for the rest of the session. Between deliveries this
//   file holds the key, the prompt agents, the held prompt ID, each pool's
//   lifts in the evaluation and the pools noted against the held prompt ID,
//   nothing else.
//
// ===========================================================================
// OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by
// definition. It is raised with the user, never made.
//
// 1. CONTEXTMATCHER MUST NOT POLL.
//    It must not loop, time, or check anything to find out whether
//    something has happened. It must act only on what it is told.
//
// 2. A CLASS ID MUST NOT BE STORED OR WRITTEN IN.
//    It must be looked up by name from LiveRegistry at the moment it is
//    needed.
//
// 3. EVERY WATCHER REQUEST MUST CARRY THE PIPELINE SCOPE.
//    A request must not outlive the pipeline whose class IDs it was built
//    from.
//
// 4. EVERY WATCHER REQUEST MUST BE HANDED OVER WITH EVERY PART STATED.
//    No part may be left to a default.
//
// 5. AN INPUT POOL NAME LIVEREGISTRY DOES NOT KNOW MUST BE FLAGGED TO
//    WELLNESS AND MUST NOT STOP THE OTHERS.
//    Nothing may stand in for the missing class, and it must not be retried.
//
// 6. A WATCHER DELIVERY MUST BE ANSWERED AT ONCE.
//    No work may be done before the receipt goes back. Nothing may be asked
//    of Watcher or sent back to it beyond the receipt.
//
// 7. THE MAP KEY MUST BE TAKEN ONCE PER SESSION, BEFORE THE FIRST VISIT TO
//    THE SCREEN, AND HELD.
//    It must not be asked for again in the session, passed on, or edited.
//    The map must be read only through the key, never through its layout.
//
// 8. EVERY MAP READ MUST BE LEFT BEFORE WHAT IT GAVE IS USED.
//    A read must not be kept, carried to another delivery, or handed on.
//
// 9. THE NEWEST POOL MUST BE CHOSEN BY THE GREATEST POOL ID.
//    It must not be chosen by time or by prompt ID.
//
// 10. A PROMPT AGENT MUST BE FOUND FROM THE PIPELINE'S PERMISSION TABLE.
//     No agent may be named here. An agent must be identified by agent
//     name, never by model.
//
// 11. ATTENTION MUST BE TAKEN ONLY AS DELIVERED.
//     This file must not read attention for itself, take raw weights, or
//     take attention to anything outside Class 1.
//
// 12. A POOL MUST NOT BE NOTED ON A SINGLE READING.
//     Noting must be earned over a run of steps.
//
// 13. EVERY CHANGE TO A PROMPT ID'S POOLS MUST BE TOLD TO MASKING IN THE
//     SAME MOTION.
//     Masking must be told nothing but the prompt IDs and the pool IDs
//     concerned.
//
// 14. A PROMPT ID'S POOLS MUST NOT BE WRITTEN TO LIVEREGISTRY WHILE IT IS
//     BEING EVALUATED.
//     Nothing partial may be posted.
//
// 15. CONTEXTMATCHER MUST NOT CREATE, WRITE, MIGRATE OR DESTROY ANY POOL.
//
// WELLNESS
//   Bare booleans, named for what they answer, set at the instant they are
//   answered, never read again here. Wellness sees them because they exist.
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

// One generated token's Class 1 attention: its total to Class 1, and its
// attention to each Class 1 pool.
struct AttentionStep {
    float                      class_total = 0.0f;
    std::vector<PoolAttention> pools;
};

// Registers one Watcher request per input pool the registry knows.
void ContextMatcher_Watch();

// Watcher's recipient: the request's name and message. The mechanical
// receipt goes back at once.
bool ContextMatcher_Receive(const std::string& name, const std::string& message);

// One generation step's Class 1 attention, delivered as it is produced and
// evaluated per token.
void ContextMatcher_Evaluate(const AttentionStep& step);

} // namespace prime
