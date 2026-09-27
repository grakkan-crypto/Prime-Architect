// models_panel_render.cpp — Prime Architect Models panel render implementation

#include "models_panel_render.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace prime {

ModelsPanelRender::ModelsPanelRender(AgentConfig&       agents,
                                     TeamConfig&        teams,
                                     ModelDiscovery&    discovery,
                                     PairTempReadFn     pair_temp,
                                     SplitFlatTempFn    split_flat_temp,
                                     NameToDepartmentFn name_to_dept,
                                     DirtyFn            mark_dirty)
    : agents_(agents),
      teams_(teams),
      discovery_(discovery),
      pair_temp_(std::move(pair_temp)),
      split_flat_temp_(std::move(split_flat_temp)),
      name_to_dept_(std::move(name_to_dept)),
      mark_dirty_(std::move(mark_dirty)) {}

void ModelsPanelRender::reset_view() {
    selected_department_.clear();
    add_agent_open_ = false;
    add_team_open_  = false;
    add_agent_name_[0] = '\0';
    add_team_name_[0]  = '\0';
    add_agent_model_idx_  = -1;
    add_agent_target_idx_ = 0;
}

void ModelsPanelRender::draw() {
    const float total = ImGui::GetContentRegionAvail().x;

    if (ImGui::BeginChild("##departments", ImVec2(total * 0.20f, 0), true))
        draw_department_pane();
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("##agents", ImVec2(total * 0.38f, 0), true))
        draw_agent_pane();
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("##teams", ImVec2(0, 0), true))
        draw_team_pane();
    ImGui::EndChild();

    draw_add_agent_dialog();
    draw_add_team_dialog();
}

void ModelsPanelRender::draw_department_pane() {
    ImGui::TextUnformatted("DEPARTMENTS");
    ImGui::Separator();

    for (const auto& department : discovery_.departments()) {
        const bool selected = (department == selected_department_);
        if (ImGui::Selectable(department.c_str(), selected))
            selected_department_ = department;
    }
}

// ---------------------------------------------------------------------------
// MIDDLE — agents
//
// No temperature here: this pane has no team in hand, so any single number
// beside a bare agent name would be one of several without saying which.
// ---------------------------------------------------------------------------
void ModelsPanelRender::draw_agent_pane() {
    if (selected_department_.empty()) {
        ImGui::TextDisabled("Select a department.");
        return;
    }

    ImGui::TextUnformatted("AGENTS");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add Agent")) add_agent_open_ = true;
    ImGui::Separator();

    const auto dept_agents = agents_.in_department(selected_department_);
    if (dept_agents.empty()) {
        ImGui::TextDisabled("No agents in this department.");
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

    for (const auto& a : dept_agents) {
        ImGui::PushID(a.id.c_str());

        ImGui::TextUnformatted(a.name.c_str());
        ImGui::SameLine();

        int target_idx = 0;
        for (int i = 0; i < ComputeTargets::kCount; ++i)
            if (a.compute_target == ComputeTargets::kOptions[i]) target_idx = i;

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("##target", &target_idx,
                         ComputeTargets::kOptions, ComputeTargets::kCount)) {
            agents_.edit_agent(a.id, a.mapped_path,
                               ComputeTargets::kOptions[target_idx]);
            if (mark_dirty_) mark_dirty_();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            agents_.delete_agent(a.id);
            if (mark_dirty_) mark_dirty_();
        }

        ImGui::PopID();
    }

    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// RIGHT — teams
// ---------------------------------------------------------------------------
void ModelsPanelRender::draw_team_pane() {
    if (selected_department_.empty()) {
        ImGui::TextDisabled("Select a department.");
        return;
    }

    ImGui::TextUnformatted("TEAMS");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add Team")) add_team_open_ = true;
    ImGui::Separator();

    // Parent teams only at the top level. Duplicates are nested inside the team
    // they mirror.
    const auto dept_teams = teams_.authorable_in_department(selected_department_);
    if (dept_teams.empty()) {
        ImGui::TextDisabled("No teams in this department.");
        return;
    }

    for (const auto& team : dept_teams)
        draw_team_row(team);
}

void ModelsPanelRender::draw_team_row(const TeamEntry& team) {
    auto resolve = [this](const std::string& name) -> std::string {
        return name_to_dept_ ? name_to_dept_(name) : std::string();
    };

    ImGui::PushID(team.id.c_str());

    if (ImGui::CollapsingHeader(team.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

        if (split_policy::department_allows_split(team.department)) {
            bool enabled = team.split_enabled;
            if (ImGui::Checkbox("Split", &enabled)) {
                teams_.set_split_enabled(team.id, enabled, resolve);
                if (mark_dirty_) mark_dirty_();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(adds one fusion per generating member - same agents)");
        }

        for (const auto& member : team.roster) {
            ImGui::PushID(member.c_str());

            ImGui::BulletText("%s", member.c_str());

            // Temperature for THIS PAIR. Read-only here. Nothing is drawn when
            // the pair has no control: an absent control must not read as a
            // deliberate zero.
            if (pair_temp_) {
                if (auto t = pair_temp_(team.name, member)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("  temp %.2f", *t);
                }
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                teams_.remove_member(team.id, member, resolve);
                if (mark_dirty_) mark_dirty_();
            }

            ImGui::PopID();
        }

        // Eligible pool: this department's agents, plus Arbiter. That is the
        // whole rule, and it is what makes member lookup two bucket reads
        // everywhere else.
        std::vector<MemberCandidate> eligible;
        for (const auto& a : agents_.in_department(team.department))
            eligible.push_back({a.name, a.department});
        if (team.department != "Arbiter")
            for (const auto& a : agents_.in_department("Arbiter"))
                eligible.push_back({a.name, a.department});

        if (!eligible.empty() && ImGui::BeginCombo("##add_member", "+ member")) {
            for (const auto& candidate : eligible) {
                // Ineligible options are not listed rather than listed and
                // refused on click.
                if (teams_.check_eligibility(team, candidate, resolve)
                    != AddMemberResult::Added)
                    continue;

                if (ImGui::Selectable(candidate.name.c_str())) {
                    teams_.add_member(team.id, candidate, resolve);
                    if (mark_dirty_) mark_dirty_();
                }
            }
            ImGui::EndCombo();
        }

        draw_split_children(team);

        ImGui::Separator();
        if (ImGui::SmallButton("Delete team")) {
            teams_.delete_team(team.id);
            if (mark_dirty_) mark_dirty_();
        }
    }

    ImGui::PopID();
}

void ModelsPanelRender::draw_split_children(const TeamEntry& parent) {
    const auto duplicates = teams_.splits_of(parent.name);
    if (duplicates.empty()) return;

    ImGui::Indent();
    ImGui::TextDisabled("Split fusions (generated, not editable)");

    for (size_t k = 0; k < duplicates.size(); ++k) {
        const auto& dup = duplicates[k];
        ImGui::PushID(dup.id.c_str());

        // The flat value this fusion runs at, drawn from one of the parent's
        // members. Derived rather than stored — it is fully determined by the
        // parent's own authored values.
        std::string label = dup.name;
        if (split_flat_temp_) {
            if (auto flat = split_flat_temp_(parent.name, static_cast<int>(k) + 1)) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "   flat %.2f", *flat);
                label += buf;
            }
        }

        ImGui::BulletText("%s", label.c_str());

        // The same agents as the parent — not copies. Shown so the duplicate is
        // visibly a full fusion rather than something reduced, with no controls:
        // there is nothing about it to edit.
        ImGui::Indent();
        for (const auto& member : dup.roster)
            ImGui::TextDisabled("%s", member.c_str());
        ImGui::Unindent();

        ImGui::PopID();
    }

    ImGui::Unindent();
}

void ModelsPanelRender::draw_add_agent_dialog() {
    if (!add_agent_open_) return;

    ImGui::OpenPopup("Add Agent");
    if (!ImGui::BeginPopupModal("Add Agent", &add_agent_open_,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("Name", add_agent_name_, sizeof(add_agent_name_));

    const auto choices =
        agents_.assignable_models(selected_department_, discovery_.models());

    std::vector<const char*> labels;
    labels.reserve(choices.size());
    for (const auto& c : choices) labels.push_back(c.name.c_str());

    if (!labels.empty())
        ImGui::Combo("Model", &add_agent_model_idx_, labels.data(),
                     static_cast<int>(labels.size()));
    else
        ImGui::TextDisabled("No assignable models for this department.");

    ImGui::Combo("Target", &add_agent_target_idx_,
                 ComputeTargets::kOptions, ComputeTargets::kCount);

    const bool valid = add_agent_name_[0] != '\0' &&
                       add_agent_model_idx_ >= 0 &&
                       add_agent_model_idx_ < static_cast<int>(choices.size());

    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Create")) {
        agents_.create_agent(add_agent_name_,
                             selected_department_,
                             choices[add_agent_model_idx_].load_path,
                             ComputeTargets::kOptions[add_agent_target_idx_]);
        if (mark_dirty_) mark_dirty_();
        add_agent_name_[0] = '\0';
        add_agent_model_idx_ = -1;
        add_agent_open_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        add_agent_open_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ModelsPanelRender::draw_add_team_dialog() {
    if (!add_team_open_) return;

    ImGui::OpenPopup("Add Team");
    if (!ImGui::BeginPopupModal("Add Team", &add_team_open_,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("Name", add_team_name_, sizeof(add_team_name_));
    ImGui::TextDisabled("Department: %s", selected_department_.c_str());

    ImGui::BeginDisabled(add_team_name_[0] == '\0');
    if (ImGui::Button("Create")) {
        teams_.create_team(add_team_name_, selected_department_);
        if (mark_dirty_) mark_dirty_();
        add_team_name_[0] = '\0';
        add_team_open_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        add_team_open_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace prime
