// id_registry.h — The Prime Dictionary / unified ID registry
//
// SINGLE WRITER: THE AUTHOR. This registry has exactly one writing agent. That
// fact is the entire design. Because only one hand ever writes, minting is
// nothing more than "does this concept exist? If not, take the next number."
// There is NO separate allocator mechanism — that was considered and REJECTED
// as over-engineering (Session: RAG design). An allocator earns its keep only
// when more than one writer could collide. There is one writer. It cannot
// collide with itself.
//
// WHAT AN ENTRY IS
//   A concept — not a word. "Cold" (temperature) and "cold" (manner) are two
//   entries; language ambiguity is resolved UPSTREAM at Ingest/Author before
//   anything reaches here. By the time a concept is minted it is unambiguous.
//
// THE DETERMINISTIC KIND RULE (decided this session):
//   Referent — points at something that exists independently of the inflection
//              axes (objects, places, named things, concrete actions, dates).
//              Inflection colours HOW it is said, never WHAT it is. Never needs
//              evaluation. Settled the instant it is minted.
//   Reading  — the concept is made of the same material the axes measure
//              (emotions, attitudes, judgements, intensities). A strong enough
//              axis combination can tip it into a genuinely different concept
//              (joy -> glee). Flagged for evaluation at mint.
//
// EVALUATION IS NOT A SECOND MINTING PATH. Downtime evaluation of a Reading
// concept NEVER creates a new hex token by itself. Its only output is a
// CROSS-REFERENCE logged against entries that already exist — the registry is
// also a thesaurus. A new address is only ever minted by the Author, against
// real content, through the one door. (Decided after the speculative-minting
// proposal was explicitly rejected as a drift source.)
//
// SYNONYMS — THE CONCEPT IS THE THING, WORDS ARE ITS DOORS
//   Natural language accumulated many words for the same thing (finished /
//   complete / wrapped up). That is noise to machine communication — the same
//   concept in different costumes, NOT inflection variance. A concept
//   therefore carries ONE address and MANY known surface words: the label it
//   was minted under plus every synonym the Author attaches. Any of those
//   words resolves lookup to the address; none of them mints separately.
//   Attaching synonyms is the Author's DOWNTIME enrichment work, after mint,
//   same priority rule as evaluation: real logging always wins.
//
//   THE SAME WORD CAN AND WILL SIT UNDER MULTIPLE CONCEPTS ("cold" the
//   temperature, "cold" the manner — genuine homonymy, real language, not an
//   error). A colliding mint is HELD and resolved by the BLIND SYNONYM
//   CHALLENGE (final form — supersedes the hard refusal, the ride-along
//   flag, AND the show-and-acknowledge confirm):
//
//     The mint is held and the Author is told only THAT a collision exists —
//     never which concept, never its words. The dictionary is blocked to the
//     Author for the exchange (agent-side discipline; the engine's part is
//     that the challenge reveals nothing). The Author must independently
//     supply synonyms for what IT means. The engine then does pure set
//     intersection against each colliding concept's full word set (label +
//     synonyms): ANY shared word — it is the same concept, the mint is
//     rejected and the matching concept is revealed as the reuse target;
//     NO shared word — genuinely new, minted. Exact-string overlap only:
//     the moment "overlap" means "close enough in meaning" the determinism
//     is gone. The Author cannot be sycophantic or cheat — it created the
//     original entry, so describing the same thing blind lands on the same
//     words, and a slip dies on its own signature. Pure and deterministic;
//     no Arbiter needed for this decision.
//
//   This check is only as strong as the existing entries' synonym sets —
//   which is why enrichment is NOT "over time, as and when":
//
// SERIALIZED MINTING — ONE CONCEPT AT A TIME (agreed):
//   A concept is not finished at mint; it is finished when its synonym set
//   is EXHAUSTED. Only one concept may be in that open-enrichment state.
//   While one is open, further mints are refused — the Author's priority is
//   completing the entry, then minting continues. At machine speed against
//   human typing this queue drains before the sentence that caused it is
//   finished; it costs nothing real and it means a duplicate can never slip
//   past a thin, just-born signature — the pile-up that would need an
//   Arbiter cannot build up. The Arbiter remains the safety net for the one
//   genuine leftover: several never-before-seen flavours of one concept
//   arriving in a single prompt (agent-side; see pending spec).
//   The one unconditional refusal is unchanged: the same word attached twice
//   to the SAME concept ("cold and cold") — duplication with no legitimate
//   reading.
//
//   Which surface word is SPOKEN when a concept is rendered back to natural
//   language (one flows better than another) is a synthesis-time choice at
//   the Architect's NL boundary — never a registry concern. See
//   SPEC_RAG_Pending_Builds.md.
//
// EVALUATION IS AUTHOR'S IDLE WORK, not an overnight job. Priority: real
// logging always wins immediately; the moment the log queue is empty the
// Author pulls from pending_evaluation(). No clock involved.
//
// RESERVED SYSTEM ENTRIES
//   Seeded once, at first construction of an empty registry, before any Author
//   minting. These are the lifecycle/anchor markers agreed this session:
//   deleted, expired, superseded, failed, callback. The store hardcodes the
//   behaviour of exactly two — deleted (excluded from normal retrieval, still
//   deliberately recallable) and expired (set by the watchdog on natural
//   expiry). Everything else rides through with the Author's meaning.
//
// PERSISTENCE
//   Append-only, through the RagPersistence boundary (rag_persistence.h). The
//   concrete on-disk format is PENDING the OS decision — see
//   SPEC_RAG_Pending_Builds.md. Nothing here assumes a format.
//
// NO SILENT ANYTHING
//   Minting a label that already exists is REFUSED, not deduplicated silently.
//   Relating an ID that doesn't exist is REFUSED. A refusal returns the reason.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace prime::rag {

class RagPersistence; // boundary, see rag_persistence.h

// Fixed IDs of the seeded system entries. Fixed because they are seeded before
// any Author minting can occur, so their positions are structural, not magic.
namespace reserved {
    inline constexpr uint16_t kDeleted    = 0x0000; // hardcoded store behaviour
    inline constexpr uint16_t kExpired    = 0x0001; // hardcoded watchdog target
    inline constexpr uint16_t kSuperseded = 0x0002; // rides through
    inline constexpr uint16_t kFailed     = 0x0003; // failure-recall anchor tag
    inline constexpr uint16_t kCallback   = 0x0004; // personalisation anchor tag
    inline constexpr uint16_t kFirstMintable = 0x0005;
}

enum class ConceptKind : uint8_t {
    Referent = 0, // exists independent of the axes — never evaluated
    Reading  = 1, // made of axis material — flagged for evaluation at mint
};

struct RegistryEntry {
    uint16_t    id = 0;
    std::string label;          // the word it was minted under
    std::vector<std::string> synonyms; // further doors onto the same concept
    ConceptKind kind = ConceptKind::Referent;
    bool        evaluated = false;      // Readings only; Referents are born true
    std::vector<uint16_t> related;      // thesaurus cross-references (joy<->glee)

    bool carries_word(const std::string& w) const {
        if (label == w) return true;
        for (const std::string& s : synonyms) if (s == w) return true;
        return false;
    }
};

// The outcome of an attempted mint. Four honest shapes, never confusable:
//   minted      — new address taken.
//   challenged  — mint HELD: a collision exists. NOTHING about the colliding
//                 concept is revealed (the challenge is blind). The Author
//                 answers through mint_resolve_blind with its own synonyms.
//   matched     — (from mint_resolve_blind) the blind test intersected: this
//                 is an existing concept; `matched` names the reuse target(s),
//                 revealed only now, after the test. Not an error — a verdict.
//   refused     — `refusal` says why (empty label, enrichment open, space
//                 exhausted, persistence, resolve without a collision).
struct MintResult {
    bool        minted = false;
    uint16_t    id = 0;          // valid only when minted
    bool        challenged = false;
    std::vector<uint16_t> matched; // blind-test verdict: reuse these instead
    std::string refusal;         // non-empty only when refused
};

// The outcome of attaching a synonym. A word other concepts carry holds the
// attach until confirmed via add_synonym_confirm, acknowledging the exact
// holders. (The synonym door keeps the acknowledge form: here the Author is
// enriching a concept whose entry it is looking at by necessity — a blind
// exchange is not meaningful at this door.)
struct SynonymResult {
    bool        added = false;
    bool        challenged = false;
    std::vector<uint16_t> challenge;
    std::string refusal;
};

// A word carried by more than one concept — the derived review list. Derived
// on demand from the entries themselves: one copy of the truth, nothing
// stored that could drift against it.
struct WordCollision {
    std::string word;
    std::vector<uint16_t> concept_ids;
};

class IdRegistry {
public:
    IdRegistry() = default;

    // Attach the persistence boundary and load whatever it holds. An empty
    // registry gets the reserved system entries seeded (and persisted).
    // Returns false with reason_out set if the backing store exists but cannot
    // be read — which is a real failure, never presented as "nothing yet".
    bool open(RagPersistence& persistence, std::string& reason_out);

    // DECLARED in-memory operation for the interim before the OS-pending
    // persistence exists. Seeds the reserved entries; nothing survives the
    // process. This is an explicit mode the caller chose, never a fallback
    // the registry slid into — open() failing does NOT route here.
    bool open_in_memory(std::string& reason_out);

    // THE ONE DOOR — step one. Refused while another concept's enrichment is
    // open (serialized minting: finish the entry, then mint the next). A
    // word collision HOLDS the mint blind — the result says only that a
    // collision exists, revealing nothing. No collision: minted, and
    // enrichment opens on the new concept.
    MintResult mint(const std::string& label, ConceptKind kind);

    // THE ONE DOOR — the blind resolve. The Author, dictionary-blocked,
    // supplies its own synonyms for what it means. Pure exact-string set
    // intersection against every colliding concept's word set decides:
    //   intersect  -> not minted; `matched` reveals the reuse target(s).
    //   disjoint   -> minted as genuinely new; enrichment opens.
    // Refused if the word has no collision (use mint()), if the offered set
    // is empty (an empty signature tests nothing and would pass vacuously —
    // that is a hole, not a pass), or while enrichment is open elsewhere.
    MintResult mint_resolve_blind(const std::string& label, ConceptKind kind,
                                  const std::vector<std::string>& offered_synonyms);

    // Close the open concept's enrichment: the Author declares its synonym
    // set exhausted. Only then may the next mint proceed. Refused if `id` is
    // not the concept currently open.
    bool complete_enrichment(uint16_t id, std::string& reason_out);
    std::optional<uint16_t> enrichment_open() const { return enrichment_open_; }

    // Downtime enrichment: attach a surface word. Acknowledge-gated on
    // cross-concept collision (see SynonymResult). Refused unconditionally
    // if the concept already carries the word or is unknown.
    SynonymResult add_synonym(uint16_t id, const std::string& word);
    SynonymResult add_synonym_confirm(uint16_t id, const std::string& word,
                                      const std::vector<uint16_t>& acknowledged);

    // THE THESAURUS READ. Every concept this word opens onto — label or
    // synonym, one entry or several (homonyms). The reader takes all of them
    // and judges from context which applies, exactly as a thesaurus is read.
    std::vector<uint16_t> concepts_for(const std::string& word) const;

    const RegistryEntry* get(uint16_t id) const;

    // Oversight list: every word currently sitting under more than one
    // concept. Because the challenge gate means multi-concept words can only
    // exist through an explicit confirmation, everything here was
    // deliberately confirmed — this is the record of those decisions, derived
    // fresh each call from the entries themselves. One copy of the truth.
    std::vector<WordCollision> word_collisions() const;

    // Downtime evaluation outcome: two EXISTING concepts are related
    // (thesaurus link). Symmetric — recorded on both. Refused with reason if
    // either ID is unknown. This is the ONLY write evaluation ever performs.
    bool relate(uint16_t a, uint16_t b, std::string& reason_out);

    // Mark a Reading's evaluation complete. Refused on a Referent (nothing to
    // evaluate) and on an unknown ID.
    bool mark_evaluated(uint16_t id, std::string& reason_out);

    // The Author's idle-work queue: every Reading not yet evaluated, oldest
    // first (mint order — nothing cleverer was agreed).
    std::vector<uint16_t> pending_evaluation() const;

    size_t count() const { return entries_.size(); }
    const std::vector<RegistryEntry>& all() const { return entries_; }

private:
    bool seed_reserved(std::string& reason_out);
    RegistryEntry* find_mutable(uint16_t id);
    MintResult    mint_write(const std::string& label, ConceptKind kind);
    bool          synonym_precheck(uint16_t id, const std::string& word,
                                   std::string& refusal_out) const;
    SynonymResult synonym_write(uint16_t id, const std::string& word);
    static bool   same_id_set(std::vector<uint16_t> a, std::vector<uint16_t> b);

    // The one concept whose enrichment is open. Serializes minting.
    // NOT yet persisted — after a cold load no enrichment is open; whether
    // that state should survive restart is a persistence-format question
    // parked with the format itself (see pending spec).
    std::optional<uint16_t> enrichment_open_;

    std::vector<RegistryEntry> entries_;   // index != id is impossible: ids are
                                           // sequential from 0, so entries_[id]
                                           // IS the entry. One copy, no map to
                                           // drift against it.
    RagPersistence* persistence_ = nullptr;
    bool open_ = false;
};

} // namespace prime::rag
