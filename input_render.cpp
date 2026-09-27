// input_render.cpp

#include "input_render.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace prime {

InputRender::InputRender(prime::frontend::IngestionState& ingestion,
                         CommitFn                         commit,
                         AbortFn                          abort,
                         AnswerQuestionFn                 answer_question)
    : ingestion_(ingestion),
      commit_(std::move(commit)),
      abort_(std::move(abort)),
      answer_question_(std::move(answer_question)) {}

void InputRender::draw(bool busy) {
    draw_questions_box();

    ImGui::InputTextMultiline("##input", &ingestion_.composition, ImVec2(-1.0f, 90.0f));

    const bool has_text = !ingestion_.composition.empty();

    // Wiping the box aborts. Fires on the clear, not every frame it sits empty.
    if (had_text_last_frame_ && !has_text && abort_) abort_();
    had_text_last_frame_ = has_text;

    if (ImGui::Button("Commit", ImVec2(110.0f, 0.0f)) && commit_) commit_();

    if (busy) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.76f, 0.26f, 0.26f, 1.0f));
        if (ImGui::Button("Abort", ImVec2(110.0f, 0.0f)) && abort_) abort_();
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Mic", &ingestion_.mic_on);
    ImGui::SameLine();
    ImGui::Checkbox("TTS", &ingestion_.tts_on);
}

void InputRender::draw_questions_box() {
    if (ingestion_.questions.empty()) return;   // gone entirely, no space taken

    const auto& question = ingestion_.questions.front();

    // Switched to a different question — drop any half-typed answer so it can't
    // end up attached to the wrong one.
    if (answering_ != question.number) {
        answering_ = question.number;
        answer_[0] = '\0';
    }

    // Height grows and shrinks with the number of questions waiting.
    const float line   = ImGui::GetTextLineHeightWithSpacing();
    const float height = line * (2.0f + static_cast<float>(ingestion_.questions.size()));

    if (ImGui::BeginChild("##questions", ImVec2(0.0f, height), true)) {
        for (const auto& q : ingestion_.questions) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "?");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", q.text.c_str());
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-90.0f);
        const bool entered = ImGui::InputText("##answer", answer_, sizeof(answer_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool pressed = ImGui::Button("Answer");

        if ((entered || pressed) && answer_[0] != '\0' && answer_question_) {
            answer_question_(question.number, std::string(answer_));
            answer_[0] = '\0';
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
}

} // namespace prime
