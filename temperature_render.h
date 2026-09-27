// temperature_render.h — Temperature panel, render layer
//
// THE PANEL IS A USER OF THE DATA, NOT ITS OWNER
//   LiveRegistry holds the temperatures. This panel reaches into it directly —
//   no store object, no callbacks handed in, no second durable copy anywhere.
//
// THE WORKING COPY LIVES ONLY WHILE THE PANEL IS OPEN
//   open() takes a copy of what the registry currently holds. Every edit
//   changes that copy and nothing else — what generation is running on is
//   untouched. close() discards it: nothing changes, no prompt, no
//   "unsaved changes" nag. Save is the ONE moment the copy goes back to the
//   registry and to disk, together, as one action.
//
// OPENING IS THE ACKNOWLEDGEMENT
//   open() clears the default flag on every held value, whether or not
//   anything is then edited. An operator happy with 0.7 clears the warning by
//   opening this panel and closing it again. That is deliberate: it stops
//   "yes, I know it is default, I just need to get on" quietly becoming
//   "when did THAT happen?" three weeks later.
//
// THE SHAPE ON SCREEN
//   Grouped by team. Every team with at least one controllable member gets a
//   group listing ALL its controllable members, each with its own editable
//   value. An agent on two teams appears twice, once under each — two rows,
//   two values, because the same specialist may want a different spread in
//   each fusion it contributes to.
//
//   Controllable agents belonging to no team get their own section at the end.
//
// SPLIT RUNNERS ARE A LABEL, NOT A ROW
//   A split-generated team NEVER appears as its own group — it would only
//   show a number already shown against its parent. It appears INSIDE its
//   parent's group as a label beside the member whose value it borrows, named
//   exactly as the system names it. The first label is always the parent team
//   itself: it runs flat at its first member's value when split is active.
//
//   The labels come from the registry's own ordered runner list, read
//   positionally against the same team's own member list. Nothing here
//   computes an index, parses a name, or infers an order.
//
// ABSENT IS NOT ZERO
//   An agent with no control has no entry and renders nothing — not a zero,
//   not a dash. A zero would read as a deliberate setting rather than an
//   absence.
//
// OUT OF RANGE IS REFUSED, NEVER CLAMPED
//   Save refuses the WHOLE commit if any value is out of bounds, names the
//   row, and applies none of it. A silently corrected temperature is a
//   silently different agent.

#pragma once

#include "live_registry.h"

#include <string>
#include <vector>

namespace prime {

class TemperatureRender {
public:
    explicit TemperatureRender(LiveRegistry& registry) : registry_(registry) {}

    // Takes the working copy and acknowledges defaults. Call when the panel
    // is opened, not every frame.
    void open();

    // Discards the working copy. Nothing is written; nothing changes.
    void close();

    bool is_open() const { return open_; }

    void draw();

private:
    void draw_team_group(const std::string& team,
                         const std::vector<std::string>& members,
                         const std::vector<std::string>& runner_labels);
    void draw_member_row(const std::string& team,
                         const std::string& agent,
                         const std::string& runner_label);
    void draw_solo_section();
    void do_save();

    LiveRegistry& registry_;

    bool open_ = false;

    // Lives only between open() and close(). The registry's own values are
    // untouched until save.
    std::vector<LiveTemperature> working_;

    // Transient view state, owned here.
    std::string rejected_team_;
    std::string rejected_agent_;
    std::string save_error_;
    bool        saved_notice_ = false;
};

} // namespace prime
