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

// One diagnostics line. is_error lets the panel colour/filter without parsing
// the text. Kept minimal — this is a feed line, not a record.
struct DiagnosticsLine {
    std::string text;
    bool        is_error = false;
};

// A bounded ring of diagnostics lines. When full, the oldest line is dropped as
// a new one arrives. Capacity is fixed at construction; the feed never grows
// without bound regardless of session length.
class DiagnosticsLog {
public:
    explicit DiagnosticsLog(std::size_t capacity = kDefaultCapacity)
        : capacity_(capacity == 0 ? kDefaultCapacity : capacity) {}

    // Append a line, evicting the oldest if at capacity. info() and error() are
    // the ordinary call sites; write() takes the flag directly.
    void info (const std::string& text) { write(text, false); }
    void error(const std::string& text) { write(text, true);  }
    void write(const std::string& text, bool is_error);

    // Snapshot the current feed, oldest-first, for rendering. Returns a copy so
    // the UI never holds the lock while drawing.
    std::vector<DiagnosticsLine> snapshot() const;

    std::size_t size() const;
    void        clear();

    static constexpr std::size_t kDefaultCapacity = 2048;

private:
    mutable std::mutex          mutex_;
    std::deque<DiagnosticsLine> lines_;
    std::size_t                 capacity_;
};

// One step in the reasoning journey: which agent, what it was given, what it
// reasoned (chain-of-thought), what it produced. The full quartet crosses here
// for the diagnostics view — this is the frontend's own audit record and is
// distinct from anything that rides the inter-agent boundary.
struct JourneyEntry {
    std::string agent;
    std::string input;
    std::string cot;
    std::string output;
};

// Append-only journey of agent steps for the session. Not bounded — it is the
// reasoning audit trail, cleared explicitly (e.g. on a new session or on user
// request), not aged out.
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

} // namespace prime::frontend
