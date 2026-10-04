// google_action.cpp — Google Workspace action, initiated by the agent that needs it

#include "google_action.h"
#include "engine_context.h"
#include "slot_registry.h"

#include <fstream>
#include <iterator>

namespace prime {

const char* google_status_name(GoogleStatus s) {
    switch (s) {
        case GoogleStatus::Ok:             return "ok";
        case GoogleStatus::Denied:         return "denied";
        case GoogleStatus::NoPipeline:     return "no_pipeline";
        case GoogleStatus::UnknownService: return "unknown_service";
        case GoogleStatus::MissingAction:  return "missing_action";
        case GoogleStatus::SetupRequired:  return "setup_required";
        case GoogleStatus::AuthRequired:   return "auth_required";
        case GoogleStatus::ClientNotBound: return "client_not_bound";
        default:                           return "unknown";
    }
}

namespace {

constexpr const char* kCredentialsPath = "/prime/google/credentials.json";
constexpr const char* kTokenPath       = "/prime/google/token.json";

bool file_exists(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return f.is_open();
}

bool is_known_service(const std::string& s) {
    return s == "gmail" || s == "drive" || s == "calendar"
        || s == "tasks" || s == "sheets";
}

enum class TokenState { Missing, Expired, Valid };

TokenState resolve_token_state(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return TokenState::Missing;
    const std::string contents{std::istreambuf_iterator<char>(f), {}};
    if (contents.empty()) return TokenState::Missing;

    return TokenState::Valid;
}

GoogleOutcome out(GoogleStatus s, std::string detail) {
    GoogleOutcome o;
    o.status = s;
    o.detail = std::move(detail);
    return o;
}

}

GoogleOutcome google_action(EngineContext&     engine,
                            const std::string& source_agent,
                            const std::string& service,
                            const std::string& action,
                            const std::string& data) {
    if (!engine.has_pipeline())
        return out(GoogleStatus::NoPipeline, "no pipeline resident");

    auto caller = engine.slots().by_name(source_agent);
    if (!caller || caller->agent.department != "Analyst")
        return out(GoogleStatus::Denied,
                   "Google Workspace is reachable only by resident "
                   "Analyst-department agents");

    if (service.empty() || !is_known_service(service))
        return out(GoogleStatus::UnknownService,
                   "unknown service; expected gmail, drive, calendar, tasks or sheets");

    if (action.empty())
        return out(GoogleStatus::MissingAction, "action is required");

    if (!file_exists(kCredentialsPath))
        return out(GoogleStatus::SetupRequired,
                   "credentials.json not found; OAuth client secrets must be "
                   "supplied before use");

    const TokenState tok = resolve_token_state(kTokenPath);
    if (tok == TokenState::Missing)
        return out(GoogleStatus::AuthRequired,
                   "no token present; interactive OAuth consent required");

    if (tok == TokenState::Expired) {

        return out(GoogleStatus::AuthRequired,
                   "token expired and no refresh path is bound; re-consent required");
    }

    (void)data;
    return out(GoogleStatus::ClientNotBound,
               "credentials resolved for " + service + "." + action +
               "; the Google client library is not bound and nothing was executed");
}

}
