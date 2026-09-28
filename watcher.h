// watcher.h — Watcher: the one file in the system that polls. It is asked to
//             watch for something; it goes and looks, compares, and tells
//             whoever asked when the thing occurs.
//
// ===========================================================================
// WHAT THIS FILE IS
//
//   A file like any other. It runs when it has something to do and does not
//   run when it does not. The first request is what starts it; when the last
//   request has gone it stops. While it runs it is one loop: read every
//   source something has named, once; work out what has newly occurred;
//   deliver; replace its previous copies; go again. Immediately.
//
//   Watcher fires on an occurrence. It never reports that something is still
//   so. A file that needs the span of a thing registers for its start and
//   for its end and works the span out itself.
//
//   Between passes this file holds: the live list, the reading list, one
//   previous copy of each source on it — a pool read carrying its own view
//   of itself by class, turn and prompt, the read seen differently, living
//   and dying with it — the forbidden list of pools, the published copy of
//   that list, the queue at the doors, the requests it refused, kept for
//   Wellness, and the pool-file table, asked of Pool Maintenance once before
//   its first visit to the screen and kept for the session. Nothing else.
//   [[COW-EDIT 61]]
//
// ===========================================================================
// NAMING
//
//   Every call into this file is Watcher_[action], so a call visibly starts
//   with Watcher. A name here is never a word another file might use for its
//   own list, queue, or button; the prefix is what makes that structurally
//   true, not care. The forbidden list is data, not a call, and carries no
//   prefix. The one instance is reached as watcher(), the same way the live
//   whiteboard is reached.
//
// ===========================================================================
// OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by
// definition. It is raised with the user, never made.
//
// 1. WATCHER IS THE ONLY FILE IN THE SYSTEM THAT POLLS.
//    Nothing else loops, checks its own input on a timer, or announces
//    itself downstream. Nothing checks Watcher. A second loop anywhere, for
//    any reason, is wrong by definition. Registering with Watcher does not
//    make anything part of Watcher.
//
// 2. A FIRING IS AN OCCURRENCE, NEVER A STANDING STATE.
//    An item becomes eligible only when the previous read against the
//    current one shows it entering the tested state. A request that arrives
//    to find something already there is not told about it. Once used, the
//    item is spent for that request — on the attempt, not on the delivery;
//    a check that knocks the firing back still spends it — and it is not
//    eligible again until it has left the state and entered it again. The
//    record of what was used is written after delivery, and only for a
//    request still on the list; a request leaving on this pass writes none,
//    there being nothing left to keep the record for. Spent is the request's
//    own: the same occurrence is whole and eligible for every other request
//    that watches for it. Never add a retry, a pending state, a first-look
//    exception, or a refire on "still true".
//
// 3. THE COUNT COUNTS DELIVERIES. ZERO IS ENDLESS.
//    Not passes, not attempts, not checks. A knocked-back attempt does not
//    move it. When one pass produces more occurrences than the count has
//    left, the count's worth are delivered — oldest first — and the request
//    leaves; the rest are not delivered to anyone. At zero remaining the
//    request leaves by the same removal as a deregistration.
//
// 4. A CHECK IS NEVER CAUSAL AND NEVER SPENT.
//    A check may sit true for any number of passes and Watcher does nothing
//    about it. It is evaluated at exactly one moment: when the request's
//    triggers have produced a match on this pass. It gates that firing and
//    is forgotten. Its items are never tracked, never spent, never counted.
//    Its source is read every pass like any other, so that it has a
//    previous copy to compare against when the moment comes.
//
// 5. THE STOP TOKEN IS THE ONE THING WATCHER UNDERSTANDS, AND THE FORBIDDEN
//    LIST IS THE ONE THING IT DOES WITH IT.
//    Every other read is compared, like for like, and reported; nothing is
//    interpreted. A pool whose stop token Watcher has read goes on the
//    forbidden list and is never read again, not once, not to check.
//    Nothing in it changes again; whatever needs its content afterwards has
//    its own way to it and that is no concern of this file. It leaves the
//    list only when its ID has left the map, and an ID leaves the map by one
//    route: destruction. Between those it is eligible for destruction, and
//    every destruction follows Watcher having read that token; word cannot
//    go out before the read that produced it has completed. Nothing else in
//    the system reads the stop token. Never add a second reader.
//    THE LIST IS THE ONE DEFINITIVE STATEMENT OF WHAT IS FORBIDDEN. It is
//    written here alone, and what is readable is never the list being
//    edited: at the end of every pass on which the list changed, a complete
//    copy is published whole, and a published copy is never touched again.
//    A reader reads the copy, directly — no call into this file, no broker —
//    and holds a finished list, last pass's or this pass's, never one
//    mid-edit, for as long as it likes.
//
// 6. THE LIVE LIST HAS ONE EDITOR.
//    Adding and removing are both editing, and nothing outside this file
//    does either. The doors — Watcher_Register, Watcher_Deregister,
//    Watcher_ScopeTeardown, Watcher_Wake — queue. The pass drains the queue
//    in arrival order before it reads. A request arriving mid-pass joins the
//    next pass.
//
// 7. EVERY REQUEST HAS A WAY OUT.
//    It fires itself out, it is deregistered by name, or a scope it carries
//    ends and takes everything under it. A request carries one scope or
//    several and leaves on the first of them to end. Endless with no ending
//    scope is refused. None alongside an ending scope is accepted and does
//    nothing — ruled, not overlooked. A scope goes on the list only once the
//    thing that ends it exists. A scope ending with nothing under it is
//    nothing, correctly.
//
// 8. INSTRUCTIONS ARRIVE COMPLETE. NOTHING IS DERIVED, INFERRED, CALCULATED,
//    DEFAULTED OR ASSUMED.
//    Every part of a request is stated by the registrant, including scope
//    none, which is a stated value and never a blank. Anything not to the
//    convention is refused whole: one flag, and the request retained exactly
//    as handed for Wellness. It never touches the live list. The caller
//    hears nothing. Watcher never corrects, never partially accepts, never
//    guesses what was meant; Wellness, which can see what Watcher cannot,
//    works out what was meant and registers the corrected request through
//    the ordinary door as its own.
//
// 9. WATCHER READS FOR ITSELF, WHOLE, ONCE PER PASS.
//    Watcher goes and looks for itself and asks nothing of anyone. A
//    requester never supplies a way of reading, and whether the requester
//    can reach the source itself does not matter.
//    Every source is read whole, once per pass, and selection happens
//    afterwards, in evaluation, never at the read.
//
// 10. A SOURCE IS READ ONLY WHILE SOMETHING NAMES IT, AND FORGOTTEN THE
//     MOMENT NOTHING DOES.
//     No copy is kept "in case". The reading list is maintained by count at
//     the events that can change it — register, deregister, fire-out, scope
//     end — and never rebuilt. The map is read before any pool's content.
//
// 11. THE CLOCK AND THE CALENDAR ARE NEVER SOURCES, AND THE PASS NEVER WAITS.
//     No timer, no backoff, no throttle between passes. A file with a time
//     in mind holds its own request until the time comes and hands it over
//     then. When there is nothing to do — the live list is empty — Watcher
//     is not running; a door starts it. Sleep is not nothing to do: asleep,
//     foreground requests are not evaluated and the sources only they name
//     are not read; background continues untouched; on waking, foreground
//     resumes as it was, because nothing in the foreground happened while
//     the system slept.
//
// 12. DELIVERY IS THE MESSAGE AND THE NAME. NOTHING ELSE.
//     Not which item, not which cycle, not why. Handed over, the receipt is
//     mechanical, and Watcher moves on; it never waits on what a recipient
//     does, exactly as no file waits on Watcher. A recipient that needs to
//     know which goes and reads the map itself. Watcher never resolves a
//     name into a destination, never composes or alters a message, never
//     lends its access to anyone. Being told a thing happened is not
//     permission to act on it.
//
// ===========================================================================
// THE REQUEST — supplied whole by the registrant (Ruling 8).
//
//   NAME          Chosen by the registrant, meaningful to the recipient, the
//                 handle for deregistration, delivered with every firing.
//                 Must not match a name currently live. A removed name is
//                 free.
//   TRIGGERS      One or more entries. These make occurrences.
//   CHECKS        Zero or more entries. These gate (Ruling 4).
//   COMBINE       How the triggers combine: And, Or. NOT is per entry.
//   RECIPIENTS    One or more, each the means of reaching it, used as given.
//   MESSAGE       Delivered verbatim.
//   COUNT         Deliveries before the request leaves. Zero is endless.
//   SCOPES        One or more, each one of kWatcherScopes or
//                 kWatcherScopeNone. Stated. The request leaves on the first
//                 to end; none ends nothing.
//   ACTIVE LEVEL  Foreground or background (Ruling 11).
//
// THE ENTRY
//   Which source, and which of it: a pool source names its pools by pool ID,
//   class (its number, as the registrant has it — Watcher derives nothing
//   from a name), turn, prompt, or any combination, each an inclusion or an
//   exclusion; a flag or a file is named by name or path; memory by address
//   and length. Then the test, the value the test compares against where it
//   takes one, and NOT.
//
//   Exists       an item is present.
//   Destroyed    an item was present on the previous read and is not now.
//                A fact of the map: a pool leaves the content read by being
//                finished, not by being destroyed.
//   Changed      two reads of the item are not exactly the same. On the map
//                that is the bytes assigned to a pool, or its class, moving.
//   Equals, Contains, GreaterThan, LessThan
//                the item's value against the given value, like for like.
//                GreaterThan and LessThan are numeric: the given value must
//                be a number or the request is refused; a read that is not a
//                number is flagged for Wellness and does not hold.
//   NOT          inverts the entry. On a selector that names a population it
//                becomes one state of the source — "none matching is so".
//
//   And          one unspent item on every trigger entry, on the same pass,
//                is one complete match; the number of complete matches is
//                the smallest such count across the entries, and where an
//                entry has more than that, the oldest by creation are used.
//   Or           every unspent item on any entry is its own complete match.
//
// WHAT AN ITEM IS
//   A pool carries its record — ID, class, turn, prompts, creation time,
//   bytes assigned — and, for content, what was read. Everything else is
//   one thing with one value: a flag's value, a file's bytes, a memory
//   region's bytes. Presence is the item being there at all.
// ===========================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace prime {

// The scopes Watcher will accept. It has no idea what any of them is; it
// checks the value is one it has been told is real. None is a stated value.
inline const std::vector<std::string> kWatcherScopes   = { "pipeline", "Turn", "RebuttalTurn" };
inline constexpr const char*          kWatcherScopeNone = "none";

enum class SourceKind  { PoolMap, PoolContent, Ram, Vram, Flag, DiskFile };
enum class Test        { Exists, Destroyed, Changed, Equals, Contains, GreaterThan, LessThan };
enum class Combine     { And, Or };
enum class ActiveLevel { Foreground, Background };

// One attribute of a pool selector. No values: any. Values: one of them, or,
// when exclude is set, anything but one of them. The value is the
// attribute's own type: class is a number, the rest are text.
template <class T>
struct Selection {
    std::vector<T> values;
    bool           exclude = false;
};

struct Selector {
    Selection<std::string>   pool_id;
    Selection<std::uint64_t> class_id;
    Selection<std::string>   turn_id;
    Selection<std::string>   prompt_id;
};

struct Entry {
    SourceKind    kind    = SourceKind::PoolMap;
    std::string   key;                  // Flag: its name. DiskFile: its path.
    std::uint64_t address = 0;          // Ram, Vram
    std::uint64_t length  = 0;          // Ram, Vram
    Selector      selector;             // PoolMap, PoolContent
    Test          test    = Test::Exists;
    std::string   value;                // Equals, Contains, GreaterThan, LessThan
    bool          negate  = false;
};

// The means of reaching one recipient, used exactly as given: the request's
// name and message go in; the mechanical receipt comes back, at once. A
// shape, not an operation of this file's: each recipient fills it in.
using Reach = std::function<bool(const std::string& name, const std::string& message)>;

struct Request {
    std::string              name;
    std::vector<Entry>       triggers;
    std::vector<Entry>       checks;
    Combine                  combine = Combine::And;
    std::vector<Reach>       recipients;
    std::string              message;
    std::uint64_t            count   = 0;     // 0: endless
    std::vector<std::string> scopes;
    ActiveLevel              level   = ActiveLevel::Foreground;
};

// A pool as the map states it. Class is the number LiveRegistry assigned;
// the rest is text.
struct PoolRecord {
    std::string              id;
    std::uint64_t            class_id     = 0;
    std::string              turn_id;
    std::vector<std::string> prompt_ids;
    std::uint64_t            timestamp_ns = 0;
    std::uint64_t            bytes        = 0;
};

inline bool operator==(const PoolRecord& a, const PoolRecord& b) {
    return a.id == b.id && a.class_id == b.class_id && a.turn_id == b.turn_id &&
           a.prompt_ids == b.prompt_ids && a.timestamp_ns == b.timestamp_ns &&
           a.bytes == b.bytes;
}

// The shape of one read of one item.
struct WatcherItem {
    std::string               value;
    std::optional<PoolRecord> pool;
};

inline bool operator==(const WatcherItem& a, const WatcherItem& b) {
    return a.value == b.value && a.pool == b.pool;
}

// One read of one source: every item it holds, by Watcher's own key. A
// single-valued source is one item with an empty key.
using Snapshot = std::map<std::string, WatcherItem>;

// A pool read's own view of itself: which pool IDs sit under which class,
// which turn, which prompt. Pool reads only — nothing else carries one. Built
// once at the read, from the read, and going wherever the read goes; never
// kept in step with anything because it never outlives what it was built
// from.
struct PoolView {
    std::map<std::uint64_t, std::vector<std::string>> by_class;
    std::map<std::string,   std::vector<std::string>> by_turn;
    std::map<std::string,   std::vector<std::string>> by_prompt;
};

// ---- the mechanism ----------------------------------------------------------
class Watcher {
    // Declared first so the view below is bound to it: the published copy of
    // the forbidden list. Replaced whole, never edited (Ruling 5). Starts as
    // an empty list, never as nothing.
    std::atomic<std::shared_ptr<const std::set<std::string>>> forbidden_published_{
        std::make_shared<const std::set<std::string>>() };

public:
    Watcher() = default;

    Watcher(const Watcher&)            = delete;
    Watcher& operator=(const Watcher&) = delete;

    // THE DOORS. Each queues and, if Watcher is not running, starts it.
    void Watcher_Register(Request request);
    void Watcher_Deregister(const std::string& name);
    void Watcher_ScopeTeardown(const std::string& scope);

    // Sleep and wake (Ruling 11).
    void Watcher_Sleep();
    void Watcher_Wake();

    // Requests refused at the door, exactly as handed, for Wellness. Watcher
    // appends; Wellness clears.
    std::vector<Request>& Watcher_Retained();

    // THE FORBIDDEN LIST, read directly (Ruling 5). The one definitive
    // statement of what is forbidden. What is read here is always a finished
    // list — the last one this file published, whole — and a reader that has
    // taken one holds it unchanged for as long as it likes. Nothing writes
    // through this.
    const std::atomic<std::shared_ptr<const std::set<std::string>>>& forbidden = forbidden_published_;

private:
    struct Door {
        enum Kind { Register, Deregister, ScopeTeardown, Wake } kind = Register;
        Request     request;
        std::string name;
    };

    struct Live {
        Request                               request;
        std::uint64_t                         remaining = 0;
        std::vector<std::vector<std::string>> pending;   // per trigger: eligible, unspent
        std::vector<std::set<std::string>>    spent;     // per trigger: used, still in state
    };

    struct Watched {
        int foreground = 0;
        int background = 0;
    };

    void            open(Door door);
    void            run();
    void            remove(std::size_t i);
    void            watch(const Request& request, int delta);
    const Snapshot* snapshot_of(const Entry& entry, bool previous) const;
    const PoolView* view_of(const Entry& entry, bool previous) const;

    // the doors' queue and the running/asleep state — the one guarded thing
    std::mutex        door_mutex_;
    std::vector<Door> doors_;
    bool              running_ = false;
    bool              asleep_  = false;

    std::vector<Live>    live_;
    std::vector<Request> retained_;

    // the reading list, by count (Ruling 10)
    Watched                         pool_map_watch_;
    Watched                         pool_content_watch_;
    std::map<std::string, Watched>  flags_;
    std::map<std::string, Watched>  files_;
    std::map<std::string, Watched>  memory_;

    // this pass, and the previous pass
    Snapshot                        map_now_,          prev_map_;
    PoolView                        map_view_now_,     prev_map_view_;
    Snapshot                        content_now_,      prev_content_;
    PoolView                        content_view_now_, prev_content_view_;
    std::map<std::string, Snapshot> flags_now_,        prev_flags_;
    std::map<std::string, Snapshot> files_now_,        prev_files_;
    std::map<std::string, Snapshot> memory_now_,       prev_memory_;
    bool                            has_map_     = false;
    bool                            has_content_ = false;

    // the forbidden list as this file edits it (Ruling 5). Pruned at the
    // read, added to at bookkeeping, published when it changed.
    std::set<std::string>           forbidden_;

    // [[COW-EDIT 62]] The pool-file table, asked of Pool Maintenance once,
    // before the first visit to the screen, and kept for the session.
    std::map<std::string, std::string> pool_files_;
    bool                               has_pool_files_ = false;
};

// The one instance, there for the life of the process.
Watcher& watcher();

} // namespace prime
