// resident_context.cpp — Architect's cross-turn context carrier implementation (frontier)

#include "resident_context.h"

namespace prime::frontend {

void ResidentContext::upsert(const std::string& id, const std::string& text) {
    for (auto& e : entries_) {
        if (e.id == id) {
            e.text = text;
            return;
        }
    }
    entries_.push_back(ResidentEntry{id, text});
}

bool ResidentContext::remove(const std::string& id) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == id) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

}
