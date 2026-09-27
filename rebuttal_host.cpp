// rebuttal_host.cpp — Rebuttal window host implementation

#include "rebuttal_host.h"

#include "rebuttal_turn.h"

#include <algorithm>

namespace prime::frontend {

void RebuttalHost::add_row(const std::string& turn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Row r;
    r.turn_id = turn_id;
    r.bold    = rows_.empty(); // the only row IS the current one
    rows_.push_back(std::move(r));
}

void RebuttalHost::set_subject(const std::string& turn_id, std::string subject) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : rows_) {
        if (r.turn_id == turn_id) {
            r.subject = std::move(subject);
            return;
        }
    }
}

void RebuttalHost::append_input(const std::string& turn_id, std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : rows_) {
        if (r.turn_id == turn_id) {
            r.inputs.push_back(std::move(text));
            return;
        }
    }
}

void RebuttalHost::advance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (rows_.empty()) return;
    // Strict creation order, wrapping. Bold repainted whole: one on, the
    // rest off — the event, once.
    size_t current = 0;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].bold) { current = i; break; }
    const size_t next = (current + 1) % rows_.size();
    for (size_t i = 0; i < rows_.size(); ++i) rows_[i].bold = (i == next);
}

void RebuttalHost::remove(const std::string& turn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(rows_.begin(), rows_.end(),
                           [&](const Row& r) { return r.turn_id == turn_id; });
    if (it == rows_.end()) return;

    const bool was_bold = it->bold;
    const size_t idx    = (size_t)(it - rows_.begin());
    rows_.erase(it);

    // Bold passes along the same ruled cycle order — the row now sitting
    // where the removed one was (wrapping).
    if (was_bold && !rows_.empty()) {
        const size_t next = idx % rows_.size();
        for (size_t i = 0; i < rows_.size(); ++i) rows_[i].bold = (i == next);
    }
}

void RebuttalHost::dismiss(const std::string& turn_id) {
    // UI door: the row dies here first...
    remove(turn_id);
    // ...then RebuttalTurn is told the id and that it is closed. It
    // resolves; this file's part is over.
    if (turn_) turn_->closed_from_ui(turn_id);
}

std::string RebuttalHost::current_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& r : rows_)
        if (r.bold) return r.turn_id;
    return {};
}

size_t RebuttalHost::row_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rows_.size();
}

std::vector<RebuttalHost::Row> RebuttalHost::rows() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rows_;
}

} // namespace prime::frontend
