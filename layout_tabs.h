// layout_tabs.h — The layout tab bar
//
// A tab bar, not a mode. Endlessly addable: a new pipeline is a new tab. The old
// two-mode toggle it replaces hid one fixed button group and showed another,
// which only ever worked because there were exactly two.
//
// THE TAB SPECIFIES THE PIPELINE
//   That is the whole point of it. The active tab names which pipeline
//   everything else is talking about — the activity bar, Abort, rules,
//   temperature. It is the lookup key, and it is the only thing that answers
//   "which pipeline".
//
// SWITCHING A TAB HAS NO SIDE EFFECTS
//   No pipeline load, no forced agent selection, no clearing. Switching
//   switches. For the coding pipeline, selecting a PROJECT is what loads that
//   pipeline against that project's data — the tab change does nothing more.
//
// CHAT AND INPUT ARE SHARED, NOT PER-TAB
//   One system, one user, one conversation. A discussion started while one tab
//   was open must be just as reachable from another, so the chat surface and the
//   input box live on the shell. They are not duplicated per tab and not cleared
//   on switch.
//
// WHAT IS GENUINELY PER-TAB
//   Two things: which pipeline the tab points at, and the indicator text shown
//   above the activity bar (for the coding pipeline, the selected project). That
//   is the whole record.
//
// NO REGISTRY OF ANYTHING, ANYWHERE
//   Which pipelines are OPEN is the tab bar's own state — free, and needed
//   anyway to draw the bar. Which pipelines EXIST is read live from disk when
//   something asks. Neither is a maintained list, because a maintained list is a
//   second record that can disagree with the first.
//
// THE ROSTER IS NOT STORED HERE EITHER
//   The activity bar and Abort both need "is any of THIS pipeline's agents
//   busy". The answer comes from reading the pipeline's own declaration live,
//   keyed by the tab's pipeline name, every time. The shell used to hold a
//   copied roster vector; that copy is gone.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace prime {

struct LayoutTab {

    std::string pipeline;

    std::string indicator;
};

class LayoutTabs {
public:

    size_t open(const std::string& pipeline, const std::string& indicator = "");

    bool close(size_t index);

    void select(size_t index);

    bool   empty() const { return tabs_.empty(); }
    size_t count() const { return tabs_.size(); }
    size_t active_index() const { return active_; }

    const std::vector<LayoutTab>& tabs() const { return tabs_; }

    const LayoutTab* active() const;
    LayoutTab*       active();

    std::string active_pipeline() const;

    std::string active_indicator() const;
    void set_active_indicator(const std::string& text);

private:
    std::vector<LayoutTab> tabs_;
    size_t                 active_ = 0;
};

}
