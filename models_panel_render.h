// models_panel_render.h — Prime Architect Models panel, render layer
//
// SINGLE RESPONSIBILITY
//   Draw the three panes and translate user actions into calls on the authoring
//   layers. It owns only transient view state. Agents live in AgentConfig, teams
//   in TeamConfig, models in ModelDiscovery, temperatures in the UI's
//   TemperatureStore. It never writes config to disk itself — it calls the
//   host's save hook.
//
// THREE PANES (immediate-mode: no DOM, no widget retention between frames)
//   LEFT  — department list, folder-derived, never hardcoded. Arbiter appears
//           here as an ordinary entry.
//   MIDDLE— the selected department's agents: model assignment, compute target.
//   RIGHT — the selected department's teams: roster, Split toggle where
//           eligible, and each member's temperature.
//
// TEMPERATURE IS SHOWN IN THE TEAM PANE, NOT THE AGENT PANE
//   It used to sit in the middle pane, one value beside each agent's name. That
//   is not meaningful: a value belongs to a (team, agent) PAIR, and an agent on
//   two teams carries two different ones. A single number beside a bare agent
//   name would be showing one of several without saying which.
//
//   The right pane walks each team's roster with the team in hand, so the pair
//   is unambiguous there. Read-only either way — temperature is authored in the
//   Temperature panel.
//
// THE SPLIT TOGGLE
//   Shown on a team where its department allows it. Turning it on creates the
//   duplicate fusions; turning it off removes them. NO AGENT IS CREATED EITHER
//   WAY — a duplicate holds the same agents its parent holds.
//
//   Duplicates are drawn nested inside their parent, read-only, with the flat
//   temperature each runs at. They never appear as top-level teams and have no
//   controls: a duplicate mirrors its parent exactly and there is nothing about
//   it to edit.
//
// COMPUTE TARGET
//   Explicit selection; never inferred from the model file.

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "agent_config.h"
#include "team_config.h"
#include "model_discovery.h"
#include "temperature_store.h"

namespace prime {

using PairTempReadFn = std::function<std::optional<double>(const std::string& team,
                                                           const std::string& agent)>;

using SplitFlatTempFn = std::function<std::optional<double>(const std::string& parent_team,
                                                            int duplicate_index)>;

using NameToDepartmentFn = std::function<std::string(const std::string& name)>;

using DirtyFn = std::function<void()>;

struct ComputeTargets {
    static constexpr const char* kOptions[] = {"rdna", "xdna", "hybrid"};
    static constexpr int kCount = 3;
};

class ModelsPanelRender {
public:
    ModelsPanelRender(AgentConfig&       agents,
                      TeamConfig&        teams,
                      ModelDiscovery&    discovery,
                      PairTempReadFn     pair_temp,
                      SplitFlatTempFn    split_flat_temp,
                      NameToDepartmentFn name_to_dept,
                      DirtyFn            mark_dirty);

    void draw();
    void reset_view();

private:
    void draw_department_pane();
    void draw_agent_pane();
    void draw_team_pane();

    void draw_team_row(const TeamEntry& team);
    void draw_split_children(const TeamEntry& parent);

    void draw_add_agent_dialog();
    void draw_add_team_dialog();

    AgentConfig&       agents_;
    TeamConfig&        teams_;
    ModelDiscovery&    discovery_;
    PairTempReadFn     pair_temp_;
    SplitFlatTempFn    split_flat_temp_;
    NameToDepartmentFn name_to_dept_;
    DirtyFn            mark_dirty_;

    std::string selected_department_;

    bool  add_agent_open_ = false;
    char  add_agent_name_[128] = {0};
    int   add_agent_model_idx_ = -1;
    int   add_agent_target_idx_ = 0;

    bool  add_team_open_ = false;
    char  add_team_name_[128] = {0};
};

}
