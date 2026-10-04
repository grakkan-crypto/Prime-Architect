// rules_render.h — Rules & Directives panel, render layer
//
// The operator writes in ordinary words here. This is the ONLY place natural
// language exists — the moment anything leaves this panel it is JSON, and it
// stays JSON in RAM, on disk, and in every pool an agent reads. Opening the
// panel is the reverse: the stored entries come back as lines in boxes.
//
// That conversion is mechanical in both directions because entries are authored
// as short, direct, itemised lines and stored as exactly those lines. Nothing is
// ever collapsed into a paragraph, so nothing has to be unpacked from one.
//
// THE CONTROLS
//   KIND dropdown — AI Rules / Agent Rules / Agent Directives.
//   AGENT dropdown — shown for Agent Rules and Agent Directives ONLY. AI Rules
//                    is its own standing list with no agent to pick, so the
//                    control is absent, not greyed.
//   PIPELINE dropdown — ALWAYS shown. Rules are pipeline-scoped; Directive is
//                    not, but the pipeline still decides which agents exist.
//
// LISTS, NOT TEXT BOXES
//   Rules is a list of entries, each its own line, added and removed one at a
//   time. Directive is a fixed "You are ..." line plus a list beneath it.
//
//   Every list shows under its header — "AI Rules", "[Agent] Rules", "[Agent]
//   Functional Directive" — the same header that goes into the pool, so what the
//   operator sees and what the agent reads are labelled identically.
//
// AI RULES IS ITS OWN LIST
//   Not a tag meaning "everyone". Every agent reads it because that is what the
//   list is. Putting a rule there does NOT place it in any agent's own list.
//
// COPY AND UNLINK — ON RULES ONLY, NEVER DIRECTIVE
//   Each rule row carries a copy icon. It offers every other list as a
//   destination — other agents, and AI Rules when viewing an agent. Choosing one
//   links the SAME entry into that list, so it appears in both, and editing
//   either changes both.
//
//   That second, closer, named appearance under an agent's own header is the
//   whole point: an explicitly labelled repetition carries more weight than one
//   distant mention.
//
//   A linked row also carries an unlink icon. It detaches this list's occurrence
//   into its own independent entry, free to diverge from then on; every other
//   list stays linked together.
//
//   Directive rows have neither. Directive is agent-specific by nature and never
//   shared.
//
// NO LIMITS
//   No cap on entry length or entry count.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace prime {

enum class RulesKind { AiRules, AgentRules, AgentDirective };

struct RuleRow {
    int         id = 0;
    std::string text;

    std::vector<std::string> lists;
    bool linked() const { return lists.size() > 1; }
};

struct DirectiveView {
    std::string              you_are;
    std::vector<std::string> entries;
};

struct RulesPanelIO {
    std::function<std::vector<std::string>()> list_pipelines;
    std::function<std::string()>              active_pipeline;

    std::function<std::vector<std::string>(const std::string& pipeline)> agents_for;
    std::function<std::string(const std::string& pipeline,
                              const std::string& agent)> overseer_of;

    std::function<std::vector<RuleRow>(const std::string& pipeline,
                                       const std::string& list)> read_rules;

    std::function<std::optional<DirectiveView>(const std::string& pipeline,
                                               const std::string& agent)> read_directive;

    std::function<bool(const std::string& pipeline,
                       const std::string& list,
                       const std::string& text)> add_rule;
    std::function<bool(const std::string& pipeline,
                       int id, const std::string& text)> edit_rule;
    std::function<bool(const std::string& pipeline,
                       int id, const std::string& list)> remove_rule;
    std::function<bool(const std::string& pipeline,
                       int id, const std::string& to_list)> link_rule;
    std::function<bool(const std::string& pipeline,
                       int id, const std::string& from_list)> unlink_rule;

    std::function<bool(const std::string& pipeline,
                       const std::string& agent,
                       const std::string& you_are)> save_you_are;
    std::function<bool(const std::string& pipeline,
                       const std::string& agent,
                       const std::vector<std::string>& entries)> save_directive_entries;
};

class RulesRender {
public:
    explicit RulesRender(RulesPanelIO io);

    void draw();
    void reset_view();

private:
    void draw_kind_selector();
    void draw_pipeline_selector();
    void draw_agent_selector();
    void draw_rules_list();
    void draw_directive();
    void draw_copy_menu(const RuleRow& row);

    std::string current_list() const;

    void reload();

    RulesPanelIO io_;

    RulesKind   kind_ = RulesKind::AiRules;
    std::string selected_pipeline_;
    std::string selected_agent_;

    std::vector<std::string> agents_;
    std::vector<RuleRow>     rows_;
    DirectiveView            directive_;

    int         editing_id_ = 0;
    std::string edit_buffer_;
    std::string new_entry_buffer_;
    std::string you_are_buffer_;

    int         copy_menu_for_ = 0;

    bool        last_failed_ = false;
    std::string last_message_;
};

}
