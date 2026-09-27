// resident_context.h — Architect's cross-turn context carrier (frontier)
//
// The shared_context carried turn-to-turn by relevance. The frontend HOLDS this
// so it survives the round trip between turns; it does not AUTHOR it. Authority
// is the Architect's: the Architect populates, audits, and prunes it every
// ingestion for contextual awareness. The frontend is custodian of the data, not
// its owner (your ruling; corrects HANDOVER_Session22's "engine concern"
// framing, which was wrong — it is neither backend- nor frontend-owned, it is
// Architect-owned and frontend-carried).
//
// WHY IT LIVES IN FRONTEND STATE
//   It has to persist across turns and the frontend is the thing that persists
//   across turns. That is the whole reason it is here. This is not the frontend
//   managing context residency — the Architect does that. It is the frontend
//   keeping the carrier alive between the turns the Architect audits it in.
//
// GROWTH
//   Empty set until the Archivist feeds real chunks in. The residency machinery
//   operates on an empty set until then without special-casing. Retrieval
//   integration lands here later; the shape holds it.
//
// SCOPE
//   An ordered list of { id, text } entries. id is the entry's stable handle so
//   the Architect can address a specific entry to prune or update it without
//   positional fragility. Order is preserved because relevance ordering is
//   meaningful to the carrier.

#pragma once

#include <string>
#include <vector>

namespace prime::frontend {

// One carried context entry. id is stable for the entry's lifetime so the
// Architect can target it by handle across audits.
struct ResidentEntry {
    std::string id;
    std::string text;
};

// The cross-turn carrier. Mutated by the Architect bridge (add/prune/replace);
// read wherever the current turn's shared context is assembled. Plain custody —
// this struct holds and hands back, it does not decide what belongs.
class ResidentContext {
public:
    ResidentContext() = default;

    // Replace the whole set — the Architect's audit result for a turn. This is
    // the common path: the Architect re-derives the relevant set each ingestion
    // and hands back the pruned result wholesale.
    void set(std::vector<ResidentEntry> entries) { entries_ = std::move(entries); }

    // Targeted mutation by handle, for incremental adjustment when a full
    // re-derive is not warranted.
    void upsert(const std::string& id, const std::string& text);
    bool remove(const std::string& id);

    const std::vector<ResidentEntry>& entries() const { return entries_; }
    bool   empty() const { return entries_.empty(); }
    size_t size()  const { return entries_.size(); }

    void clear() { entries_.clear(); }

private:
    std::vector<ResidentEntry> entries_;
};

} // namespace prime::frontend
