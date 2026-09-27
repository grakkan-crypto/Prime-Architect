// chat_render.cpp

#include "chat_render.h"

#include "imgui.h"

namespace prime {

void ChatRender::add_user(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back(Line{true, text});
}

void ChatRender::add_ai(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back(Line{false, text});
}

void ChatRender::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
}

void ChatRender::draw() {
    if (ImGui::BeginChild("##chat")) {
        std::vector<Line> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = lines_;
        }

        for (const auto& line : snapshot) {
            if (line.from_user) {
                ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.66f, 1.0f), "You");
            } else {
                ImGui::TextColored(ImVec4(0.45f, 0.68f, 0.95f, 1.0f), "Prime");
            }
            ImGui::TextWrapped("%s", line.text.c_str());
            ImGui::Spacing();
        }

        // Stay at the bottom unless the user has scrolled up to read back.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

} // namespace prime
