// chat_rag.cpp — Chat RAG: the physical structure. See chat_rag.h for design.

#include "chat_rag.h"
#include "id_registry.h"     // reserved::kDeleted
#include "rag_persistence.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace prime::rag {

static float cosine_distance(const std::vector<float>& a,
                             const std::vector<float>& b) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na  += static_cast<double>(a[i]) * a[i];
        nb  += static_cast<double>(b[i]) * b[i];
    }
    if (na == 0.0 || nb == 0.0) return 1.0f; // zero vector matches nothing;
                                             // reported as distant, not error
    return 1.0f - static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

bool ChatRag::open(RagPersistence& persistence, std::string& reason_out) {
    std::vector<RagEntry>       entries;
    std::vector<LifecycleEvent> events;
    std::vector<ExpiryRecord>   expiries;
    if (!persistence.load_entries(entries, events, expiries, reason_out))
        return false; // real read failure, reported — never "nothing yet"

    entries_  = std::move(entries);
    expiries_ = std::move(expiries);

    // Replay lifecycle events in order — the append-only record of every
    // cascade since the beginning. Entry state is derived, never stored twice.
    for (const LifecycleEvent& ev : events) {
        for (RagEntry& e : entries_) {
            if (e.thread_id == ev.thread_id) {
                e.lifecycle        = ev.marker;
                e.lifecycle_reason = ev.reason;
            }
        }
    }

    for (const RagEntry& e : entries_) {
        for (const TagSpan& s : e.spans)
            if (!s.embedding.empty()) { vector_dim_ = s.embedding.size(); break; }
        if (!vector_dim_)
            for (const VoiceSpan& v : e.voices)
                if (!v.embedding.empty()) { vector_dim_ = v.embedding.size(); break; }
        if (vector_dim_) break;
    }

    persistence_ = &persistence;
    return true;
}

std::optional<uint64_t> ChatRag::append(RagEntry entry,
                                        std::string& reason_out) {
    // Span discipline: every span must lie inside the turn and cover a real
    // stretch. A malformed span is refused whole — a boundary that lies about
    // where the context ends would poison every fetch that ever matched it.
    for (const TagSpan& s : entry.spans) {
        if (s.begin >= s.end) {
            reason_out = "span with begin >= end refused";
            return std::nullopt;
        }
        if (s.end > entry.content.size()) {
            reason_out = "span [" + std::to_string(s.begin) + "," +
                         std::to_string(s.end) + ") exceeds turn length " +
                         std::to_string(entry.content.size()) + " — refused";
            return std::nullopt;
        }
        if (!s.embedding.empty()) {
            if (vector_dim_ == 0) {
                vector_dim_ = s.embedding.size();
            } else if (s.embedding.size() != vector_dim_) {
                reason_out = "span embedding dimension " +
                             std::to_string(s.embedding.size()) +
                             " does not match established dimension " +
                             std::to_string(vector_dim_);
                return std::nullopt;
            }
        }
    }

    // Voice-label discipline: same geometry rules, plus no two labels may
    // overlap — speakers alternate; an overlap is a caller bug refused whole.
    for (size_t i = 0; i < entry.voices.size(); ++i) {
        const VoiceSpan& v = entry.voices[i];
        if (v.begin >= v.end) {
            reason_out = "voice label with begin >= end refused";
            return std::nullopt;
        }
        if (v.end > entry.content.size()) {
            reason_out = "voice label exceeds turn length — refused";
            return std::nullopt;
        }
        if (!v.embedding.empty()) {
            if (vector_dim_ == 0) {
                vector_dim_ = v.embedding.size();
            } else if (v.embedding.size() != vector_dim_) {
                reason_out = "voice label embedding dimension mismatch";
                return std::nullopt;
            }
        }
        for (size_t j = i + 1; j < entry.voices.size(); ++j) {
            const VoiceSpan& w = entry.voices[j];
            if (v.begin < w.end && w.begin < v.end) {
                reason_out = "overlapping voice labels refused — speakers "
                             "alternate; labels never overlap";
                return std::nullopt;
            }
        }
    }

    entry.entry_id = static_cast<uint64_t>(entries_.size());
    if (entry.timestamp == 0)
        entry.timestamp = static_cast<int64_t>(::time(nullptr));

    if (persistence_ && !persistence_->append_entry(entry)) {
        reason_out = "persistence refused the entry append";
        return std::nullopt; // memory untouched — no split truth
    }

    entries_.push_back(std::move(entry));
    return entries_.back().entry_id;
}

bool ChatRag::gate(const RagEntry& e) const {
    return !e.spans.empty() && e.lifecycle != reserved::kDeleted;
}

bool ChatRag::turn_carries(const RagEntry& e, uint16_t concept_id) const {
    for (const TagSpan& s : e.spans)
        if (s.tag.concept_id == concept_id) return true;
    return false;
}

bool ChatRag::span_excluded(const RagEntry& e, const TagSpan& s,
                            const std::vector<uint16_t>& none_of) const {
    if (none_of.empty()) return false;
    // Overlap-based, span-precise: a candidate span is dropped only where an
    // excluded tag's span actually overlaps it — a live section of a turn
    // survives its obsolete neighbour.
    for (const TagSpan& x : e.spans) {
        bool is_excluded_tag = false;
        for (uint16_t id : none_of)
            if (x.tag.concept_id == id) { is_excluded_tag = true; break; }
        if (!is_excluded_tag) continue;
        const bool overlaps = (x.begin < s.end) && (s.begin < x.end);
        if (overlaps) return true;
    }
    return false;
}

bool ChatRag::span_voice_ok(const RagEntry& e, const TagSpan& s,
                            const TagQuery& q) const {
    if (!q.voice) return true;
    // Narrowing only: the tag span must overlap a voice label of the asked
    // voice. Voice alone never qualifies anything through this gate.
    for (const VoiceSpan& v : e.voices) {
        if (v.voice != *q.voice) continue;
        if (v.begin < s.end && s.begin < v.end) return true;
    }
    return false;
}

bool ChatRag::span_frame_ok(const TagSpan& s, const TagQuery& q) const {
    if (q.polarity && s.tag.polarity != *q.polarity) return false;
    if (q.tense    && s.tag.tense    != *q.tense)    return false;
    if (q.subject  && s.tag.subject  != *q.subject)  return false;
    return true;
}

std::vector<SpanHit> ChatRag::find(const TagQuery& q,
                                   std::string& reason_out) const {
    std::vector<SpanHit> out;
    if (q.all_of.empty() && q.any_of.empty()) {
        reason_out = "empty query refused — \"fetch everything\" is not a "
                     "thing; name the tags";
        return out;
    }

    for (const RagEntry& e : entries_) {
        if (!gate(e)) continue;

        bool ok = true;
        for (uint16_t id : q.all_of)
            if (!turn_carries(e, id)) { ok = false; break; }
        if (!ok) continue;
        if (!q.any_of.empty()) {
            bool any = false;
            for (uint16_t id : q.any_of)
                if (turn_carries(e, id)) { any = true; break; }
            if (!any) continue;
        }

        // Return the REQUESTED tags' spans — the exact context asked for,
        // nothing before, nothing after.
        for (const TagSpan& s : e.spans) {
            bool requested = false;
            for (uint16_t id : q.all_of)
                if (s.tag.concept_id == id) { requested = true; break; }
            if (!requested)
                for (uint16_t id : q.any_of)
                    if (s.tag.concept_id == id) { requested = true; break; }
            if (!requested) continue;
            if (!span_frame_ok(s, q)) continue;
            if (!span_voice_ok(e, s, q)) continue;
            if (span_excluded(e, s, q.none_of)) continue;
            out.push_back({&e, &s, 0.0f});
        }
    }
    return out;
}

std::vector<SpanHit> ChatRag::nearest(const std::vector<float>& query,
                                      std::optional<float> max_distance,
                                      const std::vector<uint16_t>& none_of,
                                      std::string& reason_out) const {
    std::vector<SpanHit> out;
    if (vector_dim_ == 0) { reason_out = "store holds no vectors yet"; return out; }
    if (query.size() != vector_dim_) {
        reason_out = "query dimension " + std::to_string(query.size()) +
                     " does not match store dimension " +
                     std::to_string(vector_dim_);
        return out;
    }

    // ---- PHASE 1: exact scan over spans of gate-passing turns.        ----
    // ---- ANN SLOT: the Phase 2 approximate structure replaces ONLY    ----
    // ---- the candidate walk below; contract identical (vector in,     ----
    // ---- span hits out). Build behind this line when the pile has     ----
    // ---- earned it; its index rebuild is a pre-staged watchdog job.   ----
    for (const RagEntry& e : entries_) {
        if (!gate(e)) continue;
        for (const TagSpan& s : e.spans) {
            if (s.embedding.empty()) continue;
            if (span_excluded(e, s, none_of)) continue;
            float d = cosine_distance(query, s.embedding);
            if (max_distance && d > *max_distance) continue;
            out.push_back({&e, &s, d});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const SpanHit& a, const SpanHit& b) {
                  return a.distance < b.distance;
              });
    return out;
}

const RagEntry* ChatRag::full_turn(uint64_t entry_id) const {
    // The deliberate act: the complete original turn, untagged process and
    // all — or nothing. Deleted turns stay behind the recall path.
    if (entry_id >= entries_.size()) return nullptr;
    const RagEntry& e = entries_[entry_id];
    if (e.lifecycle == reserved::kDeleted) return nullptr;
    return &e;
}

std::vector<const RagEntry*> ChatRag::get_thread(const std::string& thread_id,
                                                 bool include_deleted) const {
    std::vector<const RagEntry*> out;
    for (const RagEntry& e : entries_) {
        if (e.thread_id != thread_id) continue;
        if (!include_deleted && e.lifecycle == reserved::kDeleted) continue;
        out.push_back(&e);
    }
    return out;
}

size_t ChatRag::set_thread_state(const std::string& thread_id, uint16_t marker,
                                 const std::string& reason,
                                 std::string& refusal_out) {
    size_t touched = 0;
    for (RagEntry& e : entries_)
        if (e.thread_id == thread_id) ++touched;
    if (touched == 0) {
        refusal_out = "unknown thread '" + thread_id + "' — cascade refused";
        return 0;
    }

    LifecycleEvent ev;
    ev.thread_id = thread_id;
    ev.marker    = marker;
    ev.reason    = reason;
    ev.timestamp = static_cast<int64_t>(::time(nullptr));
    if (persistence_ && !persistence_->append_lifecycle_event(ev)) {
        refusal_out = "persistence refused the lifecycle event";
        return 0;
    }

    for (RagEntry& e : entries_) {
        if (e.thread_id == thread_id) {
            e.lifecycle        = marker;
            e.lifecycle_reason = reason;
        }
    }
    return touched;
}

std::vector<SpanHit> ChatRag::recall_deleted(const TagQuery& q,
                                             std::string& reason_out) const {
    std::vector<SpanHit> out;
    if (q.all_of.empty() && q.any_of.empty()) {
        reason_out = "empty query refused";
        return out;
    }
    for (const RagEntry& e : entries_) {
        if (e.lifecycle != reserved::kDeleted || e.spans.empty()) continue;
        bool ok = true;
        for (uint16_t id : q.all_of)
            if (!turn_carries(e, id)) { ok = false; break; }
        if (!ok) continue;
        if (!q.any_of.empty()) {
            bool any = false;
            for (uint16_t id : q.any_of)
                if (turn_carries(e, id)) { any = true; break; }
            if (!any) continue;
        }
        for (const TagSpan& s : e.spans) {
            bool requested = false;
            for (uint16_t id : q.all_of)
                if (s.tag.concept_id == id) { requested = true; break; }
            if (!requested)
                for (uint16_t id : q.any_of)
                    if (s.tag.concept_id == id) { requested = true; break; }
            if (!requested) continue;
            if (!span_frame_ok(s, q)) continue;
            if (!span_voice_ok(e, s, q)) continue;
            out.push_back({&e, &s, 0.0f});
        }
    }
    return out;
}

std::vector<VoiceHit> ChatRag::voice_find(Voice v,
                                          std::optional<int64_t> from_ts,
                                          std::optional<int64_t> to_ts) const {
    // GATE 2: voice is the sole qualification. Concept tags are irrelevant
    // here — the turns this serves are precisely the ones that never earned
    // one. Deleted stays behind the recall path, as everywhere.
    std::vector<VoiceHit> out;
    for (const RagEntry& e : entries_) {
        if (e.lifecycle == reserved::kDeleted) continue;
        if (from_ts && e.timestamp < *from_ts) continue;
        if (to_ts   && e.timestamp > *to_ts)   continue;
        for (const VoiceSpan& s : e.voices)
            if (s.voice == v) out.push_back({&e, &s, 0.0f});
    }
    std::sort(out.begin(), out.end(),
              [](const VoiceHit& a, const VoiceHit& b) {
                  if (a.entry->timestamp != b.entry->timestamp)
                      return a.entry->timestamp < b.entry->timestamp;
                  if (a.entry->entry_id != b.entry->entry_id)
                      return a.entry->entry_id < b.entry->entry_id;
                  return a.span->begin < b.span->begin;
              });
    return out;
}

std::vector<VoiceHit> ChatRag::voice_nearest(const std::vector<float>& query,
                                             Voice v,
                                             std::optional<int64_t> from_ts,
                                             std::optional<int64_t> to_ts,
                                             std::optional<float> max_distance,
                                             std::string& reason_out) const {
    std::vector<VoiceHit> out;
    if (vector_dim_ == 0) { reason_out = "store holds no vectors yet"; return out; }
    if (query.size() != vector_dim_) {
        reason_out = "query dimension does not match store dimension";
        return out;
    }
    for (const RagEntry& e : entries_) {
        if (e.lifecycle == reserved::kDeleted) continue;
        if (from_ts && e.timestamp < *from_ts) continue;
        if (to_ts   && e.timestamp > *to_ts)   continue;
        for (const VoiceSpan& s : e.voices) {
            if (s.voice != v || s.embedding.empty()) continue;
            float d = cosine_distance(query, s.embedding);
            if (max_distance && d > *max_distance) continue;
            out.push_back({&e, &s, d});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const VoiceHit& a, const VoiceHit& b) {
                  return a.distance < b.distance;
              });
    return out;
}

bool ChatRag::register_expiry(const std::string& thread_id, int64_t expiry_ts,
                              std::string& reason_out) {
    bool known = false;
    for (const RagEntry& e : entries_)
        if (e.thread_id == thread_id) { known = true; break; }
    if (!known) {
        reason_out = "unknown thread '" + thread_id + "' — expiry refused";
        return false;
    }
    ExpiryRecord rec{thread_id, expiry_ts, false};
    if (persistence_ && !persistence_->append_expiry(rec)) {
        reason_out = "persistence refused the expiry record";
        return false;
    }
    expiries_.push_back(rec);
    return true;
}

std::vector<ExpiryRecord*> ChatRag::due_expiries(int64_t now) {
    std::vector<ExpiryRecord*> out;
    for (ExpiryRecord& r : expiries_)
        if (!r.fired && r.expiry_ts <= now) out.push_back(&r);
    return out;
}

size_t ChatRag::pending_expiry_count() const {
    size_t n = 0;
    for (const ExpiryRecord& r : expiries_)
        if (!r.fired) ++n;
    return n;
}

} // namespace prime::rag
