// chat_rag.h — Chat RAG: the physical structure
//
// PURPOSE (stated so the function is never lost): every prompt should be able
// to reflect back on all prompts gone by and "remember" them with clarity —
// the illusion of one infinite, perfectly-recalled scrolling context, built
// honestly: the system knows exactly what is relevant to THIS prompt out of
// everything that ever happened, and fetches exactly that. No pollution, no
// drift, no repeating. Fetches are never user-facing; they arm the system.
//
// THE ENTRY IS THE WHOLE TURN. One entry = one turn, written out end to end,
// verbatim, in whatever text the system had it in at the time. Everything is
// kept forever; nothing is ever discarded on the way in.
//
// TAGS ARE SPANS OVER THE TEXT — THE TAG IS THE CONTEXT BOUNDARY.
//   A tag covers a stretch of the turn: exactly the complete context for that
//   tag, end to end — a sentence, a paragraph, whatever it needs to be. Tags
//   are NOT a standard size. They stack: many spans over one turn, freely
//   overlapping; the same tag may cover several separate stretches of the
//   same turn (an overarching tag skipping the in-joke between its segments);
//   a tighter tag may sit inside a wider one when that inner stretch has its
//   own exact relevance.
//
// WHAT GETS TAGGED (Author's discipline, recorded here as the contract):
//   anything a LATER, DIFFERENT prompt could use as context — decisions,
//   things the user liked, failed attempts, abandoned brainstorm branches.
//   What stays untagged: the process that led to a decision, inter-agent
//   chatter, user verbosity, output flair, worked examples — real, kept,
//   and correctly invisible: handing process to a different future prompt is
//   noise at best, actively misleading at worst.
//
// ALL FETCHES ARE EXACT, PRECISE, AND TO THE POINT.
//   A fetch returns tagged SPANS — the precise text each tag covers, nothing
//   before, nothing after. No padding, no "bit surrounding it": the
//   surrounding text is by definition the material the tag was drawn to
//   exclude. There is no anchor-and-expand — that mechanism was removed as
//   solving a problem that must not exist (a correctly drawn tag is already
//   complete).
//
// THE DELIBERATE FULL PULL — "why would I say that?"
//   Normal retrieval hands back the marked context (the decision). When that
//   isn't enough, the whole turn the span lives in — untagged process and
//   all — is read via full_turn(). A separate, deliberate act, never a
//   default, never partial: the complete original document or nothing.
//
// QUERY SHAPES (three real behaviours, not one guessing query):
//   ALL-OF   — the default: a turn must carry every listed tag. Exact
//              intersection context ("coding" AND "RAG" AND "failed").
//   ANY-OF   — deliberate vagueness (rare, not forced away): keywords from a
//              prompt, cast wide, filtered down after.
//   NONE-OF  — exclusion (obsolete, superseded, ...): a candidate span that
//              OVERLAPS a span carrying an excluded tag is dropped. Overlap-
//              based, span-precise, so a live section of a turn survives its
//              obsolete neighbour. [My resolution of span-vs-turn exclusion —
//              flag if turn-level was intended.]
//   Returned spans are those of the requested (all_of/any_of) tags only.
//
// FUZZY SEARCH runs over SPANS: each tagged span may carry its own vector
// (supplied by the Archivist fleet; the store computes nothing, only
// compares). Exact brute-force scan — the DECIDED Phase 1; the ANN SLOT for
// Phase 2 is marked in nearest(). Untagged text is never embedded and never
// fuzzy-found.
//
// VOICE LABELS — provenance, not tags. A second, separate marking system:
//   every user-facing stretch of a turn carries a voice label (User or
//   System), changing every time the speaker changes, however many times
//   that happens. The moment the log moves into inter-agent territory the
//   label is simply absent — no third value, honest absence. Labels are
//   MECHANICAL: derived at write time from which pool the stretch reflected
//   from (user input pool / system output pool) — no judgement, no Author
//   discretion, the same confidence as a timestamp. Only two values ever
//   exist; internal agents are never labelled.
//
//   VOICE IS TAG-ADJACENT, NEVER A TAG. Two search gates, deliberately
//   never mixed (a shared path is a chance for bleed when they overlap):
//   GATE 1 — concept tags. Ordinary retrieval. Voice may NARROW a tag
//     fetch ("what did you tell me about X" = tag X + voice System,
//     overlap-based) but voice alone finds nothing here.
//   GATE 2 — the deliberate voice search. Its own path, its own
//     eligibility: voice-labelled stretches are findable REGARDLESS of
//     concept tags — because the questions this serves ("when did I ask to
//     set that reminder?", "what did I say about ...?") are about turns
//     that would never earn a concept tag: the act of the conversation is
//     itself the context. Content-matched (fuzzy over voice-span vectors)
//     and/or time-bounded — time is optional; sometimes time IS the
//     question. Never part of routine context-casting. If one prompt ever
//     needs both context types: two deliberate trips through two doors,
//     never one merged query.
//
// LIFECYCLE is per-thread, cascaded (deleted / expired / superseded / any
// Author-coined marker). The store hardcodes exactly one behaviour:
// reserved::kDeleted is excluded from all normal retrieval, recallable only
// through the deliberate recall path. Expiry DATA lives here; the watchdog
// drives it.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace prime::rag {

class RagPersistence;

inline constexpr uint16_t kLive = 0xFFFF; // absence of a lifecycle marker

enum class Voice : uint8_t { User = 1, System = 2 };

enum class Tense   : uint8_t { Past = 0, Present = 1, Future = 2 };
enum class Subject : uint8_t { Me = 0, You = 1, Them = 2 };

struct TagRef {
    uint16_t concept_id = 0;                 // registry address
    uint8_t  polarity   = 1;                 // 1 positive, 0 negative
    Tense    tense      = Tense::Present;
    Subject  subject    = Subject::Me;
};

// A voice label over a user-facing stretch of the turn. Mechanical
// provenance from the pool it reflected from. Optional embedding
// (Archivist-supplied) makes the stretch content-searchable through the
// voice gate — the second eligibility route into fuzzy search.
struct VoiceSpan {
    Voice    voice = Voice::User;
    uint32_t begin = 0;
    uint32_t end   = 0;
    std::vector<float> embedding;
};

// A tag laid over a stretch of the turn. THE SPAN IS THE CONTEXT BOUNDARY:
// [begin, end) byte offsets into the turn's content, drawn exactly as wide
// as the complete context for this tag — no wider, no narrower.
struct TagSpan {
    TagRef   tag;
    uint32_t begin = 0;
    uint32_t end   = 0;
    std::vector<float> embedding; // optional, span-level, Archivist-supplied
};

// The coordinate riding with content — extracted once at generation, stored
// settled. absent==true for material that never carried one; honest absence,
// never zero-filled to look like calm neutrality.
struct InflectionRecord {
    bool    absent = true;
    uint8_t confidence = 0;
    uint8_t axes[7] = {0};
};

struct RagEntry {
    uint64_t         entry_id = 0;    // sequential, assigned by the store
    uint64_t         turn_id  = 0;
    std::string      thread_id;       // subject thread — the cascade target
    std::string      content;         // THE WHOLE TURN, verbatim, end to end
    std::vector<TagSpan> spans;       // empty == invisible to GATE 1
    std::vector<VoiceSpan> voices;    // user-facing stretches; GATE 2 material
    InflectionRecord inflection;
    uint16_t         lifecycle = kLive;
    std::string      lifecycle_reason;
    int64_t          timestamp = 0;
};

struct LifecycleEvent {
    std::string thread_id;
    uint16_t    marker = kLive;
    std::string reason;
    int64_t     timestamp = 0;
};

struct ExpiryRecord {
    std::string thread_id;
    int64_t     expiry_ts = 0;
    bool        fired = false;
};

// One exact fetch result: a span and the turn it lives in. text() is the
// precise covered stretch — the complete context, nothing else.
struct SpanHit {
    const RagEntry* entry = nullptr;
    const TagSpan*  span  = nullptr;
    float distance = 0.0f; // meaningful for fuzzy results only
    std::string text() const {
        return entry->content.substr(span->begin, span->end - span->begin);
    }
};

// One voice-gate result: a labelled stretch and the turn it lives in.
struct VoiceHit {
    const RagEntry*  entry = nullptr;
    const VoiceSpan* span  = nullptr;
    float distance = 0.0f; // meaningful for fuzzy results only
    std::string text() const {
        return entry->content.substr(span->begin, span->end - span->begin);
    }
};

// The three query behaviours in one declared shape. all_of empty AND any_of
// empty is a refused query — "fetch everything" is not a thing.
struct TagQuery {
    std::vector<uint16_t> all_of;   // turn must carry EVERY one (default use)
    std::vector<uint16_t> any_of;   // turn must carry AT LEAST one (vague cast)
    std::vector<uint16_t> none_of;  // spans overlapping these are dropped
    std::optional<Voice> voice;     // NARROWING ONLY, PERMANENTLY: decides
                                    // WHICH tag spans qualify (must overlap a
                                    // voice span of this voice); NEVER widens
                                    // WHAT text comes back. A returned hit is
                                    // always exactly the tag span's own
                                    // boundaries — never the voice span's,
                                    // however much wider that voice span is.
                                    // The label is annotation for filtering,
                                    // not an extraction boundary. Widening it
                                    // to "the whole quote for context" would
                                    // reopen exactly the noise this design
                                    // exists to keep out (jokes, chatter
                                    // riding the same voice stretch as a real
                                    // decision). Voice alone finds nothing
                                    // through this gate — see GATE 2 for that.
    // Optional frame narrowing applied to matched (returned) spans:
    std::optional<uint8_t> polarity;
    std::optional<Tense>   tense;
    std::optional<Subject> subject;
};

class ChatRag {
public:
    ChatRag() = default;

    bool open(RagPersistence& persistence, std::string& reason_out);
    bool has_persistence() const { return persistence_ != nullptr; }

    // THE ONE WRITE. The whole turn plus its tag spans and voice labels, in
    // one act. Refuses: span/label offsets outside the content, inverted or
    // mutually overlapping voice labels (speakers alternate; overlap is a
    // caller bug), embedding dimension mismatch. (Tag-id validation against
    // the registry is Access's job at the door.) Turns with no tag spans
    // are legal and expected — invisible to GATE 1, still voice-findable.
    std::optional<uint64_t> append(RagEntry entry, std::string& reason_out);

    // ---- EXACT SEARCH — the tag filter system ----
    // Returns the requested tags' spans from turns satisfying the query.
    // Refuses (empty + reason) an empty query.
    std::vector<SpanHit> find(const TagQuery& q, std::string& reason_out) const;

    // ---- FUZZY SEARCH over spans (Phase 1 exact scan; ANN SLOT inside) ----
    std::vector<SpanHit> nearest(const std::vector<float>& query,
                                 std::optional<float> max_distance,
                                 const std::vector<uint16_t>& none_of,
                                 std::string& reason_out) const;

    // ---- GATE 2: THE DELIBERATE VOICE SEARCH ----
    // Listing by voice, optionally time-bounded (time is optional — sometimes
    // time IS the question, sometimes there is no window at all). Chronological.
    std::vector<VoiceHit> voice_find(Voice v,
                                     std::optional<int64_t> from_ts,
                                     std::optional<int64_t> to_ts) const;
    // Content search ("what did I say about ...?"): fuzzy over voice-span
    // vectors, tagged or not — voice is the sole gate here.
    std::vector<VoiceHit> voice_nearest(const std::vector<float>& query,
                                        Voice v,
                                        std::optional<int64_t> from_ts,
                                        std::optional<int64_t> to_ts,
                                        std::optional<float> max_distance,
                                        std::string& reason_out) const;

    // ---- THE DELIBERATE FULL PULL — the whole original turn, or nothing ----
    const RagEntry* full_turn(uint64_t entry_id) const;

    // ---- THREAD OPERATIONS ----
    std::vector<const RagEntry*> get_thread(const std::string& thread_id,
                                            bool include_deleted = false) const;
    size_t set_thread_state(const std::string& thread_id, uint16_t marker,
                            const std::string& reason,
                            std::string& refusal_out);

    // ---- DELIBERATE RECALL of deleted material ----
    std::vector<SpanHit> recall_deleted(const TagQuery& q,
                                        std::string& reason_out) const;

    // ---- EXPIRY DATA (driven by the watchdog) ----
    bool register_expiry(const std::string& thread_id, int64_t expiry_ts,
                         std::string& reason_out);
    std::vector<ExpiryRecord*> due_expiries(int64_t now);
    size_t pending_expiry_count() const;

    size_t count() const { return entries_.size(); }

private:
    bool turn_carries(const RagEntry& e, uint16_t concept_id) const;
    bool span_excluded(const RagEntry& e, const TagSpan& s,
                       const std::vector<uint16_t>& none_of) const;
    bool span_frame_ok(const TagSpan& s, const TagQuery& q) const;
    bool span_voice_ok(const RagEntry& e, const TagSpan& s,
                       const TagQuery& q) const;
    bool gate(const RagEntry& e) const; // has spans, not deleted

    std::vector<RagEntry>     entries_;
    std::vector<ExpiryRecord> expiries_;
    size_t vector_dim_ = 0;
    RagPersistence* persistence_ = nullptr;
};

} // namespace prime::rag
