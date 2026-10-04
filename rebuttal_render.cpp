// rebuttal_render.cpp — The rebuttal window's draw half, implementation

#include "rebuttal_render.h"

#include "imgui.h"

#include <algorithm>

namespace prime::frontend {

bool RebuttalRender::expanded(const TurnId& turn) const {
    return std::find(expanded_.begin(), expanded_.end(), turn) != expanded_.end();
}

void RebuttalRender::toggle_expanded(const TurnId& turn) {
    auto it = std::find(expanded_.begin(), expanded_.end(), turn);
    if (it != expanded_.end()) expanded_.erase(it);
    else                       expanded_.push_back(turn);
}

void RebuttalRender::draw() {

    if (!host_.any_open()) {
        expanded_.clear();
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x - 24.0f, vp->WorkPos.y + 48.0f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("Rebuttals", nullptr, flags)) {
        ImGui::End();
        return;
    }

    std::vector<TurnId> order;
    for (const auto& l : host_.lines()) order.push_back(l.turn);

    for (const auto& id : order) {
        const RebuttalLine* line = nullptr;
        for (const auto& l : host_.lines())
            if (l.turn == id) { line = &l; break; }
        if (line == nullptr) continue;

        ImGui::PushID(id.value.c_str());

        if (line->bold)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        const bool open = expanded(id);
        if (ImGui::SmallButton(open ? "v" : ">")) toggle_expanded(id);
        ImGui::SameLine();

        ImGui::TextUnformatted(line->subject.c_str());

        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            if (line->bold) ImGui::PopStyleColor();
            ImGui::PopID();
            host_.dismiss(id);
            continue;
        }

        if (line->bold) ImGui::PopStyleColor();

        if (open) {
            const auto* inputs = host_.inputs_for(id);
            if (inputs != nullptr) {
                ImGui::Indent();
                for (const auto& text : *inputs)
                    ImGui::TextWrapped("%s", text.c_str());
                ImGui::Unindent();
            }
        }

        ImGui::PopID();
    }

    ImGui::End();
}

}
