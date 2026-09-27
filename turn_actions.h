// turn_actions.h — What Commit and Abort actually do.
//
// The behaviour behind the two buttons, kept out of the window shell. The shell
// draws the buttons and calls these when they're pressed; it doesn't decide
// what they mean.
//
// COMMIT is not turn-end.
//   It's the user signalling that THEIR part — composing — is done. The turn
//   keeps going: the system still has work to finish, and that work is part of
//   the same turn. The turn only actually ends once the last of it has finished
//   and produced its output. That end is a separate moment, handled elsewhere;
//   this is only the user's commit.
//
//   On commit: the typed prompt goes into the chat window, the box is cleared,
//   and the caller's on-commit hook runs (e.g. letting the system know the user
//   has finished composing).
//
// ABORT is scoped to this pipeline.
//   The caller's stop runs, then this pipeline's agents are marked not-working.
//   Only the agents passed in — background agents and any other loaded pipeline
//   are never touched.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "agent_activity.h"
#include "chat_render.h"
#include "ingestion_state.h"

namespace prime {

class TurnActions {
public:
    // on_commit — runs after the prompt is placed and the box cleared. What the
    //             system does when the user finishes composing.
    // on_abort  — runs before this pipeline's agents are marked idle. The actual
    //             stop.
    // roster    — returns this pipeline's agents. A function, not a fixed list,
    //             because the loaded pipeline can change; abort always reads the
    //             current one.
    TurnActions(prime::frontend::IngestionState& ingestion,
                prime::frontend::AgentActivity&  activity,
                ChatRender&                      chat,
                std::function<void()>            on_commit,
                std::function<void()>            on_abort,
                std::function<std::vector<std::string>()> roster);

    void commit();
    void abort();

private:
    prime::frontend::IngestionState& ingestion_;
    prime::frontend::AgentActivity&  activity_;
    ChatRender&                      chat_;

    std::function<void()>                     on_commit_;
    std::function<void()>                     on_abort_;
    std::function<std::vector<std::string>()> roster_;
};

} // namespace prime
