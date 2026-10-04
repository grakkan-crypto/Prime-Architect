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

    static IdGeneration& instance();

    IdGeneration(const IdGeneration&)            = delete;
    IdGeneration& operator=(const IdGeneration&) = delete;

    std::string mint_turn_id();

    std::string mint_prompt_id();

    std::string mint_pool_id();

private:
    IdGeneration();

    std::string           session_;
    std::atomic<uint64_t> next_turn_{0};
    std::atomic<uint64_t> next_prompt_{0};
    std::atomic<uint64_t> next_pool_{0};
    uint64_t              prompt_offset_ = 0;
};

}
