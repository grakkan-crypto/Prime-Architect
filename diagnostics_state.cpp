// diagnostics_state.cpp — Diagnostics and journey logging implementation (frontier)

#include "diagnostics_state.h"

namespace prime::frontend {

// ---------------------------------------------------------------------------
// DiagnosticsLog
// ---------------------------------------------------------------------------
void DiagnosticsLog::write(const std::string& text, bool is_error) {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back(DiagnosticsLine{text, is_error});
    while (lines_.size() > capacity_) lines_.pop_front();
}

std::vector<DiagnosticsLine> DiagnosticsLog::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<DiagnosticsLine>(lines_.begin(), lines_.end());
}

std::size_t DiagnosticsLog::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lines_.size();
}

void DiagnosticsLog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
}

// ---------------------------------------------------------------------------
// JourneyLog
// ---------------------------------------------------------------------------
void JourneyLog::append(const JourneyEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(entry);
}

void JourneyLog::append(std::string agent, std::string input,
                        std::string cot, std::string output) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(JourneyEntry{std::move(agent), std::move(input),
                                    std::move(cot), std::move(output)});
}

std::vector<JourneyEntry> JourneyLog::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

std::size_t JourneyLog::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void JourneyLog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

} // namespace prime::frontend
