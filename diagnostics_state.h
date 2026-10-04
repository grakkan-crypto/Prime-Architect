// diagnostics_state.h — Diagnostics and journey logging (frontier)
//
// The backing data for the diagnostics panel. Because the native frontend is
// one process with the engine (SPEC_Connection_Architecture §3.1), the inference
// layer writes here DIRECTLY — there is no IDiagnostics interface, no sink
// abstraction, no transport (HANDOVER_Session24 §3.4). Anything in the process
// writes; the panel reads. This header is the whole contract.
//
// TWO LOGS, TWO SHAPES (HANDOVER_Session24 §3.4)
//   DiagnosticsLog — a bounded ring of short lines, each flagged error or not.
//                    Bounded because it is a running feed: old lines age out,
//                    the panel shows recent activity, memory is capped.
//   JourneyLog     — append-only, structured per agent step: the input a step
//                    received, its chain-of-thought, and its output. This is the
//                    audit trail of a turn's reasoning, not a scrolling feed, so
//                    it is not a ring — entries are kept for the session.
//
// THREAD SAFETY
//   Writers are on whatever thread the inference step runs on; the reader is the
//   UI thread. Both logs guard their own state. Callers do not lock.

#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace prime::frontend {

struct DiagnosticsLine {
    std::string text;
    bool        is_error = false;
};

class DiagnosticsLog {
public:
    explicit DiagnosticsLog(std::size_t capacity = kDefaultCapacity)
        : capacity_(capacity == 0 ? kDefaultCapacity : capacity) {}

    void info (const std::string& text) { write(text, false); }
    void error(const std::string& text) { write(text, true);  }
    void write(const std::string& text, bool is_error);

    std::vector<DiagnosticsLine> snapshot() const;

    std::size_t size() const;
    void        clear();

    static constexpr std::size_t kDefaultCapacity = 2048;

private:
    mutable std::mutex          mutex_;
    std::deque<DiagnosticsLine> lines_;
    std::size_t                 capacity_;
};

struct JourneyEntry {
    std::string agent;
    std::string input;
    std::string cot;
    std::string output;
};

class JourneyLog {
public:
    JourneyLog() = default;

    void append(const JourneyEntry& entry);
    void append(std::string agent, std::string input,
                std::string cot, std::string output);

    std::vector<JourneyEntry> snapshot() const;
    std::size_t               size() const;
    void                      clear();

private:
    mutable std::mutex        mutex_;
    std::vector<JourneyEntry> entries_;
};

}
