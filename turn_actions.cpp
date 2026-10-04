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

    if (!ingestion_.composition.empty()) {
        chat_.add_user(ingestion_.composition);
        ingestion_.composition.clear();
    }

    if (on_commit_) on_commit_();
}

void TurnActions::abort() {

    if (on_abort_) on_abort_();

    if (roster_) activity_.clear(roster_());
}

}
