// main_window_host.cpp

#include "main_window_host.h"

#include "cylon_bar.h"
#include "imgui.h"

namespace prime {

MainWindowHost::MainWindowHost(prime::frontend::IngestionState& ingestion,
                               prime::frontend::AgentActivity&  activity,
                               prime::frontend::DiagnosticsLog& diagnostics,
                               prime::frontend::JourneyLog&     journey,
                               RosterLookupFn                   roster_lookup,
                               std::function<void()>            on_commit,
                               std::function<void()>            on_abort,
                               AnswerQuestionFn                 answer_question,
                               RulesRender&                     rules,
                               TemperatureRender&               temperature,
                               prime::frontend::RebuttalRender& rebuttal)
    : activity_(activity),
      roster_lookup_(std::move(roster_lookup)),

      input_(ingestion,
             [this]() { actions_.commit(); },
             [this]() { actions_.abort(); },
             std::move(answer_question)),
      diagnostics_(diagnostics, journey),
      actions_(ingestion, activity, chat_,
               std::move(on_commit), std::move(on_abort),

               [this]() -> std::vector<std::string> {
                   const std::string pipeline = tabs_.active_pipeline();
                   if (pipeline.empty() || !roster_lookup_) return {};
                   return roster_lookup_(pipeline);
               }),
      rules_(rules),
      temperature_(temperature),
      rebuttal_(rebuttal) {}

void MainWindowHost::draw() {
    draw_menu_bar();
    draw_conversation();

    rebuttal_.draw();
    draw_rules_window();
    draw_temperature_window();
    draw_diagnostics_window();
}

void MainWindowHost::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Diagnostics", nullptr, &diagnostics_open_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Rules & Directives", nullptr, &rules_open_))
            if (rules_open_) rules_.reset_view();
        ImGui::MenuItem("Temperature", nullptr, &temperature_open_);

        ImGui::MenuItem("Models", nullptr, false, false);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void MainWindowHost::draw_tab_bar() {
    if (!ImGui::BeginTabBar("##layout", ImGuiTabBarFlags_AutoSelectNewTabs |
                                        ImGuiTabBarFlags_Reorderable))
        return;

    const auto& tabs = tabs_.tabs();
    for (size_t i = 0; i < tabs.size(); ++i) {
        bool open = true;
        const std::string label =
            tabs[i].pipeline + "###tab" + std::to_string(i);

        if (ImGui::BeginTabItem(label.c_str(), &open)) {

            tabs_.select(i);
            ImGui::EndTabItem();
        }

        if (!open) {
            tabs_.close(i);
            break;
        }
    }

    ImGui::EndTabBar();
}

void MainWindowHost::draw_conversation() {
    if (!ImGui::Begin("Conversation")) {
        ImGui::End();
        return;
    }

    draw_tab_bar();

    const std::string pipeline = tabs_.active_pipeline();
    std::vector<std::string> roster;
    if (!pipeline.empty() && roster_lookup_) roster = roster_lookup_(pipeline);

    const bool busy = activity_.any_busy(roster);

    const std::string indicator = tabs_.active_indicator();
    if (!indicator.empty()) {
        ImGui::TextUnformatted(indicator.c_str());
        draw_cylon_bar(busy);
        ImGui::Spacing();
    }

    const float available = ImGui::GetContentRegionAvail().y;
    const float reserved  = 200.0f;
    const float height    = (available > reserved) ? (available - reserved) : 0.0f;

    if (ImGui::BeginChild("##conversation", ImVec2(0.0f, height))) {
        chat_.draw();
    }
    ImGui::EndChild();

    ImGui::Separator();
    input_.draw(busy);

    ImGui::End();
}

void MainWindowHost::draw_rules_window() {
    if (!rules_open_) return;
    if (ImGui::Begin("Rules & Directives", &rules_open_)) rules_.draw();
    ImGui::End();
}

void MainWindowHost::draw_temperature_window() {
    if (!temperature_open_) return;
    if (ImGui::Begin("Temperature", &temperature_open_)) temperature_.draw();
    ImGui::End();
}

void MainWindowHost::draw_diagnostics_window() {
    if (!diagnostics_open_) return;
    if (ImGui::Begin("Diagnostics", &diagnostics_open_)) diagnostics_.draw();
    ImGui::End();
}

}
