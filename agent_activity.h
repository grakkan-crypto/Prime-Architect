// agent_activity.h — Which agents are currently working.
//
// Each agent has its own on/off. Nothing is shared or pooled across agents.
//
// SCOPED BY ROSTER
//   Every question here takes the list of agents you care about — normally the
//   currently loaded pipeline's roster. Agents outside that list are never
//   looked at, so a background task or a second loaded pipeline can't make this
//   pipeline's bar light up, and Abort can't reach past its own agents to stop
//   them.
//
// Nothing sets these yet. Whatever ends up doing the work calls set_busy() when
// it starts and again when it stops.

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace prime::frontend {

class AgentActivity {
public:
    void set_busy(const std::string& agent, bool busy) {
        std::lock_guard<std::mutex> lock(mutex_);
        busy_[agent] = busy;
    }

    bool any_busy(const std::vector<std::string>& agents) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& agent : agents) {
            auto it = busy_.find(agent);
            if (it != busy_.end() && it->second) return true;
        }
        return false;
    }

    void clear(const std::vector<std::string>& agents) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& agent : agents) busy_[agent] = false;
    }

private:
    mutable std::mutex                      mutex_;
    std::unordered_map<std::string, bool>   busy_;
};

}
