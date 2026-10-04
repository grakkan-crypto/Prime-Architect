// aether_fetch.cpp — Quarantined external fetch, initiated by the agent that needs it

#include "aether_fetch.h"
#include "engine_context.h"
#include "slot_registry.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>

namespace prime {

const char* aether_status_name(AetherStatus s) {
    switch (s) {
        case AetherStatus::Ready:           return "ready";
        case AetherStatus::Denied:          return "denied";
        case AetherStatus::NoPipeline:      return "no_pipeline";
        case AetherStatus::NoAetherSlot:    return "no_aether_slot";
        case AetherStatus::InvalidUrl:      return "invalid_url";
        case AetherStatus::NetworkNotBound: return "network_not_bound";
        case AetherStatus::SanitiseFailed:  return "sanitise_failed";
        default:                            return "unknown";
    }
}

namespace {

std::mutex  s_audit_mutex;
std::string s_audit_log_path = "/prime/auxiliary/watchdog/aether_audit.log";

std::string iso_timestamp() {
    const auto now = std::chrono::system_clock::now();
    std::time_t t  = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

void audit(const std::string& source_agent, const std::string& url,
           AetherStatus status) {
    std::lock_guard<std::mutex> lk(s_audit_mutex);
    std::ofstream f(s_audit_log_path, std::ios::app);
    if (!f.is_open()) return;
    f << iso_timestamp() << '\t'
      << source_agent    << '\t'
      << aether_status_name(status) << '\t'
      << url << '\n';
}

AetherOutcome done(const std::string& source_agent, const std::string& url,
                   AetherStatus s, std::string detail) {
    audit(source_agent, url, s);
    AetherOutcome o;
    o.status = s;
    o.detail = std::move(detail);
    return o;
}

}

AetherOutcome aether_fetch(EngineContext&     engine,
                           const std::string& source_agent,
                           const std::string& url) {
    if (!engine.has_pipeline())
        return done(source_agent, url, AetherStatus::NoPipeline,
                    "no pipeline resident");

    auto caller = engine.slots().by_name(source_agent);
    if (!caller || caller->agent.department != "Analyst")
        return done(source_agent, url, AetherStatus::Denied,
                    "Aether is reachable only by resident Analyst-department agents");

    if (url.empty())
        return done(source_agent, url, AetherStatus::InvalidUrl, "url is empty");

    auto aether = engine.slots().by_name("Aether");
    if (!aether)
        return done(source_agent, url, AetherStatus::NoAetherSlot,
                    "this pipeline has no resident Aether agent to sanitise with");

    (void)aether;
    return done(source_agent, url, AetherStatus::NetworkNotBound,
                "Prime OS network layer is not bound; no fetch was attempted "
                "and no content was produced");
}

}
