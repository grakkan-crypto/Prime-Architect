#include "watcher.h"

#include "live_registry.h"
#include "pool_maintenance.h"
#include "text_file.h"

#include <algorithm>
#include <cstdlib>
#include <thread>
#include <utility>

namespace prime {

std::string                pool_content(const PoolRecord& pool);
std::optional<std::string> unified_memory(std::uint64_t address, std::uint64_t length);
std::optional<std::string> flag_value(const std::string& name);
std::string                stop_token();

}

namespace prime {

namespace {

template <class T>
bool selected(const Selection<T>& s, const T& v) {
    if (s.values.empty()) return true;
    const bool listed = std::find(s.values.begin(), s.values.end(), v) != s.values.end();
    return listed != s.exclude;
}

bool matches(const Selector& s, const WatcherItem& item) {
    if (!item.pool) return true;
    const PoolRecord& p = *item.pool;
    if (!selected(s.pool_id, p.id))         return false;
    if (!selected(s.class_id, p.class_id))  return false;
    if (!selected(s.turn_id, p.turn_id))    return false;
    if (s.prompt_id.values.empty())         return true;
    bool prompt_ok = s.prompt_id.exclude;
    for (const std::string& id : p.prompt_ids) {
        const bool ok = selected(s.prompt_id, id);
        prompt_ok = s.prompt_id.exclude ? (prompt_ok && ok) : (prompt_ok || ok);
    }
    return prompt_ok;
}

bool numeric(const std::string& text, double& out) {
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0';
}

Snapshot single(std::optional<std::string> v) {
    Snapshot s;
    if (v) s.emplace(std::string{}, WatcherItem{ std::move(*v), std::nullopt });
    return s;
}

using Sought = std::pair<Test, std::string>;

std::string memory_key(const Entry& e) {
    return std::to_string(e.address) + ":" + std::to_string(e.length);
}

std::set<std::string> candidates(const Selector& s, const Snapshot& snap, const PoolView* view) {
    std::set<std::string> out;
    if (!s.pool_id.values.empty() && !s.pool_id.exclude) {
        for (const std::string& id : s.pool_id.values)
            if (snap.count(id)) out.insert(id);
        return out;
    }
    if (view != nullptr) {
        const auto filed_under = [&out](const auto& by, const auto& filed) {
            for (const auto& v : by.values) {
                const auto it = filed.find(v);
                if (it != filed.end()) out.insert(it->second.begin(), it->second.end());
            }
        };
        if (!s.class_id.values.empty()  && !s.class_id.exclude)  { filed_under(s.class_id,  view->by_class);  return out; }
        if (!s.turn_id.values.empty()   && !s.turn_id.exclude)   { filed_under(s.turn_id,   view->by_turn);   return out; }
        if (!s.prompt_id.values.empty() && !s.prompt_id.exclude) { filed_under(s.prompt_id, view->by_prompt); return out; }
    }
    for (const auto& [id, item] : snap) out.insert(id);
    return out;
}

std::vector<std::string> in_state(const Entry& e,
                                  const Snapshot& now, const PoolView* now_view,
                                  const Snapshot* old, const PoolView* old_view) {
    std::vector<std::string> ids;
    switch (e.test) {
    case Test::Exists:
        for (const std::string& id : candidates(e.selector, now, now_view))
            if (matches(e.selector, now.at(id))) ids.push_back(id);
        break;

    case Test::Destroyed:
        if (old != nullptr)
            for (const std::string& id : candidates(e.selector, *old, old_view))
                if (matches(e.selector, old->at(id)) && !now.count(id)) ids.push_back(id);
        break;

    case Test::Changed:
        if (old != nullptr) {
            for (const std::string& id : candidates(e.selector, now, now_view)) {
                const WatcherItem& item = now.at(id);
                if (!matches(e.selector, item)) continue;
                const auto b = old->find(id);
                if (b == old->end() || !(b->second == item)) ids.push_back(id);
            }
            for (const std::string& id : candidates(e.selector, *old, old_view))
                if (matches(e.selector, old->at(id)) && !now.count(id)) ids.push_back(id);
        }
        break;

    default:
        for (const std::string& id : candidates(e.selector, now, now_view)) {
            const WatcherItem& item = now.at(id);
            if (!matches(e.selector, item)) continue;
            bool holds = false;
            if (e.test == Test::Equals) {
                holds = item.value == e.value;
            } else if (e.test == Test::Contains) {
                holds = item.value.find(e.value) != std::string::npos;
            } else {
                double a = 0.0;
                double b = 0.0;
                const bool wellness_check_watcher_read_numeric = numeric(item.value, a);
                (void)wellness_check_watcher_read_numeric;
                numeric(e.value, b);
                holds = wellness_check_watcher_read_numeric &&
                        (e.test == Test::GreaterThan ? a > b : a < b);
            }
            if (holds) ids.push_back(id);
        }
        break;
    }
    if (e.negate) return ids.empty() ? std::vector<std::string>{ std::string{} }
                                     : std::vector<std::string>{};
    return ids;
}

std::vector<std::string> narrowed(const Entry& e, const std::vector<std::string>& found,
                                  const Snapshot& look, const Snapshot* also) {
    std::vector<std::string> ids;
    for (const std::string& id : found) {
        const auto it = look.find(id);
        if (matches(e.selector, it != look.end() ? it->second : also->at(id))) ids.push_back(id);
    }
    if (e.negate) return ids.empty() ? std::vector<std::string>{ std::string{} }
                                     : std::vector<std::string>{};
    return ids;
}

}

void Watcher::open(Door door) {
    bool start = false;
    {
        std::lock_guard<std::mutex> lock(door_mutex_);
        doors_.push_back(std::move(door));
        if (!running_) { running_ = true; start = true; }
    }
    if (start) std::thread([this] { run(); }).detach();
}

void Watcher::Watcher_Register(Request request) {
    Door d;
    d.kind    = Door::Register;
    d.request = std::move(request);
    open(std::move(d));
}

void Watcher::Watcher_Deregister(const std::string& name) {
    Door d;
    d.kind = Door::Deregister;
    d.name = name;
    open(std::move(d));
}

void Watcher::Watcher_ScopeTeardown(const std::string& scope) {
    Door d;
    d.kind = Door::ScopeTeardown;
    d.name = scope;
    open(std::move(d));
}

void Watcher::Watcher_Sleep() {
    std::lock_guard<std::mutex> lock(door_mutex_);
    asleep_ = true;
}

void Watcher::Watcher_Wake() {
    {
        std::lock_guard<std::mutex> lock(door_mutex_);
        asleep_ = false;
    }
    Door d;
    d.kind = Door::Wake;
    open(std::move(d));
}

std::vector<Request>& Watcher::Watcher_Retained() {
    return retained_;
}

void Watcher::watch(const Request& r, int delta) {
    const bool background = r.level == ActiveLevel::Background;
    for (const std::vector<Entry>* list : { &r.triggers, &r.checks })
        for (const Entry& e : *list) {
            if (e.kind == SourceKind::PoolMap || e.kind == SourceKind::PoolContent) {
                Watched& w = e.kind == SourceKind::PoolMap ? pool_map_watch_ : pool_content_watch_;
                (background ? w.background : w.foreground) += delta;
                if (e.kind == SourceKind::PoolMap && w.foreground == 0 && w.background == 0) {
                    map_now_.clear();     prev_map_.clear();
                    map_view_now_ = PoolView{}; prev_map_view_ = PoolView{};
                    has_map_ = false;
                }
                if (e.kind == SourceKind::PoolContent && w.foreground == 0 && w.background == 0) {
                    content_now_.clear(); prev_content_.clear();
                    content_view_now_ = PoolView{}; prev_content_view_ = PoolView{};
                    has_content_ = false;
                }
                continue;
            }
            const std::string key = e.kind == SourceKind::Flag || e.kind == SourceKind::DiskFile
                                        ? e.key : memory_key(e);
            auto& counts = e.kind == SourceKind::Flag     ? flags_
                         : e.kind == SourceKind::DiskFile ? files_      : memory_;
            auto& now    = e.kind == SourceKind::Flag     ? flags_now_
                         : e.kind == SourceKind::DiskFile ? files_now_  : memory_now_;
            auto& prev   = e.kind == SourceKind::Flag     ? prev_flags_
                         : e.kind == SourceKind::DiskFile ? prev_files_ : prev_memory_;
            Watched& w = counts[key];
            (background ? w.background : w.foreground) += delta;
            if (w.foreground == 0 && w.background == 0) {
                counts.erase(key);
                now.erase(key);
                prev.erase(key);
            }
        }
}

void Watcher::remove(std::size_t i) {
    watch(live_[i].request, -1);
    live_.erase(live_.begin() + static_cast<std::ptrdiff_t>(i));
}

const Snapshot* Watcher::snapshot_of(const Entry& e, bool previous) const {
    switch (e.kind) {
    case SourceKind::PoolMap:
        return previous ? (has_map_ ? &prev_map_ : nullptr)
                        : (map_now_.empty() && !has_map_ ? nullptr : &map_now_);
    case SourceKind::PoolContent:
        return previous ? (has_content_ ? &prev_content_ : nullptr)
                        : (content_now_.empty() && !has_content_ ? nullptr : &content_now_);
    default: {
        const auto& m  = e.kind == SourceKind::Flag     ? (previous ? prev_flags_  : flags_now_)
                       : e.kind == SourceKind::DiskFile ? (previous ? prev_files_  : files_now_)
                                                        : (previous ? prev_memory_ : memory_now_);
        const auto  it = m.find(e.kind == SourceKind::Flag || e.kind == SourceKind::DiskFile
                                    ? e.key : memory_key(e));
        return it == m.end() ? nullptr : &it->second;
    }
    }
}

const PoolView* Watcher::view_of(const Entry& e, bool previous) const {
    if (snapshot_of(e, previous) == nullptr) return nullptr;
    switch (e.kind) {
    case SourceKind::PoolMap:     return previous ? &prev_map_view_     : &map_view_now_;
    case SourceKind::PoolContent: return previous ? &prev_content_view_ : &content_view_now_;
    default:                      return nullptr;
    }
}

void Watcher::run() {
    for (;;) {
        std::vector<Door> doors;
        bool asleep = false;
        {
            std::lock_guard<std::mutex> lock(door_mutex_);
            doors.swap(doors_);
            asleep = asleep_;
        }

        for (Door& d : doors) {
            if (d.kind == Door::Wake) continue;
            if (d.kind == Door::Deregister) {
                for (std::size_t i = 0; i < live_.size(); ++i)
                    if (live_[i].request.name == d.name) { remove(i); break; }
                continue;
            }
            if (d.kind == Door::ScopeTeardown) {
                for (std::size_t i = live_.size(); i-- > 0;) {
                    const std::vector<std::string>& sc = live_[i].request.scopes;
                    if (std::find(sc.begin(), sc.end(), d.name) != sc.end()) remove(i);
                }
                continue;
            }

            Request& r = d.request;
            bool valid = !r.name.empty() &&
                         !r.triggers.empty() &&
                         !r.recipients.empty() &&
                         !r.scopes.empty();
            bool ends = false;
            for (const std::string& s : r.scopes) {
                const bool none   = s == kWatcherScopeNone;
                const bool listed = std::find(kWatcherScopes.begin(), kWatcherScopes.end(), s) != kWatcherScopes.end();
                if (!none && !listed) valid = false;
                if (listed) ends = true;
            }
            if (r.count == 0 && !ends) valid = false;
            for (const Live& l : live_)
                if (l.request.name == r.name) valid = false;
            for (const std::vector<Entry>* list : { &r.triggers, &r.checks })
                for (const Entry& e : *list) {
                    const bool pool   = e.kind == SourceKind::PoolMap || e.kind == SourceKind::PoolContent;
                    const bool memory = e.kind == SourceKind::Ram || e.kind == SourceKind::Vram;
                    const bool named  = e.kind == SourceKind::Flag || e.kind == SourceKind::DiskFile;
                    const bool selects = !e.selector.pool_id.values.empty() ||
                                         !e.selector.class_id.values.empty() ||
                                         !e.selector.turn_id.values.empty() ||
                                         !e.selector.prompt_id.values.empty();
                    if (pool   && (!e.key.empty() || e.address != 0 || e.length != 0)) valid = false;
                    if (memory && (!e.key.empty() || e.length == 0 || selects))        valid = false;
                    if (named  && (e.key.empty()  || e.address != 0 || e.length != 0 || selects)) valid = false;
                    const bool takes_value = e.test == Test::Equals || e.test == Test::Contains ||
                                             e.test == Test::GreaterThan || e.test == Test::LessThan;
                    if (!takes_value && !e.value.empty()) valid = false;
                    double n = 0.0;
                    if ((e.test == Test::GreaterThan || e.test == Test::LessThan) && !numeric(e.value, n)) valid = false;
                }

            const bool wellness_check_watcher_request_well_formed = valid;
            (void)wellness_check_watcher_request_well_formed;
            if (!valid) { retained_.push_back(std::move(r)); continue; }

            bool resolves = true;
            for (const Reach& reach : r.recipients)
                if (!reach) resolves = false;
            const bool wellness_check_watcher_recipient_resolves = resolves;
            (void)wellness_check_watcher_recipient_resolves;

            Live l;
            l.remaining = r.count;
            l.pending.resize(r.triggers.size());
            l.spent.resize(r.triggers.size());
            l.request   = std::move(r);
            watch(l.request, +1);
            live_.push_back(std::move(l));
        }

        if (live_.empty()) {
            std::lock_guard<std::mutex> lock(door_mutex_);
            if (doors_.empty()) { running_ = false; return; }
            continue;
        }

        const bool read_map     = asleep ? pool_map_watch_.background > 0
                                         : pool_map_watch_.foreground + pool_map_watch_.background > 0;
        const bool read_content = asleep ? pool_content_watch_.background > 0
                                         : pool_content_watch_.foreground + pool_content_watch_.background > 0;
        bool forbidden_changed = false;
        std::vector<PoolRecord> map;
        PoolView                view;
        if (read_map || read_content) {
            if (!has_map_key_) {
                map_key_     = pool_maintenance().map_key();
                has_map_key_ = true;
            }
            const MapKey& k      = map_key_;
            const void*   screen = live_registry().screen;
            map.reserve(k.unit_count);
            for (std::uint64_t i = 0; i < k.unit_count; ++i) {
                const std::uint8_t* p = map_record(k, screen, i);
                if (p == nullptr) continue;
                PoolRecord r;
                r.id           = map_field<std::string>(p, k.pool_id);
                r.class_id     = map_field<std::uint64_t>(p, k.class_id);
                r.turn_id      = map_field<std::string>(p, k.turn_id);
                const auto& prompts = map_field<std::set<std::string>>(p, k.prompt_ids);
                r.prompt_ids.assign(prompts.begin(), prompts.end());
                r.timestamp_ns = map_field<std::uint64_t>(p, k.timestamp_ns);
                r.bytes        = map_field<std::uint64_t>(p, k.byte_capacity);
                map.push_back(std::move(r));
            }
            for (auto it = forbidden_.begin(); it != forbidden_.end();) {
                bool present = false;
                for (const PoolRecord& p : map) if (p.id == *it) { present = true; break; }
                if (present) ++it;
                else { it = forbidden_.erase(it); forbidden_changed = true; }
            }
            for (const PoolRecord& p : map) {
                view.by_class[p.class_id].push_back(p.id);
                view.by_turn[p.turn_id].push_back(p.id);
                for (const std::string& pr : p.prompt_ids) view.by_prompt[pr].push_back(p.id);
            }
        }
        if (read_map) {
            map_now_.clear();
            for (const PoolRecord& p : map) map_now_.emplace(p.id, WatcherItem{ {}, p });
            map_view_now_ = view;
        }
        if (read_content) {
            content_now_.clear();
            for (const PoolRecord& p : map) {
                if (forbidden_.count(p.id)) continue;
                content_now_.emplace(p.id, WatcherItem{ pool_content(p), p });
            }
            content_view_now_ = PoolView{};
            const auto less_forbidden = [this](const auto& from, auto& to) {
                for (const auto& [key, ids] : from)
                    for (const std::string& id : ids)
                        if (!forbidden_.count(id)) to[key].push_back(id);
            };
            less_forbidden(view.by_class,  content_view_now_.by_class);
            less_forbidden(view.by_turn,   content_view_now_.by_turn);
            less_forbidden(view.by_prompt, content_view_now_.by_prompt);
        }
        for (const auto& [name, w] : flags_)
            if (asleep ? w.background > 0 : true) flags_now_[name] = single(flag_value(name));
        for (const auto& [path, w] : files_)
            if (asleep ? w.background > 0 : true) {
                std::string blob;
                files_now_[path] = single(read_text_file(path, blob) == FileRead::Ok
                                              ? std::optional<std::string>(std::move(blob)) : std::nullopt);
            }
        for (const auto& [key, w] : memory_)
            if (asleep ? w.background > 0 : true) {
                const std::size_t   colon   = key.find(':');
                const std::uint64_t address = std::strtoull(key.substr(0, colon).c_str(), nullptr, 10);
                const std::uint64_t length  = std::strtoull(key.substr(colon + 1).c_str(), nullptr, 10);
                memory_now_[key] = single(unified_memory(address, length));
            }

        std::map<Sought, std::vector<std::string>> found_now;
        std::map<Sought, std::vector<std::string>> found_before;
        std::string token;
        if (read_content) {
            token = stop_token();
            const Snapshot* old      = has_content_ ? &prev_content_      : nullptr;
            const PoolView* old_view = has_content_ ? &prev_content_view_ : nullptr;
            const auto search = [&](Test test, const std::string& value, bool before) {
                const Sought key{ test, value };
                Entry bare;
                bare.kind  = SourceKind::PoolContent;
                bare.test  = test;
                bare.value = value;
                if (!found_now.count(key))
                    found_now.emplace(key, in_state(bare, content_now_, &content_view_now_, old, old_view));
                if (before && old != nullptr && !found_before.count(key))
                    found_before.emplace(key, in_state(bare, *old, old_view, nullptr, nullptr));
            };
            search(Test::Contains, token, false);
            for (const Live& l : live_) {
                if (asleep && l.request.level == ActiveLevel::Foreground) continue;
                for (const Entry& e : l.request.triggers)
                    if (e.kind == SourceKind::PoolContent) search(e.test, e.value, true);
                for (const Entry& e : l.request.checks)
                    if (e.kind == SourceKind::PoolContent) search(e.test, e.value, false);
            }
        }

        std::vector<std::size_t> fired_out;
        for (std::size_t i = 0; i < live_.size(); ++i) {
            Live&          l = live_[i];
            const Request& r = l.request;
            if (asleep && r.level == ActiveLevel::Foreground) continue;

            bool comparable = true;
            for (const Entry& e : r.triggers)
                if (snapshot_of(e, false) == nullptr || snapshot_of(e, true) == nullptr) comparable = false;
            if (!comparable) continue;

            for (std::size_t k = 0; k < r.triggers.size(); ++k) {
                const Entry&    e        = r.triggers[k];
                const Snapshot& now      = *snapshot_of(e, false);
                const Snapshot& old      = *snapshot_of(e, true);
                const PoolView* now_view = view_of(e, false);
                const PoolView* old_view = view_of(e, true);
                const bool   shared = e.kind == SourceKind::PoolContent;
                const Sought key{ e.test, e.value };
                const std::vector<std::string> now_ids    = shared ? narrowed(e, found_now.at(key), now, &old)
                                                                   : in_state(e, now, now_view, &old, old_view);
                const std::vector<std::string> before_ids = shared ? narrowed(e, found_before.at(key), old, nullptr)
                                                                   : in_state(e, old, old_view, nullptr, nullptr);

                std::vector<std::string> pending;
                for (const std::string& id : l.pending[k])
                    if (std::find(now_ids.begin(), now_ids.end(), id) != now_ids.end()) pending.push_back(id);
                std::set<std::string> spent;
                for (const std::string& id : l.spent[k])
                    if (std::find(now_ids.begin(), now_ids.end(), id) != now_ids.end()) spent.insert(id);
                for (const std::string& id : now_ids) {
                    if (std::find(before_ids.begin(), before_ids.end(), id) != before_ids.end()) continue;
                    if (std::find(pending.begin(), pending.end(), id) != pending.end()) continue;
                    spent.erase(id);
                    pending.push_back(id);
                }
                std::stable_sort(pending.begin(), pending.end(),
                    [&now](const std::string& a, const std::string& b) {
                        const auto ia = now.find(a);
                        const auto ib = now.find(b);
                        const std::uint64_t ta = ia != now.end() && ia->second.pool ? ia->second.pool->timestamp_ns : 0;
                        const std::uint64_t tb = ib != now.end() && ib->second.pool ? ib->second.pool->timestamp_ns : 0;
                        return ta < tb;
                    });
                l.pending[k] = std::move(pending);
                l.spent[k]   = std::move(spent);
            }

            std::vector<std::vector<std::string>> used(r.triggers.size());
            std::uint64_t complete = 0;
            if (r.combine == Combine::And) {
                complete = l.pending[0].size();
                for (const auto& p : l.pending) complete = std::min<std::uint64_t>(complete, p.size());
                for (std::size_t k = 0; k < l.pending.size(); ++k) {
                    used[k].assign(l.pending[k].begin(), l.pending[k].begin() + static_cast<std::ptrdiff_t>(complete));
                    l.pending[k].erase(l.pending[k].begin(), l.pending[k].begin() + static_cast<std::ptrdiff_t>(complete));
                }
            } else {
                for (std::size_t k = 0; k < l.pending.size(); ++k) {
                    complete += l.pending[k].size();
                    used[k] = std::move(l.pending[k]);
                    l.pending[k].clear();
                }
            }
            if (complete == 0) continue;

            bool passes = true;
            for (const Entry& c : r.checks) {
                const Snapshot* now = snapshot_of(c, false);
                if (now == nullptr) { passes = false; continue; }
                const std::vector<std::string> ids = c.kind == SourceKind::PoolContent
                    ? narrowed(c, found_now.at(Sought{ c.test, c.value }), *now, snapshot_of(c, true))
                    : in_state(c, *now, view_of(c, false), snapshot_of(c, true), view_of(c, true));
                if (ids.empty()) passes = false;
            }

            bool leaves = false;
            if (passes) {
                const std::uint64_t firings = r.count == 0 ? complete : std::min(complete, l.remaining);
                for (std::uint64_t n = 0; n < firings; ++n)
                    for (const Reach& reach : r.recipients) {
                        const bool wellness_check_watcher_delivery_reachable = static_cast<bool>(reach);
                        (void)wellness_check_watcher_delivery_reachable;
                        if (reach) reach(r.name, r.message);
                    }
                if (r.count != 0) {
                    l.remaining -= firings;
                    if (l.remaining == 0) { fired_out.push_back(i); leaves = true; }
                }
            }

            if (!leaves)
                for (std::size_t k = 0; k < used.size(); ++k)
                    for (const std::string& id : used[k]) l.spent[k].insert(id);
        }
        for (std::size_t n = fired_out.size(); n-- > 0;) remove(fired_out[n]);

        if (read_content)
            for (const std::string& id : found_now.at(Sought{ Test::Contains, token }))
                if (forbidden_.insert(id).second) forbidden_changed = true;
        if (forbidden_changed)
            forbidden_published_.store(std::make_shared<const std::set<std::string>>(forbidden_));
        if (read_map) {
            prev_map_      = std::move(map_now_);      map_now_.clear();
            prev_map_view_ = std::move(map_view_now_); map_view_now_ = PoolView{};
            has_map_ = true;
        }
        if (read_content) {
            prev_content_      = std::move(content_now_);      content_now_.clear();
            prev_content_view_ = std::move(content_view_now_); content_view_now_ = PoolView{};
            has_content_ = true;
        }
        for (auto& [name, s] : flags_now_)  prev_flags_[name] = std::move(s);
        for (auto& [path, s] : files_now_)  prev_files_[path] = std::move(s);
        for (auto& [key,  s] : memory_now_) prev_memory_[key] = std::move(s);
        flags_now_.clear();
        files_now_.clear();
        memory_now_.clear();
    }
}

namespace {
Watcher the_one;
}

Watcher& watcher() {
    return the_one;
}

}
