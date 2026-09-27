// turn.h — Turn: ordinary turn lifecycle (Prime_Architect/pipeline)
//
// NO "CURRENT TURN". NO STORED LIST. EVER.
//   Any number of ordinary turns can be open at once, each watched and closed
//   independently. "Which turns are open" is never a separate question from
//   the pools themselves: a pool carrying a turn id, outside shared context
//   and the pipeline-load pools, IS an open turn. Every read here is a live
//   read at the moment of asking — nothing cached, nothing mirrored.
//
//   The ONE sanctioned exception is the winddown list (below): built once, at
//   one instant, from one live read, and gone the moment it empties.
//
// OPEN
//   A turn opens at first input — first keystroke or vocal utterance. open()
//   mints the id (from the one id source) and that is the whole act; the
//   pools created in the turn's service carry the id from there.
//
// WATCH — Turn's own act, the same continual-pass shape the sweep already
//   established. One pass reads the completion signals directly off the pools
//   and acts. No dispatcher, no watcher file, no relay telling Turn what a
//   pool already says. (The stepping mechanics that LAND generation_complete
//   on a stamp are the known deferred dependency; this file reads the
//   signal, it does not produce it.)
//     ORDINARY CLOSE: a User_Output-classified pool (a classification, not a
//       fixed name — several ordinary turns can be open) stamped with a turn
//       id shows generation complete. The shared-context resolution runs for
//       that turn and the turn is over — nothing stored, nothing to update.
//     CODING HOLD: the Auditor_Output pool (fixed name, checked
//       unconditionally, harmless no-op on a non-coding pipeline) shows
//       complete. NOT the close — the moment Analyst_Output takes the
//       reserve mark and the turn holds for the human decision:
//         Reject -> Analyst_Output destroyed. Close.
//         Write  -> Analyst_Output destroyed. Close. The file write and
//                   reload are the write path's own — it holds the path.
//         Edit   -> Analyst_Output migrates to shared context. Close. NO
//                   successor turn opens — migration is the entire action.
//       All three run the resolution BEFORE the terminal action, so it can
//       never eat what Edit is about to migrate. Delivery of the decision is
//       Aux's job (deferred); the three methods here are the receiving side.
//       The mark has no unreserve: every outcome is terminal, so nothing
//       ever returns the pool to the sweep's world.
//
// SHARED-CONTEXT RESOLUTION — runs at EVERY close, BEFORE the sweep migrates
// the closing turn's own output (that ordering is load-bearing). The ruled
// sequence, done as direct lookups in order at the one place they are
// needed — no wrapper functions:
//   1. The closing turn's id.
//   2. The registry's prompt-link table, read exactly as it exists: for each
//      open id (mask_open_ids), linked_pools(id). The union is every pool id
//      a live prompt currently holds. A shared-context pool whose pool id is
//      not in that union has no prompt against it. (This is the same
//      in-sequence read masking already performs on the same table.)
//   3. That pool's own turn id, off its own stamp in RAM.
//   4. Current or sooner than the closing turn -> destroyed. Later -> kept.
//      Comparison is the trailing numeric counter — the one sanctioned
//      exception to turn ids being opaque. Absent parses to zero and
//      destroys: the same arithmetic, no separate branch.
//   Kept-if-later plus kept-while-held is the warm cache: content a prompt
//   just used stays for the high-probability reuse of the next turn; what
//   nothing touches decays at the next close.
//
// THE HANDOVER — recognise the interrupt, flip the switch. Nothing else.
//   No call into RebuttalTurn, nothing handed over — not even an id. The
//   switch (LiveRegistry) pushes to its registered dependents; that push is
//   the entire mechanism. Turn hands over once: every interrupt after the
//   first is RebuttalTurn's own while it holds control.
//
//   THE LOOK, BEFORE THE FLIP: Turn reads the pipeline's busy fact. Not busy
//   -> flip and stand down completely. Busy -> ONE live scan at that instant
//   captures every ordinary turn id genuinely open (the rebuttal does not
//   exist yet — human input speed guarantees it; everything found is
//   ordinary by definition), then the flip. Turn works that winddown list to
//   zero — its own turns, its own close conditions, never Rebuttal's. The
//   list is never rescanned, never added to. The switch flipping back off
//   (all rebuttals closed) is pushed to Turn; any remainder dissolves back
//   into the ordinary live-scan world it came from.

#pragma once

#include "kv_pool.h"
#include "pool_maintenance2.h"

#include <string>
#include <vector>

namespace prime {

class LiveRegistry;

class Turn {
public:
    // Registers with the switch (on_rebuttal_switch) — the push is how Turn
    // learns the handover has ended.
    Turn(KvPoolAllocator& kv, PoolManipulation& pools, LiveRegistry& registry);

    Turn(const Turn&)            = delete;
    Turn& operator=(const Turn&) = delete;

    // First input. Mints and returns the turn id — the whole act of opening.
    std::string open();

    // The interrupt. The look (busy fact), the winddown snapshot if busy,
    // then the flip. A no-op while rebuttal is already active — subsequent
    // interrupts are RebuttalTurn's own.
    void interrupt();

    // One pass of Turn's own watch: the completion signals read off the
    // pools directly — ordinary completions close, Auditor_Output's
    // completion holds.
    void watch();

    // The human decision arriving (Aux delivers — deferred). Receiving side.
    void reject(const std::string& turn_id);
    void write(const std::string& turn_id);
    void edit(const std::string& turn_id);

private:
    // The resolution — the ruled sequence in the header block.
    void sc_teardown(const std::string& closing_turn_id);

    // Trailing numeric counter of a turn id; absent/unparsable reads 0.
    static uint64_t ordinal_of(const std::string& turn_id);

    KvPoolAllocator&  kv_;
    PoolManipulation& pools_;
    LiveRegistry&     registry_;

    // Pushed by the switch (on_rebuttal_switch). Never polled.
    bool rebuttal_active_ = false;

    // THE ONE EXCEPTION: built once at the pre-flip instant, worked to zero,
    // never rescanned, never added to, dropped on switch-off. Empty at all
    // other times.
    std::vector<std::string> winddown_;
};

} // namespace prime
