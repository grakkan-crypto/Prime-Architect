// turn_actions.cpp

#include "turn_actions.h"

namespace prime {

TurnActions::TurnActions(prime::frontend::IngestionState& ingestion,
                         prime::frontend::AgentActivity&  activity,
                         ChatRender&                      chat,
                         std::function<void()>            on_commit,
                         std::function<void()>            on_abort,
                         std::function<std::vector<std::string>()> roster)
    : ingestion_(ingestion),
      activity_(activity),
      chat_(chat),
      on_commit_(std::move(on_commit)),
      on_abort_(std::move(on_abort)),
      roster_(std::move(roster)) {}

void TurnActions::commit() {
    // The user's composed prompt enters the conversation and the box clears.
    if (!ingestion_.composition.empty()) {
        chat_.add_user(ingestion_.composition);
        ingestion_.composition.clear();
    }
    // The turn does NOT end here — the system still has work to do. This only
    // signals that the user has finished their part.
    if (on_commit_) on_commit_();
}

void TurnActions::abort() {
    // Stop the work first...
    if (on_abort_) on_abort_();
    // ...then mark this pipeline's agents idle, so the bar can't stay running
    // if an agent was stopped before it reported finishing. Only this
    // pipeline's agents — nothing else is touched.
    if (roster_) activity_.clear(roster_());
}

} // namespace prime
