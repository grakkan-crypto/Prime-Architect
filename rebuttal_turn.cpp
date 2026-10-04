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

        if (on) {
            const std::string id = IdGeneration::instance().mint_turn_id();
            host_.add_row(id);
        }
    });
}

void RebuttalTurn::interrupt() {

    const std::string id = IdGeneration::instance().mint_turn_id();
    host_.add_row(id);
}

void RebuttalTurn::reply(std::string text) {
    const std::string id = host_.current_id();
    if (id.empty()) return;

    chat_.add_user(text);

    host_.append_input(id, std::move(text));

    for (const auto& name : kv_.all_names()) {
        if (kv_.classification(name) != classification::kUserInput) continue;
        auto s = kv_.stamp(name);
        if (s.has_value() && s->turn_id == id) pools_.destroy(name);
    }

    host_.advance();
}

void RebuttalTurn::advance() {

    host_.advance();
}

void RebuttalTurn::dismissed_from_chat() {

    const std::string id = host_.current_id();
    if (id.empty()) return;
    host_.remove(id);
    resolve_close(id);
}

void RebuttalTurn::closed_from_ui(const std::string& turn_id) {

    if (turn_id.empty()) return;
    resolve_close(turn_id);
}

void RebuttalTurn::resolve_close(const std::string& turn_id) {

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
        if (!s.has_value() || s->turn_id != turn_id) continue;
        if (std::find(held.begin(), held.end(), s->pool_id) != held.end())
            continue;
        pools_.destroy(name);
    }

    if (host_.row_count() == 0) registry_.set_rebuttal_active(false);
}

}
