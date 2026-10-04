// turn.cpp — Turn: ordinary turn lifecycle implementation

#include "turn.h"

#include "id_generation.h"
#include "live_registry.h"

#include <algorithm>

namespace prime {

namespace {

    const char* const kAuditorOutput = "Auditor_Output";
    const char* const kAnalystOutput = "Analyst_Output";
}

Turn::Turn(KvPoolAllocator& kv, PoolManipulation& pools, LiveRegistry& registry)
    : kv_(kv), pools_(pools), registry_(registry) {
    registry_.on_rebuttal_switch([this](bool active) {
        rebuttal_active_ = active;

        if (!active) winddown_.clear();
    });
}

std::string Turn::open() {
    return IdGeneration::instance().mint_turn_id();
}

void Turn::interrupt() {

    if (rebuttal_active_) return;

    const bool busy = false;

    if (busy) {

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

    registry_.set_rebuttal_active(true);
}

void Turn::watch() {
    for (const auto& name : kv_.all_names()) {
        auto s = kv_.stamp(name);
        if (!s.has_value() || !s->generation_complete) continue;

        if (rebuttal_active_ &&
            std::find(winddown_.begin(), winddown_.end(), s->turn_id) ==
                winddown_.end())
            continue;

        if (name == kAuditorOutput) {
            kv_.reserve(kAnalystOutput);
            continue;
        }

        if (kv_.classification(name) == classification::kUserOutput &&
            !s->turn_id.empty()) {
            sc_teardown(s->turn_id);
            auto it = std::find(winddown_.begin(), winddown_.end(), s->turn_id);
            if (it != winddown_.end()) winddown_.erase(it);
        }
    }
}

void Turn::reject(const std::string& turn_id) {
    sc_teardown(turn_id);
    pools_.destroy(kAnalystOutput);
}

void Turn::write(const std::string& turn_id) {

    sc_teardown(turn_id);
    pools_.destroy(kAnalystOutput);
}

void Turn::edit(const std::string& turn_id) {
    sc_teardown(turn_id);

    pools_.reclassify(kAnalystOutput, classification::kSharedContext);
}

void Turn::sc_teardown(const std::string& closing_turn_id) {
    const uint64_t closing = ordinal_of(closing_turn_id);

    std::vector<std::string> held;
    for (const auto& [open_id, controller] : registry_.mask_open_ids()) {
        (void)controller;
        for (const auto& pool_id : registry_.linked_pools(open_id))
            held.push_back(pool_id);
    }

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

}
