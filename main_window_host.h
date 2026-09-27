// main_window_host.h — The shell: menu bar, tab bar, windows, layout.
//
// Draws only. Menu bar, the layout tab bar, the conversation window (indicator,
// activity bar, chat, input), the settings windows, the diagnostics window — all
// as drawing functions, the same way the models panel keeps its panes together
// in one file.
//
// It does NOT decide what Commit or Abort do — that is TurnActions. The shell
// calls into it when a button is pressed.
//
// THE ROSTER IS NOT HELD HERE
//   A previous version kept a copied vector of the pipeline's agent names and
//   asked "is any of mine busy" against it. That copy is gone: it was a stored
//   mirror of something that already lives in the pipeline's own declaration.
//
//   Instead the active tab names a pipeline, and that name is the lookup key.
//   The roster is read live, every frame, through RosterLookupFn. Nothing is
//   cached between frames and nothing is stored on this object.
//
//   Both the activity bar and Abort ask the same question, once per frame,
//   scoped to the ACTIVE tab's pipeline only — which is what stops a second
//   loaded pipeline, or a background task, from lighting up a bar that has
//   nothing to do with it.
//
// CHAT AND INPUT ARE SHARED ACROSS TABS
//   They live here, on the shell, not on any tab. Switching tabs does not clear
//   them, duplicate them, or touch them at all.
//
// THE REBUTTAL WINDOW IS HELD THE SAME WAY
//   Directly on the shell, next to chat and input, for the same reason: it is
//   not a tab concern. The render reads the window's own line list live and
//   draws nothing when nothing is open — the shell just gives it its place in
//   the frame.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "agent_activity.h"
#include "chat_render.h"
#include "diagnostics_panel_render.h"
#include "input_render.h"
#include "layout_tabs.h"
#include "rebuttal_render.h"
#include "rules_render.h"
#include "temperature_render.h"
#include "turn_actions.h"

namespace prime {

// Resolves a pipeline name to the agents belonging to it, read live from that
// pipeline's own declaration. Called every frame; nothing about the result is
// kept.
using RosterLookupFn = std::function<std::vector<std::string>(const std::string& pipeline)>;

class MainWindowHost {
public:
    MainWindowHost(prime::frontend::IngestionState& ingestion,
                   prime::frontend::AgentActivity&  activity,
                   prime::frontend::DiagnosticsLog& diagnostics,
                   prime::frontend::JourneyLog&     journey,
                   RosterLookupFn                   roster_lookup,
                   std::function<void()>            on_commit,
                   std::function<void()>            on_abort,
                   AnswerQuestionFn                 answer_question,
                   RulesRender&                     rules,
                   TemperatureRender&               temperature,
                   prime::frontend::RebuttalRender& rebuttal);

    void draw();

    ChatRender& chat() { return chat_; }
    LayoutTabs& tabs() { return tabs_; }

private:
    void draw_menu_bar();
    void draw_tab_bar();
    void draw_conversation();
    void draw_rules_window();
    void draw_temperature_window();
    void draw_diagnostics_window();

    prime::frontend::AgentActivity& activity_;
    RosterLookupFn                  roster_lookup_;

    LayoutTabs             tabs_;
    ChatRender             chat_;    // shared across tabs, never per-tab
    InputRender            input_;   // shared across tabs, never per-tab
    DiagnosticsPanelRender diagnostics_;
    TurnActions            actions_;

    RulesRender&           rules_;
    TemperatureRender&     temperature_;

    // Constructed where RebuttalHost lives (it outlives the shell, same as
    // rules and temperature); the shell only draws it.
    prime::frontend::RebuttalRender& rebuttal_;

    bool diagnostics_open_ = false;
    bool rules_open_       = false;
    bool temperature_open_ = false;
};

} // namespace prime
