// rules_render.cpp — Rules & Directives panel render implementation

#include "rules_render.h"

#include "imgui.h"

#include <algorithm>

namespace prime {

namespace {

constexpr const char* kAiRulesList = "AI Rules";

const char* kind_label(RulesKind k) {
    switch (k) {
        case RulesKind::AiRules:        return "AI Rules";
        case RulesKind::AgentRules:     return "Agent Rules";
        case RulesKind::AgentDirective: return "Agent Directives";
    }
    return "AI Rules";
}

std::vector<char> buffer_for(const std::string& value) {
    std::vector<char> b(value.size() + 4096, '\0');
    std::copy(value.begin(), value.end(), b.begin());
    return b;
}

}

RulesRender::RulesRender(RulesPanelIO io) : io_(std::move(io)) {}

std::string RulesRender::current_list() const {
    return (kind_ == RulesKind::AiRules) ? kAiRulesList : selected_agent_;
}

void RulesRender::reset_view() {
    kind_ = RulesKind::AiRules;

    selected_pipeline_.clear();
    if (io_.active_pipeline) selected_pipeline_ = io_.active_pipeline();

    selected_agent_.clear();
    agents_.clear();
    if (!selected_pipeline_.empty() && io_.agents_for)
        agents_ = io_.agents_for(selected_pipeline_);

    editing_id_ = 0;
    copy_menu_for_ = 0;
    edit_buffer_.clear();
    new_entry_buffer_.clear();
    last_failed_ = false;
    last_message_.clear();

    reload();
}

void RulesRender::reload() {
    rows_.clear();
    directive_ = DirectiveView{};
    editing_id_ = 0;
    copy_menu_for_ = 0;

    if (selected_pipeline_.empty()) return;

    if (kind_ == RulesKind::AgentDirective) {
        if (!selected_agent_.empty() && io_.read_directive)
            if (auto d = io_.read_directive(selected_pipeline_, selected_agent_))
                directive_ = *d;
        you_are_buffer_ = directive_.you_are;
        return;
    }

    const std::string list = current_list();
    if (list.empty()) return;
    if (io_.read_rules) rows_ = io_.read_rules(selected_pipeline_, list);
}

void RulesRender::draw() {
    draw_kind_selector();
    ImGui::SameLine();
    draw_pipeline_selector();

    if (kind_ != RulesKind::AiRules) {
        ImGui::SameLine();
        draw_agent_selector();
    }

    ImGui::Separator();

    if (kind_ == RulesKind::AgentDirective) draw_directive();
    else                                    draw_rules_list();

    if (last_failed_) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                           last_message_.c_str());
    }
}

void RulesRender::draw_kind_selector() {
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##kind", kind_label(kind_))) {
        const RulesKind kinds[] = {
            RulesKind::AiRules, RulesKind::AgentRules, RulesKind::AgentDirective
        };
        for (RulesKind k : kinds) {
            const bool selected = (k == kind_);
            if (ImGui::Selectable(kind_label(k), selected) && !selected) {
                kind_ = k;
                if (kind_ != RulesKind::AiRules && selected_agent_.empty() &&
                    !agents_.empty())
                    selected_agent_ = agents_.front();
                reload();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void RulesRender::draw_pipeline_selector() {
    const char* preview = selected_pipeline_.empty() ? "(pipeline)"
                                                     : selected_pipeline_.c_str();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##pipeline", preview)) {

        std::vector<std::string> pipelines;
        if (io_.list_pipelines) pipelines = io_.list_pipelines();

        for (const auto& p : pipelines) {
            const bool selected = (p == selected_pipeline_);
            if (ImGui::Selectable(p.c_str(), selected) && !selected) {
                selected_pipeline_ = p;
                agents_.clear();
                if (io_.agents_for) agents_ = io_.agents_for(selected_pipeline_);
                selected_agent_.clear();
                if (kind_ != RulesKind::AiRules && !agents_.empty())
                    selected_agent_ = agents_.front();
                reload();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void RulesRender::draw_agent_selector() {
    const char* preview = selected_agent_.empty() ? "(agent)" : selected_agent_.c_str();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##agent", preview)) {
        for (const auto& a : agents_) {
            const bool selected = (a == selected_agent_);
            if (ImGui::Selectable(a.c_str(), selected) && !selected) {
                selected_agent_ = a;
                reload();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void RulesRender::draw_rules_list() {
    if (selected_pipeline_.empty()) {
        ImGui::TextDisabled("Select a pipeline.");
        return;
    }
    const std::string list = current_list();
    if (list.empty()) {
        ImGui::TextDisabled("Select an agent.");
        return;
    }

    if (list == kAiRulesList) {
        ImGui::TextUnformatted("AI Rules");
        ImGui::TextDisabled("Read by every agent in this pipeline, unmasked.");
    } else {
        ImGui::Text("%s Rules", list.c_str());
        const std::string over = io_.overseer_of
                               ? io_.overseer_of(selected_pipeline_, list)
                               : std::string();
        if (over.empty())
            ImGui::TextDisabled("Read by %s only - nothing oversees it.", list.c_str());
        else
            ImGui::TextDisabled("Read by %s and %s.", list.c_str(), over.c_str());
    }
    ImGui::Separator();

    for (auto& row : rows_) {
        ImGui::PushID(row.id);

        if (editing_id_ == row.id) {
            auto buf = buffer_for(edit_buffer_);
            ImGui::SetNextItemWidth(-220.0f);
            if (ImGui::InputText("##edit", buf.data(), buf.size()))
                edit_buffer_.assign(buf.data());

            ImGui::SameLine();
            if (ImGui::SmallButton("Save")) {

                const bool ok = io_.edit_rule &&
                                io_.edit_rule(selected_pipeline_, row.id, edit_buffer_);
                last_failed_ = !ok;
                last_message_ = ok ? std::string() : "Edit failed - not written.";
                editing_id_ = 0;
                if (ok) reload();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) editing_id_ = 0;

        } else {
            ImGui::TextUnformatted(row.text.c_str());

            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) {
                editing_id_ = row.id;
                edit_buffer_ = row.text;
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) copy_menu_for_ = row.id;

            if (row.linked()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Unlink")) {
                    const bool ok = io_.unlink_rule &&
                                    io_.unlink_rule(selected_pipeline_, row.id, list);
                    last_failed_ = !ok;
                    last_message_ = ok ? std::string() : "Unlink failed.";
                    if (ok) reload();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(linked x%d)",
                                    static_cast<int>(row.lists.size()));
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                const bool ok = io_.remove_rule &&
                                io_.remove_rule(selected_pipeline_, row.id, list);
                last_failed_ = !ok;
                last_message_ = ok ? std::string() : "Remove failed.";
                if (ok) reload();
            }

            if (copy_menu_for_ == row.id) draw_copy_menu(row);
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    auto buf = buffer_for(new_entry_buffer_);
    ImGui::SetNextItemWidth(-120.0f);
    if (ImGui::InputText("##new", buf.data(), buf.size()))
        new_entry_buffer_.assign(buf.data());

    ImGui::SameLine();
    ImGui::BeginDisabled(new_entry_buffer_.empty());
    if (ImGui::SmallButton("Add")) {
        const bool ok = io_.add_rule &&
                        io_.add_rule(selected_pipeline_, list, new_entry_buffer_);
        last_failed_ = !ok;
        last_message_ = ok ? std::string() : "Add failed - not written.";
        if (ok) {
            new_entry_buffer_.clear();
            reload();
        }
    }
    ImGui::EndDisabled();
}

void RulesRender::draw_copy_menu(const RuleRow& row) {
    ImGui::Indent();
    ImGui::TextDisabled("Copy to:");

    std::vector<std::string> destinations;
    if (kind_ != RulesKind::AiRules) destinations.push_back(kAiRulesList);
    for (const auto& a : agents_) destinations.push_back(a);

    for (const auto& dest : destinations) {
        if (std::find(row.lists.begin(), row.lists.end(), dest) != row.lists.end())
            continue;

        ImGui::PushID(dest.c_str());
        if (ImGui::SmallButton(dest.c_str())) {
            const bool ok = io_.link_rule &&
                            io_.link_rule(selected_pipeline_, row.id, dest);
            last_failed_ = !ok;
            last_message_ = ok ? std::string() : "Copy failed.";
            copy_menu_for_ = 0;
            if (ok) reload();
        }
        ImGui::PopID();
        ImGui::SameLine();
    }

    ImGui::NewLine();
    if (ImGui::SmallButton("Close")) copy_menu_for_ = 0;
    ImGui::Unindent();
}

void RulesRender::draw_directive() {
    if (selected_pipeline_.empty()) {
        ImGui::TextDisabled("Select a pipeline.");
        return;
    }
    if (selected_agent_.empty()) {
        ImGui::TextDisabled("Select an agent.");
        return;
    }

    ImGui::Text("%s Functional Directive", selected_agent_.c_str());
    ImGui::TextDisabled("Read by %s alone. Never its Arbiter, never anyone else.",
                        selected_agent_.c_str());
    ImGui::Separator();

    ImGui::TextUnformatted("Functional identity");
    auto you_buf = buffer_for(you_are_buffer_);
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##youare", you_buf.data(), you_buf.size()))
        you_are_buffer_.assign(you_buf.data());

    ImGui::SameLine();
    if (ImGui::SmallButton("Save##you")) {
        const bool ok = io_.save_you_are &&
                        io_.save_you_are(selected_pipeline_, selected_agent_,
                                         you_are_buffer_);
        last_failed_ = !ok;
        last_message_ = ok ? std::string() : "Save failed - not written.";
        if (ok) reload();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Behaviour");

    for (size_t i = 0; i < directive_.entries.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));

        ImGui::BulletText("%s", directive_.entries[i].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            auto entries = directive_.entries;
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
            const bool ok = io_.save_directive_entries &&
                            io_.save_directive_entries(selected_pipeline_,
                                                       selected_agent_, entries);
            last_failed_ = !ok;
            last_message_ = ok ? std::string() : "Remove failed.";
            if (ok) reload();
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    auto buf = buffer_for(new_entry_buffer_);
    ImGui::SetNextItemWidth(-120.0f);
    if (ImGui::InputText("##newdir", buf.data(), buf.size()))
        new_entry_buffer_.assign(buf.data());

    ImGui::SameLine();
    ImGui::BeginDisabled(new_entry_buffer_.empty());
    if (ImGui::SmallButton("Add")) {
        auto entries = directive_.entries;
        entries.push_back(new_entry_buffer_);
        const bool ok = io_.save_directive_entries &&
                        io_.save_directive_entries(selected_pipeline_,
                                                   selected_agent_, entries);
        last_failed_ = !ok;
        last_message_ = ok ? std::string() : "Add failed - not written.";
        if (ok) {
            new_entry_buffer_.clear();
            reload();
        }
    }
    ImGui::EndDisabled();
}

}
