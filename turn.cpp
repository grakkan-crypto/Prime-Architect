// turn.cpp — Turn: ordinary turn lifecycle implementation

#include "turn.h"

#include "id_generation.h"
#include "live_registry.h"

#include <algorithm>

namespace prime {

namespace {
    // The one fixed pool name Turn checks unconditionally (a no-op when the
    // pipeline has no such pool). Auditor Workbench is DEAD as a name.
    const char* const kAuditorOutput = "Auditor_Output";
    const char* const kAnalystOutput = "Analyst_Output";
}

Turn::Turn(KvPoolAllocator& kv, PoolManipulation& pools, LiveRegistry& registry)
    : kv_(kv), pools_(pools), registry_(registry) {
    registry_.on_rebuttal_switch([this](bool active) {
        rebuttal_active_ = active;
        // Switch off: the handover is over. Any winddown remainder dissolves
        // back into the live-scan world it was snapshotted from — those turns
        // are still visible on the pools, exactly as they always were.
        if (!active) winddown_.clear();
    });
}

std::string Turn::open() {
    return IdGeneration::instance().mint_turn_id();
}

void Turn::interrupt() {
    // Turn hands over ONCE. Everything after the first interrupt is
    // RebuttalTurn's own to recognise while it holds control.
    if (rebuttal_active_) return;

    // THE LOOK, BEFORE THE FLIP.
    //
    // DEFERRED — the busy gate. This reads the pipeline's activity fact off
    // the LiveRegistry. That wiring does not exist yet (nothing sets the
    // activity fact anywhere, and the registry-side read is not built), so
    // until it lands this gate resolves not-busy and the winddown snapshot
    // never fires. On the deferred list by direct instruction — stated here
    // so it is never mistaken for a decision.
    const bool busy = false;

    if (busy) {
        // ONE live scan, at this instant. The rebuttal does not exist yet —
        // nothing found here can be anything but ordinary. A pool carrying a
        // turn id outside shared context IS an open turn; distinct ids only.
        winddown_.clear();
        for (const auto& name : kv_.all_names()) {
            auto s = kv_.stamp(name);
            if (!s.has_value() || s->turn_id.empty()) continue;
            if (kv_.classification(name) == classification::kSharedContext)
                continue;
            if (std::find(winddown_.begin(), winddown_.end(), s->turn_id) ==
                winddown_.end())
                winddown_.push_back(s->turn_id);
        }
    }

    // The flip. The switch's own push activates the receiving side — Turn
    // calls nothing and hands nothing over.
    registry_.set_rebuttal_active(true);
}

void Turn::watch() {
    for (const auto& name : kv_.all_names()) {
        auto s = kv_.stamp(name);
        if (!s.has_value() || !s->generation_complete) continue;

        // During the handover Turn only works its winddown list — its own
        // turns, captured before the flip. Anything else completing belongs
        // to Rebuttal's lane and is not Turn's to touch.
        if (rebuttal_active_ &&
            std::find(winddown_.begin(), winddown_.end(), s->turn_id) ==
                winddown_.end())
            continue;

        // CODING HOLD. Auditor_Output completing is not a close — it is the
        // moment Analyst_Output takes the reserve mark and the turn holds for
        // the human decision. The mark twice is the same fact twice. Setting
        // the mark directly is an agent-shaped act on the pool, not a
        // lifecycle act — deliberately NOT routed through the manipulation
        // file. The mark's accessor lands with the same pending stamp-layer
        // absorption every stamp read here already presumes.
        if (name == kAuditorOutput) {
            kv_.reserve(kAnalystOutput);
            continue;
        }

        // ORDINARY CLOSE. A User_Output-classified pool of a turn showing
        // complete closes that turn: the resolution runs, and the turn is
        // over — there is nothing stored anywhere to update.
        if (kv_.classification(name) == classification::kUserOutput &&
            !s->turn_id.empty()) {
            sc_teardown(s->turn_id);
            auto it = std::find(winddown_.begin(), winddown_.end(), s->turn_id);
            if (it != winddown_.end()) winddown_.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// The human decision (Aux delivers). All three run the resolution FIRST so it
// can never eat what Edit is about to migrate, then take the terminal action.
// There is no unreserve: every outcome is terminal.
// ---------------------------------------------------------------------------
void Turn::reject(const std::string& turn_id) {
    sc_teardown(turn_id);
    pools_.destroy(kAnalystOutput);
}

void Turn::write(const std::string& turn_id) {
    // The file write and reload are the write path's own act — it holds the
    // file's path at the moment it writes. Turn's part is identical to
    // reject: the pool's life ends here.
    sc_teardown(turn_id);
    pools_.destroy(kAnalystOutput);
}

void Turn::edit(const std::string& turn_id) {
    sc_teardown(turn_id);
    // Migration is the ENTIRE action. No successor turn opens — the next
    // input opens the next turn like any other.
    pools_.reclassify(kAnalystOutput, classification::kSharedContext);
}

// ---------------------------------------------------------------------------
// The resolution — the ruled sequence, direct lookups in order.
// ---------------------------------------------------------------------------
void Turn::sc_teardown(const std::string& closing_turn_id) {
    const uint64_t closing = ordinal_of(closing_turn_id);

    // The pool ids a live prompt currently holds: for each open id, its
    // linked pools — the registry's own table, read exactly as it exists.
    // The same in-sequence read masking already performs on this table; open
    // turn ids simply contribute nothing (no links are keyed by them).
    std::vector<std::string> held;
    for (const auto& [open_id, controller] : registry_.mask_open_ids()) {
        (void)controller;
        for (const auto& pool_id : registry_.linked_pools(open_id))
            held.push_back(pool_id);
    }

    // Every shared-context pool, off RAM: no live prompt against it, and its
    // own stamped turn current-or-sooner than the closing turn -> destroyed.
    // Later -> kept.
    for (const auto& name : kv_.all_names()) {
        if (kv_.classification(name) != classification::kSharedContext)
            continue;
        auto s = kv_.stamp(name);
        if (!s.has_value()) continue;
        if (std::find(held.begin(), held.end(), s->pool_id) != held.end())
            continue;
        if (ordinal_of(s->turn_id) <= closing)
            pools_.destroy(name);
    }
}

uint64_t Turn::ordinal_of(const std::string& turn_id) {
    const auto dash = turn_id.rfind('-');
    if (dash == std::string::npos || dash + 1 >= turn_id.size()) return 0;
    uint64_t v = 0;
    for (size_t i = dash + 1; i < turn_id.size(); ++i) {
        const char c = turn_id[i];
        if (c < '0' || c > '9') return 0;
        v = v * 10 + (uint64_t)(c - '0');
    }
    return v;
}

} // namespace prime
