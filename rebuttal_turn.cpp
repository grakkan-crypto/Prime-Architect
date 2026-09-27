// rebuttal_turn.cpp — Rebuttal turn control implementation

#include "rebuttal_turn.h"

#include "chat_render.h"
#include "id_generation.h"
#include "live_registry.h"
#include "rebuttal_host.h"

#include <algorithm>
#include <vector>

namespace prime::frontend {

RebuttalTurn::RebuttalTurn(KvPoolAllocator& kv,
                           PoolManipulation& pools,
                           LiveRegistry& registry,
                           RebuttalHost& host,
                           ChatRender& chat)
    : kv_(kv), pools_(pools), registry_(registry), host_(host), chat_(chat) {
    registry_.on_rebuttal_switch([this](bool on) {
        // The push IS activation: nothing was handed over; this file mints
        // its own id and opens the first row. The id exists from this moment,
        // readable off the row by whatever captures the first exchange. The
        // switch only signals on CHANGE, so the on-push fires exactly once
        // per handover; the off-push is this file's own last act echoed back
        // and needs nothing.
        if (on) {
            const std::string id = IdGeneration::instance().mint_turn_id();
            host_.add_row(id);
        }
    });
}

void RebuttalTurn::interrupt() {
    // An interrupt while this file holds control — its own to recognise. One
    // turn per interrupt, the same act as activation's first.
    const std::string id = IdGeneration::instance().mint_turn_id();
    host_.add_row(id);
}

void RebuttalTurn::reply(std::string text) {
    const std::string id = host_.current_id();
    if (id.empty()) return;

    // Commit's role, during rebuttal: the text locks and renders onto the
    // main chat window, through that surface's own host...
    chat_.add_user(text);

    // ...is retained on the row (the not-pinned-visibly copy, reachable via
    // the row's own control once the output finishes)...
    host_.append_input(id, std::move(text));

    // ...the exchange's input is cleared by DESTROYING this turn's own input
    // pool through the manipulation file — scoped by the turn id on the
    // pool's own stamp, never a pipeline-wide sweep, never a wipe-in-place
    // (destruction is the one act the Author's log accounts for)...
    for (const auto& name : kv_.all_names()) {
        if (kv_.classification(name) != classification::kUserInput) continue;
        auto s = kv_.stamp(name);
        if (s.has_value() && s->turn_id == id) pools_.destroy(name);
    }

    // ...and the cycle advances to the next turn id. Processing for this
    // reply's output was already running before the trigger landed.
    host_.advance();
}

void RebuttalTurn::advance() {
    // One trigger, three arrivals — a completed reply, Synth recognising
    // "play", a future dedicated control — all landing as this one call.
    host_.advance();
}

void RebuttalTurn::dismissed_from_chat() {
    // The chat door: this file acts first. The current row's id read off the
    // host and the dismissal told to it — one motion, not two calls bouncing
    // a fact around.
    const std::string id = host_.current_id();
    if (id.empty()) return;
    host_.remove(id);
    resolve_close(id);
}

void RebuttalTurn::closed_from_ui(const std::string& turn_id) {
    // The UI door: the host already tore the row down. Resolve.
    if (turn_id.empty()) return;
    resolve_close(turn_id);
}

void RebuttalTurn::resolve_close(const std::string& turn_id) {
    // THE BRUTAL TEARDOWN, right here — one place, both doors. The window
    // accumulated for its whole life; there is no next turn in this lane to
    // keep anything warm for. Only what a live prompt holds right this
    // moment survives.
    //
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

    // Every shared-context pool of this turn, off RAM: held by nothing right
    // now -> torn down, however recently it was touched.
    for (const auto& name : kv_.all_names()) {
        if (kv_.classification(name) != classification::kSharedContext)
            continue;
        auto s = kv_.stamp(name);
        if (!s.has_value() || s->turn_id != turn_id) continue;
        if (std::find(held.begin(), held.end(), s->pool_id) != held.end())
            continue;
        pools_.destroy(name);
    }

    // The LAST row out flips the switch off — this file's own last act, and
    // the one direction of the switch that is its to flip.
    if (host_.row_count() == 0) registry_.set_rebuttal_active(false);
}

} // namespace prime::frontend
