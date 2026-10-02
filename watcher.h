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
// ===========================================================================

// [[COW-EDIT 72]]

#pragma once

#include "pool_maintenance.h"   // [[COW-EDIT 73]]

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

inline const std::vector<std::string> kWatcherScopes   = { "pipeline", "Turn", "RebuttalTurn" };
inline constexpr const char*          kWatcherScopeNone = "none";

enum class SourceKind  { PoolMap, PoolContent, Ram, Vram, Flag, DiskFile };
enum class Test        { Exists, Destroyed, Changed, Equals, Contains, GreaterThan, LessThan };
enum class Combine     { And, Or };
enum class ActiveLevel { Foreground, Background };

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
    std::string   key;
    std::uint64_t address = 0;
    std::uint64_t length  = 0;
    Selector      selector;
    Test          test    = Test::Exists;
    std::string   value;
    bool          negate  = false;
};

using Reach = std::function<bool(const std::string& name, const std::string& message)>;

struct Request {
    std::string              name;
    std::vector<Entry>       triggers;
    std::vector<Entry>       checks;
    Combine                  combine = Combine::And;
    std::vector<Reach>       recipients;
    std::string              message;
    std::uint64_t            count   = 0;
    std::vector<std::string> scopes;
    ActiveLevel              level   = ActiveLevel::Foreground;
};

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

struct WatcherItem {
    std::string               value;
    std::optional<PoolRecord> pool;
};

inline bool operator==(const WatcherItem& a, const WatcherItem& b) {
    return a.value == b.value && a.pool == b.pool;
}

using Snapshot = std::map<std::string, WatcherItem>;

struct PoolView {
    std::map<std::uint64_t, std::vector<std::string>> by_class;
    std::map<std::string,   std::vector<std::string>> by_turn;
    std::map<std::string,   std::vector<std::string>> by_prompt;
};

class Watcher {
    std::atomic<std::shared_ptr<const std::set<std::string>>> forbidden_published_{
        std::make_shared<const std::set<std::string>>() };

public:
    Watcher() = default;

    Watcher(const Watcher&)            = delete;
    Watcher& operator=(const Watcher&) = delete;

    void Watcher_Register(Request request);
    void Watcher_Deregister(const std::string& name);
    void Watcher_ScopeTeardown(const std::string& scope);

    void Watcher_Sleep();
    void Watcher_Wake();

    std::vector<Request>& Watcher_Retained();

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
        std::vector<std::vector<std::string>> pending;
        std::vector<std::set<std::string>>    spent;
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

    std::mutex        door_mutex_;
    std::vector<Door> doors_;
    bool              running_ = false;
    bool              asleep_  = false;

    std::vector<Live>    live_;
    std::vector<Request> retained_;

    Watched                         pool_map_watch_;
    Watched                         pool_content_watch_;
    std::map<std::string, Watched>  flags_;
    std::map<std::string, Watched>  files_;
    std::map<std::string, Watched>  memory_;

    Snapshot                        map_now_,          prev_map_;
    PoolView                        map_view_now_,     prev_map_view_;
    Snapshot                        content_now_,      prev_content_;
    PoolView                        content_view_now_, prev_content_view_;
    std::map<std::string, Snapshot> flags_now_,        prev_flags_;
    std::map<std::string, Snapshot> files_now_,        prev_files_;
    std::map<std::string, Snapshot> memory_now_,       prev_memory_;
    bool                            has_map_     = false;
    bool                            has_content_ = false;

    std::set<std::string>           forbidden_;

    // [[COW-EDIT 74]]
    MapKey                          map_key_;
    bool                            has_map_key_ = false;
};

Watcher& watcher();

}
