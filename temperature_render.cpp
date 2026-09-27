// temperature_render.cpp — Temperature panel render implementation

#include "temperature_render.h"

#include "imgui.h"

#include <algorithm>

namespace prime {

void TemperatureRender::open() {
    working_ = registry_.temperatures();

    // Opening IS the acknowledgement. Cleared whether or not anything is then
    // edited, so an operator content with the default clears the warning by
    // looking at it.
    registry_.acknowledge_defaults();
    for (auto& e : working_) e.is_default = false;

    rejected_team_.clear();
    rejected_agent_.clear();
    save_error_.clear();
    saved_notice_ = false;
    open_         = true;
}

void TemperatureRender::close() {
    // Discarded outright. Nothing written, nothing changed, nothing asked.
    working_.clear();
    rejected_team_.clear();
    rejected_agent_.clear();
    save_error_.clear();
    saved_notice_ = false;
    open_         = false;
}

void TemperatureRender::draw() {
    if (!open_) return;

    const std::string pipeline = registry_.pipeline_name();
    if (pipeline.empty()) {
        ImGui::TextDisabled("No pipeline loaded.");
        return;
    }

    ImGui::Text("Pipeline: %s", pipeline.c_str());
    ImGui::Separator();

    if (working_.empty()) {
        ImGui::TextDisabled("No controllable agents in this pipeline.");
        return;
    }

    // Teams, in the registry's own order. A split-generated team is never a
    // group of its own — it only ever appears as a label inside its parent.
    for (const auto& t : registry_.teams()) {
        if (!t.parent.empty()) continue;

        const std::vector<std::string> members = registry_.generating_members(t.name);

        std::vector<std::string> controllable;
        for (const auto& m : members) {
            const bool held =
                std::any_of(working_.begin(), working_.end(),
                            [&](const LiveTemperature& e) {
                                return e.team == t.name && e.agent == m;
                            });
            if (held) controllable.push_back(m);
        }
        if (controllable.empty()) continue;

        // Runner labels, positional against this same member list. Empty when
        // the team is not split — no labels, no column.
        std::vector<std::string> labels(members.size());
        if (auto sp = registry_.split_parent(t.name)) {
            for (size_t i = 0; i < sp->runners.size() && i < labels.size(); ++i)
                labels[i] = sp->runners[i];
        }

        // Re-index the labels onto the controllable subset, keeping position.
        std::vector<std::string> shown_labels;
        shown_labels.reserve(controllable.size());
        for (const auto& m : controllable) {
            const auto it = std::find(members.begin(), members.end(), m);
            const size_t pos = static_cast<size_t>(std::distance(members.begin(), it));
            shown_labels.push_back(pos < labels.size() ? labels[pos] : std::string{});
        }

        draw_team_group(t.name, controllable, shown_labels);
        ImGui::Spacing();
    }

    draw_solo_section();

    ImGui::Separator();

    if (ImGui::Button("Save")) do_save();

    ImGui::SameLine();
    if (ImGui::Button("Close without saving")) {
        close();
        return;
    }

    if (!save_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", save_error_.c_str());
    } else if (saved_notice_) {
        ImGui::TextDisabled("Saved.");
    }
}

void TemperatureRender::draw_team_group(const std::string& team,
                                        const std::vector<std::string>& members,
                                        const std::vector<std::string>& runner_labels) {
    ImGui::PushID(team.c_str());

    const bool split = std::any_of(runner_labels.begin(), runner_labels.end(),
                                   [](const std::string& s) { return !s.empty(); });

    if (split) ImGui::Text("%s   [Split active]", team.c_str());
    else       ImGui::TextUnformatted(team.c_str());

    ImGui::Indent();
    for (size_t i = 0; i < members.size(); ++i)
        draw_member_row(team, members[i],
                        i < runner_labels.size() ? runner_labels[i] : std::string{});
    ImGui::Unindent();

    ImGui::PopID();
}

void TemperatureRender::draw_member_row(const std::string& team,
                                        const std::string& agent,
                                        const std::string& runner_label) {
    auto it = std::find_if(working_.begin(), working_.end(),
                           [&](const LiveTemperature& e) {
                               return e.team == team && e.agent == agent;
                           });
    // No entry means no control. Nothing drawn — deliberately not a zero.
    if (it == working_.end()) return;

    ImGui::PushID(agent.c_str());

    ImGui::SetNextItemWidth(90.0f);
    float value = static_cast<float>(it->value);
    if (ImGui::InputFloat("##temp", &value, 0.1f, 0.1f, "%.2f")) {
        // Edits the WORKING COPY only. The registry is untouched until save.
        it->value = static_cast<double>(value);
        if (rejected_team_ == team && rejected_agent_ == agent) {
            rejected_team_.clear();
            rejected_agent_.clear();
            save_error_.clear();
        }
        saved_notice_ = false;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(agent.c_str());

    // Which runner borrows this member's value in split mode. Traceability
    // only — never a second editable field.
    if (!runner_label.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("   %s", runner_label.c_str());
    }

    if (rejected_team_ == team && rejected_agent_ == agent) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "  out of range - nothing saved");
    }

    ImGui::PopID();
}

void TemperatureRender::draw_solo_section() {
    std::vector<const LiveTemperature*> solo;
    for (const auto& e : working_)
        if (e.team.empty()) solo.push_back(&e);
    if (solo.empty()) return;

    ImGui::TextDisabled("Not in a team");
    ImGui::Indent();
    for (const auto* e : solo) draw_member_row(std::string{}, e->agent, std::string{});
    ImGui::Unindent();
}

void TemperatureRender::do_save() {
    std::string bad_team;
    std::string bad_agent;

    if (registry_.commit_temperatures(working_, bad_team, bad_agent)) {
        rejected_team_.clear();
        rejected_agent_.clear();
        save_error_.clear();
        saved_notice_ = true;
        return;
    }

    saved_notice_ = false;

    if (!bad_agent.empty()) {
        rejected_team_  = bad_team;
        rejected_agent_ = bad_agent;
        save_error_     = "Refused: a value is out of range (" +
                          std::to_string(temperature_policy::kMin) + " to " +
                          std::to_string(temperature_policy::kMax) +
                          "). Nothing was saved.";
    } else {
        save_error_ = "Save failed: the temperature file could not be written.";
    }
}

} // namespace prime
