// id_generation.h — Prime Engine identity minting (the ONLY id source)
//
// ONE FILE MINTS EVERY ID. Turn ids, prompt ids and pool ids all come from
// here and from nowhere else. TurnBook's private minting is superseded by this
// file: identity can never be minted two different ways because there is only
// one place it comes from.
//
// NO REQUEST, NO HANDOFF
//   Nothing "asks permission" for an id. The process-wide instance is simply
//   reached (same process, same address space) at the moment an id is needed —
//   by whichever file happens to be the one needing it, most commonly the pool
//   manipulation layer at the moment a pool comes into existence. An id is
//   there the instant it is needed and not a moment before.
//
// MECHANICS — session marker + counter, deliberately NO CLOCK
//   Every id mixes a per-process session marker with a monotonic per-kind
//   counter. Two sessions cannot collide even if both restart their counters
//   at zero, and no id ever depends on wall-clock (the legacy ISO-timestamp id
//   abused the clock as both identity and ordering; that is not repeated).
//   Timestamps exist elsewhere — on pool stamps, ordering facts — never
//   identity.
//
// SHAPES (opaque — nothing downstream parses these)
//   turn id    "<session>-<n>"       same shape TurnBook minted; unchanged
//   prompt id  6-digit lowercase hex  per the prompt-id ruling
//   pool id    "P<session>-<n>"
//
// THREAD SAFETY
//   Counters are atomic. Any thread mints without coordination.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace prime {

class IdGeneration {
public:
    // The process-wide instance. Reached directly wherever an id is needed —
    // never constructed per-caller, never handed around as a parameter.
    static IdGeneration& instance();

    IdGeneration(const IdGeneration&)            = delete;
    IdGeneration& operator=(const IdGeneration&) = delete;

    // A new turn id. "<session>-<n>".
    std::string mint_turn_id();

    // A new prompt id. Six lowercase hex digits, unique within the session
    // (24-bit space, session-offset start; wrapping sits far beyond any
    // session's prompt count).
    std::string mint_prompt_id();

    // A new pool id. "P<session>-<n>".
    std::string mint_pool_id();

private:
    IdGeneration();

    std::string           session_;        // per-process marker, set once
    std::atomic<uint64_t> next_turn_{0};
    std::atomic<uint64_t> next_prompt_{0};
    std::atomic<uint64_t> next_pool_{0};
    uint64_t              prompt_offset_ = 0; // session-derived start point
};

} // namespace prime
