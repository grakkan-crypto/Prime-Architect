// live_registry.h — THE LIVE WHITEBOARD
//
// ===========================================================================
// SCOPE — EXACTLY THIS, AND NOTHING BEYOND IT
//
//   LiveRegistry is the single collection of active system state, read by
//   multiple sources. It answers "what interchangeable thing is happening
//   right now?". It does not participate. It is the system noticeboard for
//   current state.
//
//   It does not read disk. It does not resolve anything for another file.
//   It does not validate. It does not derive. It does not decide.
//
// ===========================================================================
// OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by
// definition and is never made.
//
// 1. LIVEREGISTRY SORTS AND ORGANISES; IT DOES NOT COMPUTE.
//    Reserving a known name's fixed number, or counting sequentially through
//    what is left, is organising. Anything that has to understand what a
//    pool MEANS, derive a value, or apply a policy to reach an answer is
//    calculation and is never allowed — no matter how small it looks.
//
// 2. LIVEREGISTRY DOES NOT FETCH OR WORK ANYTHING OUT FOR ANOTHER FILE.
//    If another file can get a value itself, it must. "It is already loading
//    other things anyway" is not a reason.
//
// 3. LIVEREGISTRY CHECKS NOTHING FOR CORRECTNESS.
//    No range checks, no sanity checks. A bad value is the sender's mistake
//    to catch, not something fixed here.
//
// 4. EVERY STORED FACT NEEDS ITS OWN REASON TO BE KEPT.
//    Not just that someone reads it — that it genuinely needs to stay true
//    and available for a real stretch of the system's operation.
//
// 5. NO FUNCTION COMBINES TWO STORED FACTS INTO A NEW ANSWER.
//    If working something out means reading two things and reasoning about
//    how they relate, that reasoning happens elsewhere, not here.
//
// 6. NO DATA IS PLACED HERE WITHOUT AN EXIT STRATEGY.
//    Live state must update, and when it is no longer true, that must be
//    reflected here.
//
// 7. LIVEREGISTRY STORES FACTS ABOUT THE SYSTEM.
//    If the answer is not clear, it is not a fact, and it does not live here.
//
// 8. THE SCREEN IS POOL MAINTENANCE'S MEMORY, DISPLAYED.
//    LiveRegistry never writes to it, copies it, or interprets it.
// ===========================================================================

#pragma once

#include "pipeline_loader.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace prime {

struct PoolMap;
class PoolMaintenance;
PoolMaintenance& pool_maintenance();

// ===========================================================================
// THE KEY — WHAT EVERY PERMISSION AND MASK BIT MEANS
//
// The pipeline payload (pipeline_loader.h) carries, per pool, a trigger word
// of 3 bits per mask, and, per agent listed against that pool, a word of 2
// access bits followed by 3 bits per mask. That header states the widths and
// the bit order and deliberately not the meaning. The meaning is stated here,
// once, and nowhere else.
//
// BIT ORDER, restated so this file is complete on its own: written left to
// right in table order; the leftmost written digit is the highest bit. Mask
// order is table order: the first mask's triple is the highest triple.
//
// POOL TRIGGER WORD — 3 bits per mask, in mask order. For each mask, who is
// allowed to flip it, highest bit first:
//
//     [ Turn ID ] [ Prompt ID ] [ Agent ]
//
// AGENT WORD — an agent's presence in a pool's list IS its read access; no
// bit is spent on it. Then, highest bits first:
//
//     [ Write ] [ Annotate ]                       — the 2 access bits, always
//     [ Default state ] [ Affected by flips ] [ Can control flips ]
//                                                  — 3 bits, once per mask,
//                                                    in mask order
//
// The functions below give the BIT POSITION of each field for a pool with a
// given mask count, and mask indexes are 0-based in table order. A reader
// checks a bit; it never assumes a width.
// ===========================================================================
namespace permission_key {

inline constexpr std::uint32_t kAccessBits  = 2;
inline constexpr std::uint32_t kBitsPerMask = 3;

// ---- widths ---------------------------------------------------------------
constexpr std::uint32_t pool_trigger_width(std::uint32_t mask_count) {
    return kBitsPerMask * mask_count;
}
constexpr std::uint32_t agent_word_width(std::uint32_t mask_count) {
    return kAccessBits + kBitsPerMask * mask_count;
}

// ---- pool trigger word: who may flip mask `mask` -------------------------
constexpr std::uint32_t trigger_turn_bit(std::uint32_t mask_count,
                                         std::uint32_t mask) {
    return pool_trigger_width(mask_count) - 1 - kBitsPerMask * mask;
}
constexpr std::uint32_t trigger_prompt_bit(std::uint32_t mask_count,
                                           std::uint32_t mask) {
    return pool_trigger_width(mask_count) - 2 - kBitsPerMask * mask;
}
constexpr std::uint32_t trigger_agent_bit(std::uint32_t mask_count,
                                          std::uint32_t mask) {
    return pool_trigger_width(mask_count) - 3 - kBitsPerMask * mask;
}

// ---- agent word: the 2 access bits ----------------------------------------
constexpr std::uint32_t write_bit(std::uint32_t mask_count) {
    return agent_word_width(mask_count) - 1;
}
constexpr std::uint32_t annotate_bit(std::uint32_t mask_count) {
    return agent_word_width(mask_count) - 2;
}

// ---- agent word: the 3 bits for mask `mask` -------------------------------
constexpr std::uint32_t mask_default_bit(std::uint32_t mask_count,
                                         std::uint32_t mask) {
    return agent_word_width(mask_count) - kAccessBits - 1 - kBitsPerMask * mask;
}
constexpr std::uint32_t mask_affected_bit(std::uint32_t mask_count,
                                          std::uint32_t mask) {
    return agent_word_width(mask_count) - kAccessBits - 2 - kBitsPerMask * mask;
}
constexpr std::uint32_t mask_control_bit(std::uint32_t mask_count,
                                         std::uint32_t mask) {
    return agent_word_width(mask_count) - kAccessBits - 3 - kBitsPerMask * mask;
}

// ---- the one read ---------------------------------------------------------
constexpr bool bit_set(std::uint32_t word, std::uint32_t bit) {
    return ((word >> bit) & 1u) != 0;
}

} // namespace permission_key

// ===========================================================================
// THE FIXED CLASS SLOTS
//
// Class ids are assigned by this registry at pipeline load, held for the
// pipeline's lifespan, and never reassigned within it. Two are fixed:
//
//   1 — SHARED CONTEXT. Always present in the incoming pool table; found
//       there by name, wherever it sits, and given id 1.
//   2 — RULES/DIRECTIVES. Always id 2, whether or not the incoming pool table
//       names it. The entry exists so the id can be found; its permissions
//       are the ruling in the Rules file, not this table's. If the incoming
//       table does name it — under any name or derivative the recognition
//       rule below catches — that entry is skipped; the fixed slot is not
//       touched by it.
//
//       PERMANENT: THIS CLASS'S PERMISSIONS ARE DECIDED AT MINT, NEVER
//       LOOKED UP. Every pool of class 2 gets its reader list from Rules,
//       derived from the roster inside Rules' docked call and handed to
//       PoolMaintenance at the moment of minting — never from this table,
//       never from any classification, never from any stored rule. This is
//       the one class where that is so, and it is so by design, not by
//       omission. It is not a gap to be closed by wiring it back to a
//       lookup.
//
// Everything else the pipeline declares is numbered from 3, walking the
// incoming table top to bottom, stepping over the two fixed names.
// ===========================================================================

inline constexpr const char*   kSharedContextPoolName   = "SHARED_CONTEXT";
inline constexpr std::uint64_t kSharedContextClassId    = 1;

inline constexpr const char*   kRulesDirectivesPoolName = "RULES_DIRECTIVES";
inline constexpr std::uint64_t kRulesDirectivesClassId  = 2;

// The recognition rule for a Rules/Directives entry arriving in the incoming
// pool table under any name or derivative: the name, uppercased, contains
// either root word. Casing, separators, word order, and suffixes do not
// matter. Broad enough to catch a real variant; a name carrying neither
// root is not a Rules/Directives pool.
inline constexpr const char* kRulesDirectivesRootWords[] = {
    "RULES",
    "DIRECTIVE",
};

inline constexpr std::uint64_t kFirstDeclaredClassId    = 3;

// ===========================================================================
// THE FACTS
// ===========================================================================

// One pool as this registry holds it: the declaration exactly as the payload
// carried it, and the class id this registry gave it.
struct LivePool {
    std::uint64_t   class_id = 0;
    PoolDeclaration declaration;
};

// One temperature: one value, under one team, found under every identifier
// in `answers_to`. The shape of the fact and nothing about how it was
// arrived at. The first identifier is the row's own identity — a solo
// agent's name, or a team position's number; any further identifier is a
// Split agent's name paired onto this same row. One row, one value, however
// many names find it: there is no second copy anywhere to keep in step.
struct LiveTemperature {
    std::string              team;        // empty for a solo agent
    std::vector<std::string> answers_to;  // its own identity first, then any Split name on it
    double                   value      = 0.0;
    bool                     is_default = true;
};

// ===========================================================================
// THE WHITEBOARD
// ===========================================================================
class LiveRegistry {
public:
    // Starting up: Pool Maintenance is told, and claims the screen.
    LiveRegistry();

    LiveRegistry(const LiveRegistry&)            = delete;
    LiveRegistry& operator=(const LiveRegistry&) = delete;

    // This registry's standing declaration of the payload categories it
    // needs (payload_categories vocabulary). Data, read directly by whoever
    // hands the payload over.
    static const std::vector<std::string> declared_needs;

    // ---- the pipeline, landed in one motion -------------------------------
    // The old pipeline's name, roster, pool table, and temperatures come down
    // and the new name, roster, and pool table go in, under one lock — never
    // a mix. Class ids are assigned during the landing (organising, per
    // Ruling 1). The transference check — what is now held is what was
    // handed — is posted inside, at the moment it is answered. Nothing is
    // handed back.
    void store_pipeline(const std::string& pipeline_name,
                        const PipelinePayload& payload);

    std::string pipeline_name() const;
    bool        loaded() const;

    // ---- roster -----------------------------------------------------------
    // The fleet roster, exactly as the pipeline payload sent it. Nothing is
    // searched or tallied here (Ruling 5); a caller reads the list.
    std::vector<std::string> agent_names() const;

    // ---- pools: class ids and permissions ---------------------------------
    std::vector<LivePool>   pools() const;
    std::optional<LivePool> pool(const std::string& name) const;
    std::uint64_t           class_id_for(const std::string& name) const; // 0: not here

    // ---- temperature -------------------------------------------------------
    // Held here, written onto here. The whole set is replaced as handed. A
    // lookup names the team and any one identifier the row answers to — a
    // solo agent's name, a position number, or a Split agent's name — and
    // finds the one row carrying it. A membership check on a stored list:
    // organising, not computing (Ruling 1).
    std::vector<LiveTemperature> temperatures() const;
    std::optional<double> temperature_value(const std::string& team,
                                            const std::string& identifier) const;
    std::optional<bool>   temperature_is_default(const std::string& team,
                                                 const std::string& identifier) const;
    void commit_temperatures(std::vector<LiveTemperature> set);
    void acknowledge_defaults();

    // ---- rebuttal switch — owns the state, so does the telling ------------
    void set_rebuttal_active(bool active);
    bool rebuttal_active() const;
    void on_rebuttal_switch(std::function<void(bool active)> signal);

    // ---- prompt links — whole-set writes only ------------------------------
    void link_prompt(const std::string& prompt_id,
                     std::vector<std::string> linked_pool_ids);
    std::vector<std::string> linked_pools(const std::string& prompt_id) const;
    void unlink_prompt(const std::string& prompt_id);

    // ---- the screen ---------------------------------------------------------
    // The reflection of the pool map: names the map's real memory in Pool
    // Maintenance. Claimed by Pool Maintenance when this registry starts up.
    const PoolMap* screen = nullptr;

private:
    // Pipeline-scoped — everything under this lock swaps as one.
    mutable std::mutex           pipeline_mutex_;
    std::string                  pipeline_name_;
    std::vector<std::string>     roster_;
    std::vector<LivePool>        pools_;
    std::vector<LiveTemperature> temperatures_;

    // Rebuttal
    std::atomic<bool>                             rebuttal_active_{false};
    mutable std::mutex                            rebuttal_signal_mutex_;
    std::vector<std::function<void(bool active)>> rebuttal_signals_;

    // Prompt links
    mutable std::mutex                                  links_mutex_;
    std::map<std::string, std::vector<std::string>>     prompt_links_;
};

// ===========================================================================
// THE ONE LIVE INSTANCE
// ===========================================================================

// The single live whiteboard. It exists from SYSTEM load — created when the
// program comes up, not when a pipeline does. It is the live registry of the
// system; the pipeline is one of the things it holds. Whoever
// needs it calls it directly, here, and calls its operations on it.
LiveRegistry& live_registry();

} // namespace prime
