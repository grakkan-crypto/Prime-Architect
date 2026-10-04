// id_registry.cpp — the Prime Dictionary. See id_registry.h for design.

#include "id_registry.h"
#include "rag_persistence.h"

#include <algorithm>

namespace prime::rag {

bool IdRegistry::open(RagPersistence& persistence, std::string& reason_out) {
    std::vector<RegistryEntry> loaded;
    if (!persistence.load_registry(loaded, reason_out))
        return false;

    persistence_ = &persistence;
    entries_ = std::move(loaded);

    if (entries_.empty()) {
        if (!seed_reserved(reason_out)) return false;
    }

    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id != static_cast<uint16_t>(i)) {
            reason_out = "registry corrupt: entry at position " +
                         std::to_string(i) + " carries id " +
                         std::to_string(entries_[i].id);
            entries_.clear();
            persistence_ = nullptr;
            return false;
        }
    }

    open_ = true;
    return true;
}

bool IdRegistry::open_in_memory(std::string& reason_out) {
    if (open_) { reason_out = "registry already open"; return false; }
    persistence_ = nullptr;
    entries_.clear();
    if (!seed_reserved(reason_out)) return false;
    open_ = true;
    return true;
}

bool IdRegistry::seed_reserved(std::string& reason_out) {
    struct Seed { const char* label; };

    static const Seed kSeeds[] = {
        {"__deleted"},
        {"__expired"},
        {"__superseded"},
        {"__failed"},
        {"__callback"},
    };
    for (const Seed& s : kSeeds) {
        RegistryEntry e;
        e.id        = static_cast<uint16_t>(entries_.size());
        e.label     = s.label;
        e.kind      = ConceptKind::Referent;
        e.evaluated = true;
        if (persistence_ && !persistence_->append_registry_entry(e)) {
            reason_out = "persistence refused reserved seed '" + e.label + "'";
            return false;
        }
        entries_.push_back(std::move(e));
    }
    return true;
}

std::vector<uint16_t> IdRegistry::concepts_for(const std::string& word) const {
    std::vector<uint16_t> out;
    for (const RegistryEntry& e : entries_)
        if (e.carries_word(word)) out.push_back(e.id);
    return out;
}

MintResult IdRegistry::mint(const std::string& label, ConceptKind kind) {
    MintResult r;
    if (label.empty()) { r.refusal = "empty label refused"; return r; }
    if (enrichment_open_) {
        r.refusal = "concept " + std::to_string(*enrichment_open_) +
                    " enrichment incomplete — one mint at a time; finish the "
                    "entry, then mint";
        return r;
    }
    if (!concepts_for(label).empty()) {

        r.challenged = true;
        return r;
    }
    return mint_write(label, kind);
}

MintResult IdRegistry::mint_resolve_blind(
        const std::string& label, ConceptKind kind,
        const std::vector<std::string>& offered_synonyms) {
    MintResult r;
    if (label.empty()) { r.refusal = "empty label refused"; return r; }
    if (enrichment_open_) {
        r.refusal = "concept " + std::to_string(*enrichment_open_) +
                    " enrichment incomplete — one mint at a time";
        return r;
    }
    std::vector<uint16_t> holders = concepts_for(label);
    if (holders.empty()) {
        r.refusal = "'" + label + "' has no collision — nothing to resolve; "
                    "use mint()";
        return r;
    }
    if (offered_synonyms.empty()) {

        r.refusal = "blind challenge requires the Author's own synonyms; an "
                    "empty offer tests nothing";
        return r;
    }

    for (uint16_t hid : holders) {
        const RegistryEntry* h = get(hid);
        for (const std::string& w : offered_synonyms) {
            if (h->carries_word(w)) { r.matched.push_back(hid); break; }
        }
    }
    if (!r.matched.empty()) {

        r.refusal = "blind test matched existing concept(s) — same concept; "
                    "reuse the matched id";
        return r;
    }

    return mint_write(label, kind);
}

bool IdRegistry::complete_enrichment(uint16_t id, std::string& reason_out) {
    if (!enrichment_open_) {
        reason_out = "no enrichment is open";
        return false;
    }
    if (*enrichment_open_ != id) {
        reason_out = "enrichment open on concept " +
                     std::to_string(*enrichment_open_) + ", not " +
                     std::to_string(id);
        return false;
    }
    enrichment_open_.reset();
    return true;
}

MintResult IdRegistry::mint_write(const std::string& label, ConceptKind kind) {
    MintResult r;
    if (entries_.size() > 0xFFFF) {
        r.refusal = "concept space exhausted (65536)";
        return r;
    }
    RegistryEntry e;
    e.id        = static_cast<uint16_t>(entries_.size());

    e.label     = label;
    e.kind      = kind;
    e.evaluated = (kind == ConceptKind::Referent);

    if (persistence_ && !persistence_->append_registry_entry(e)) {
        r.refusal = "persistence refused the mint";
        return r;
    }
    entries_.push_back(e);
    r.minted = true;
    r.id     = e.id;

    enrichment_open_ = e.id;
    return r;
}

SynonymResult IdRegistry::add_synonym(uint16_t id, const std::string& word) {
    SynonymResult r;
    std::string refusal;
    if (!synonym_precheck(id, word, refusal)) { r.refusal = refusal; return r; }

    std::vector<uint16_t> others;
    for (const RegistryEntry& other : entries_)
        if (other.id != id && other.carries_word(word))
            others.push_back(other.id);
    if (!others.empty()) {
        r.challenged = true;
        r.challenge  = std::move(others);
        return r;
    }
    return synonym_write(id, word);
}

SynonymResult IdRegistry::add_synonym_confirm(uint16_t id,
                                              const std::string& word,
                                              const std::vector<uint16_t>& acknowledged) {
    SynonymResult r;
    std::string refusal;
    if (!synonym_precheck(id, word, refusal)) { r.refusal = refusal; return r; }

    std::vector<uint16_t> others;
    for (const RegistryEntry& other : entries_)
        if (other.id != id && other.carries_word(word))
            others.push_back(other.id);
    if (others.empty()) {
        r.refusal = "'" + word + "' has no collisions — nothing to confirm; "
                    "use add_synonym()";
        return r;
    }
    if (!same_id_set(others, acknowledged)) {
        r.refusal = "acknowledged collisions do not match the current "
                    "collision set for '" + word + "' — confirmation refused";
        return r;
    }
    return synonym_write(id, word);
}

bool IdRegistry::synonym_precheck(uint16_t id, const std::string& word,
                                  std::string& refusal_out) const {
    const RegistryEntry* e = get(id);
    if (!e)           { refusal_out = "unknown concept id"; return false; }
    if (word.empty()) { refusal_out = "empty word refused"; return false; }
    if (e->carries_word(word)) {

        refusal_out = "concept " + std::to_string(id) + " already carries '" +
                      word + "' — duplication refused";
        return false;
    }
    return true;
}

SynonymResult IdRegistry::synonym_write(uint16_t id, const std::string& word) {
    SynonymResult r;
    if (persistence_ && !persistence_->append_registry_synonym(id, word)) {
        r.refusal = "persistence refused the synonym";
        return r;
    }
    find_mutable(id)->synonyms.push_back(word);
    r.added = true;
    return r;
}

bool IdRegistry::same_id_set(std::vector<uint16_t> a, std::vector<uint16_t> b) {
    if (a.size() != b.size()) return false;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

std::vector<WordCollision> IdRegistry::word_collisions() const {
    std::vector<WordCollision> out;

    for (const RegistryEntry& e : entries_) {
        auto consider = [&](const std::string& w) {
            for (const WordCollision& existing : out)
                if (existing.word == w) return;
            std::vector<uint16_t> holders;
            for (const RegistryEntry& other : entries_)
                if (other.carries_word(w)) holders.push_back(other.id);
            if (holders.size() > 1) out.push_back({w, std::move(holders)});
        };
        consider(e.label);
        for (const std::string& s : e.synonyms) consider(s);
    }
    return out;
}

const RegistryEntry* IdRegistry::get(uint16_t id) const {
    if (id >= entries_.size()) return nullptr;
    return &entries_[id];
}

RegistryEntry* IdRegistry::find_mutable(uint16_t id) {
    if (id >= entries_.size()) return nullptr;
    return &entries_[id];
}

bool IdRegistry::relate(uint16_t a, uint16_t b, std::string& reason_out) {
    RegistryEntry* ea = find_mutable(a);
    RegistryEntry* eb = find_mutable(b);
    if (!ea || !eb) {
        reason_out = "relation refused: unknown id";
        return false;
    }
    if (a == b) {
        reason_out = "relation refused: a concept cannot relate to itself";
        return false;
    }
    const bool already =
        std::find(ea->related.begin(), ea->related.end(), b) != ea->related.end();
    if (already) {
        reason_out = "relation already recorded";
        return false;
    }
    if (persistence_ && !persistence_->append_registry_relation(a, b)) {
        reason_out = "persistence refused the relation";
        return false;
    }
    ea->related.push_back(b);
    eb->related.push_back(a);
    return true;
}

bool IdRegistry::mark_evaluated(uint16_t id, std::string& reason_out) {
    RegistryEntry* e = find_mutable(id);
    if (!e) { reason_out = "unknown id"; return false; }
    if (e->kind == ConceptKind::Referent) {
        reason_out = "referents are never evaluated — nothing to mark";
        return false;
    }
    if (e->evaluated) {
        reason_out = "already evaluated";
        return false;
    }
    if (persistence_ && !persistence_->append_registry_evaluated(id)) {
        reason_out = "persistence refused the evaluation mark";
        return false;
    }
    e->evaluated = true;
    return true;
}

std::vector<uint16_t> IdRegistry::pending_evaluation() const {
    std::vector<uint16_t> out;
    for (const RegistryEntry& e : entries_)
        if (e.kind == ConceptKind::Reading && !e.evaluated)
            out.push_back(e.id);
    return out;
}

}
