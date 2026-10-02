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
//   Wellness, and the map key, asked of Pool Maintenance once before its
//   first visit to the screen and kept for the session. Nothing else.
//   [[COW-EDIT 72]]
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
// definition and is never made.
//
// WATCHER IS THE ONLY FILE IN THE SYSTEM THAT POLLS.
//
// 1. WATCHER LOOKS FOR THE STOP TOKEN IN EVERY POOL IT READS, AND NOTHING
//    ELSE READS IT.
//
// 2. EVERY REQUEST MUST HAVE AN EXPIRY.
//
// 3. A REQUEST ARRIVES COMPLETE.
//    Nothing in it is derived, inferred, calculated, defaulted or assumed.
//
// 4. WATCHER READS FOR ITSELF.
//    It asks nothing of anyone but the map key, once a session.
//
// 5. NOTHING IS READ OR KEPT THAT NO REQUEST NAMES.
//
// 6. THE CLOCK IS NEVER A SOURCE, AND THE PASS NEVER WAITS.
//    No timer, no backoff, no throttle.
//
// 7. A DELIVERY IS THE NAME AND THE MESSAGE, NOTHING ELSE.
//    Watcher never alters a message, never waits on a recipient, and never
//    lends its access.
//
// 8. WATCHER NEVER WRITES TO ANYTHING IT READS.
//
// 9. WATCHER COMPARES; IT NEVER INTERPRETS.
//    The stop token is the one exception.
//
// 10. WATCHER'S ONLY OUTPUTS ARE DELIVERIES AND THE FORBIDDEN LIST.
//
// 11. THE FORBIDDEN LIST IS WRITTEN BY WATCHER ALONE.
//
// 12. WATCHER REPORTS CHANGE, NEVER A STANDING STATE.
//
// ===========================================================================
// THE REQUEST — supplied whole by the registrant.
//
//   NAME          Chosen by the registrant, meaningful to the recipient, the
//                 handle for deregistration, delivered with every firing.
//                 Must not match a name currently live. A removed name is
//                 free.
//   TRIGGERS      One or more entries. These make occurrences.
//   CHECKS        Zero or more entries. These gate.
//   COMBINE       How the triggers combine: And, Or. NOT is per entry.
//   RECIPIENTS    One or more, each the means of reaching it, used as given.
//   MESSAGE       Delivered verbatim.
//   COUNT         Deliveries before the request leaves. Zero is endless.
//   SCOPES        One or more, each one of kWatcherScopes or
//                 kWatcherScopeNone. Stated. The request leaves on the first
//                 to end; none ends nothing.
//   ACTIVE LEVEL  Foreground or background.
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

#include "pool_maintenance.h"   // [[COW-EDIT 73]] the map key

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
    // the forbidden list. Replaced whole, never edited. Starts as
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

    // Sleep and wake.
    void Watcher_Sleep();
    void Watcher_Wake();

    // Requests refused at the door, exactly as handed, for Wellness. Watcher
    // appends; Wellness clears.
    std::vector<Request>& Watcher_Retained();

    // THE FORBIDDEN LIST, read directly. The one definitive
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

    // the reading list, by count
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

    // the forbidden list as this file edits it. Pruned at the
    // read, added to at bookkeeping, published when it changed.
    std::set<std::string>           forbidden_;

    // [[COW-EDIT 74]] The map key, asked of Pool Maintenance once, before the
    // first visit to the screen, and kept for the session.
    MapKey                          map_key_;
    bool                            has_map_key_ = false;
};

// The one instance, there for the life of the process.
Watcher& watcher();

} // namespace prime
