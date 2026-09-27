// rebuttal_render.h — The rebuttal window's draw half (Prime_Architect/ui/render)
//
// Draws, and holds only its own small view state (which lines are expanded) —
// every real action goes through RebuttalHost, and every fact drawn is read
// off the host's lines live, each frame. Nothing durable lives here.
//
// SMALL, HOVERING, TOP-RIGHT
//   A small segment of the screen hovering in the top-right of the chat window
//   (not the exact corner). It expands DOWN one line per open rebuttal and
//   stretches HORIZONTALLY to fit longer summaries — it never takes a fixed
//   large region. It does not need to move for now.
//
// PRESENCE IS VISIBILITY
//   No open rebuttals, no window. Same pattern as ingestion_state's question
//   list.
//
// PER LINE
//   The 2-3 word subject, an expand control revealing that rebuttal's recorded
//   inputs (the in-window surface for them until the output mirror lands), and
//   the dismiss cross. There is NO confirm control anywhere in this window —
//   not an omission; confirm comes through the conversation itself. A line
//   whose bold attribute is set renders emphasised as one visual unit,
//   controls included — the attribute was painted by the host at playback
//   start; nothing here tracks or compares anything.
//
//   (Emphasis is colour until a bold font variant is loaded — the UI style is
//   temporary until the custom UI build removes the dependency entirely.)

#pragma once

#include "rebuttal_host.h"

#include <vector>

namespace prime::frontend {

class RebuttalRender {
public:
    explicit RebuttalRender(RebuttalHost& host) : host_(host) {}

    RebuttalRender(const RebuttalRender&)            = delete;
    RebuttalRender& operator=(const RebuttalRender&) = delete;

    void draw();

private:
    bool expanded(const TurnId& turn) const;
    void toggle_expanded(const TurnId& turn);

    RebuttalHost& host_;

    // View state only — which lines are expanded.
    std::vector<TurnId> expanded_;
};

} // namespace prime::frontend
