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

struct ResidentEntry {
    std::string id;
    std::string text;
};

class ResidentContext {
public:
    ResidentContext() = default;

    void set(std::vector<ResidentEntry> entries) { entries_ = std::move(entries); }

    void upsert(const std::string& id, const std::string& text);
    bool remove(const std::string& id);

    const std::vector<ResidentEntry>& entries() const { return entries_; }
    bool   empty() const { return entries_.empty(); }
    size_t size()  const { return entries_.size(); }

    void clear() { entries_.clear(); }

private:
    std::vector<ResidentEntry> entries_;
};

}
