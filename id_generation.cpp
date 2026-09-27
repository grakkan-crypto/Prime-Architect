// id_generation.cpp — Prime Engine identity minting implementation

#include "id_generation.h"

namespace prime {

namespace {
    // Session marker: hex of a session-local static's address — stable for the
    // process, distinct across processes, no clock dependency. The exact
    // mechanism TurnBook used; moved here unchanged because this is now the
    // one place identity comes from.
    std::string make_session_marker() {
        static const int anchor = 0;
        auto p = reinterpret_cast<uintptr_t>(&anchor);
        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(sizeof(p) * 2);
        for (int shift = (int)(sizeof(p) * 8) - 4; shift >= 0; shift -= 4) {
            out.push_back(hex[(p >> shift) & 0xF]);
        }
        return out;
    }
}

IdGeneration& IdGeneration::instance() {
    static IdGeneration g;
    return g;
}

IdGeneration::IdGeneration() : session_(make_session_marker()) {
    // Prompt ids start from a session-derived offset inside the 24-bit space so
    // two sessions do not both begin at 000000. Identity still never depends on
    // a clock — the offset is folded from the same address-derived marker.
    uint64_t fold = 0;
    for (char c : session_) fold = fold * 31u + (unsigned char)c;
    prompt_offset_ = fold & 0xFFFFFFull;
}

std::string IdGeneration::mint_turn_id() {
    std::string v = session_;
    v.push_back('-');
    v += std::to_string(next_turn_.fetch_add(1, std::memory_order_relaxed));
    return v;
}

std::string IdGeneration::mint_prompt_id() {
    const uint64_t n =
        (prompt_offset_ + next_prompt_.fetch_add(1, std::memory_order_relaxed))
        & 0xFFFFFFull;
    static const char* hex = "0123456789abcdef";
    std::string out(6, '0');
    for (int i = 5; i >= 0; --i) out[(size_t)i] = hex[(n >> ((5 - i) * 4)) & 0xF];
    return out;
}

std::string IdGeneration::mint_pool_id() {
    std::string v = "P";
    v += session_;
    v.push_back('-');
    v += std::to_string(next_pool_.fetch_add(1, std::memory_order_relaxed));
    return v;
}

} // namespace prime
