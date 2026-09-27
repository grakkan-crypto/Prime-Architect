// diagnostics_panel_render.cpp

#include "diagnostics_panel_render.h"

#include "imgui.h"

#include <string>

namespace prime {

DiagnosticsPanelRender::DiagnosticsPanelRender(
    prime::frontend::DiagnosticsLog& diagnostics,
    prime::frontend::JourneyLog&     journey)
    : diagnostics_(diagnostics), journey_(journey) {}

void DiagnosticsPanelRender::draw() {
    if (ImGui::BeginTabBar("##diagnostics")) {
        if (ImGui::BeginTabItem("Feed")) {
            draw_feed();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Journey")) {
            draw_journey();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void DiagnosticsPanelRender::draw_feed() {
    ImGui::Checkbox("Errors only", &errors_only_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) diagnostics_.clear();
    ImGui::Separator();

    if (ImGui::BeginChild("##feed")) {
        for (const auto& line : diagnostics_.snapshot()) {
            if (errors_only_ && !line.is_error) continue;

            if (line.is_error) {
                ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, 1.0f), "%s", line.text.c_str());
            } else {
                ImGui::TextUnformatted(line.text.c_str());
            }
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

void DiagnosticsPanelRender::draw_journey() {
    if (ImGui::SmallButton("Clear")) journey_.clear();
    ImGui::Separator();

    if (ImGui::BeginChild("##journey")) {
        const auto entries = journey_.snapshot();

        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            const std::string header = std::to_string(i + 1) + "  " + entry.agent;

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::CollapsingHeader(header.c_str())) {
                ImGui::Indent();
                if (!entry.input.empty()) {
                    ImGui::TextDisabled("INPUT");
                    ImGui::TextWrapped("%s", entry.input.c_str());
                }
                if (!entry.cot.empty()) {
                    ImGui::TextDisabled("THINKING");
                    ImGui::TextWrapped("%s", entry.cot.c_str());
                }
                if (!entry.output.empty()) {
                    ImGui::TextDisabled("OUTPUT");
                    ImGui::TextWrapped("%s", entry.output.c_str());
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

} // namespace prime
