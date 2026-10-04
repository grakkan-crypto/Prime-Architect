// rebuttal_turn.h — Rebuttal turn control (Prime_Architect/pipeline)
//
// NOT THE HOST. Holds a reference to RebuttalHost and tells it what to do —
// never touches row data. ALL host logic lives in the host.
//
// ACTIVATION — driven BY the switch's own push, never a call from Turn
//   Turn's entire part in the handover is flipping the switch. Nothing is
//   handed over — not even an id. The push landing here IS activation: this
//   file mints its own id (the same way any turn gets one) and opens the
//   first row. The id exists from the moment the row is created — whatever
//   captures the first exchange's input reads it off the row like anything
//   else would. Every interrupt AFTER the first is this file's own to
//   recognise while it holds control.
//
// ONE TURN PER INTERRUPT, held open however many exchanges it takes. All of
// them loosely anchored to the one system output that was interrupted, which
// is why exchange content can be referenced across the chain — and why
// nothing is torn down mid-chain: it accumulates for the window's whole life.
//
// REPLY — Commit's role, during rebuttal
//   Locks and renders the user's text onto the main chat window through that
//   surface's own host (ChatRender — real method, private data, the same
//   host discipline as everywhere else), retains the text on the current row
//   (the not-pinned-visibly copy), clears the exchange's input by DESTROYING
//   that turn's own input pool through the pool manipulation file — no pool
//   is ever wiped in place, destruction is the one act the Author's log
//   accounts for — and advances to the next turn id. Processing for the
//   reply's output was already running before the trigger landed.
//
// ADVANCE — one trigger, three arrivals (a completed reply, Synth
//   recognising "play", a future dedicated control), all landing as this one
//   call. Cycle order is strictly creation order, wrapping — the host owns
//   the mechanics.
//
// CLOSE — confirm and dismiss are the same operation, two doors:
//   CHAT (Adept-Infer recognising confirm/dismiss): this file acts first —
//     reads the current row's id off the host, tells the host the turn is
//     dismissed, resolves. One motion.
//   UI BUTTON: the host acts first — tears the row down, then tells this
//     file the id. This file resolves.
//   Resolution — the brutal shared-context teardown, in the one place both
//   doors land, straight through the pool manipulation file. Pool Cleanup
//   has ZERO involvement. The window accumulated for its whole life and
//   there is no next turn in this lane to keep anything warm for: every
//   shared-context pool stamped with the closing turn's id is checked
//   directly, in sequence, against the registry's prompt-link table exactly
//   as it exists — for each open id, its linked pools; a pool held by none
//   of them, however recently touched, is torn down. Only what is in use
//   right this moment survives. No earlier/later ordering exists here: this
//   file only ever resolves its own turn. Anything needed again later is
//   simply fetched again by whatever needs it, like any prompt would — the
//   rare case, not the rule, and not worth a mechanism. The LAST row
//   closing flips the switch off — this file's own last act, the one
//   direction of the switch that is its to flip.

#pragma once

#include "kv_pool.h"
#include "pool_maintenance2.h"

#include <string>

namespace prime {
class LiveRegistry;
class ChatRender;
}

namespace prime::frontend {

class RebuttalHost;

class RebuttalTurn {
public:

    RebuttalTurn(KvPoolAllocator& kv,
                 PoolManipulation& pools,
                 LiveRegistry& registry,
                 RebuttalHost& host,
                 ChatRender& chat);

    RebuttalTurn(const RebuttalTurn&)            = delete;
    RebuttalTurn& operator=(const RebuttalTurn&) = delete;

    void interrupt();

    void reply(std::string text);

    void advance();

    void dismissed_from_chat();

    void closed_from_ui(const std::string& turn_id);

private:

    void resolve_close(const std::string& turn_id);

    KvPoolAllocator&  kv_;
    PoolManipulation& pools_;
    LiveRegistry&     registry_;
    RebuttalHost&     host_;
    ChatRender&       chat_;
};

}
