// routes.cpp — Prime Engine command registration and core status command
//
// register_routes() wires the command table. This file implements the engine
// status command in full and calls each group's register_*_routes() helper to
// add its commands. The engine core holds no knowledge of specific commands — it
// dispatches a table it was handed.
//
// Command inventory (SPEC_PrimeEngine_v2_2 §10.3, §10.5, §10.6, §8.3):
//   EngineStatus            — implemented here
//   PipelineLoad/Unload     — pipeline routes
//   RequestWritePermission / ExecuteWrite / CancelWrite — dev routes
//   GetRules / PostRules / PinRules                     — dev routes
//   GetAgents                                           — dev routes
//   AetherFetch                                         — dev routes (streaming)
//   Google                                              — google routes

#include "routes.h"
#include "command_types.h"
#include "engine_context.h"
#include "pipeline_routes.h"
#include "dev_routes.h"
#include "google_routes.h"

#include <string>

namespace prime {

namespace {

// EngineStatus — engine health, loaded pipeline, memory state. Needs nothing but
// the engine context, which makes it the natural first command: it exercises the
// full path (decode -> dispatch -> unary result) without any model loaded.
Result handle_engine_status(EngineContext& engine, const Command&) {
    const bool     loaded    = engine.has_pipeline();
    const uint64_t committed = engine.memory().total_committed();
    const auto&    cfg       = engine.config();

    std::string handle = "null";
    if (loaded) {
        if (auto table = engine.access_table())
            handle = std::to_string(table->handle_id());
    }

    std::string body;
    body += "{";
    body += "\"status\":\"ready\",";
    body += "\"pipeline_loaded\":" + std::string(loaded ? "true" : "false") + ",";
    body += "\"pipeline_handle\":" + handle + ",";
    body += "\"committed_bytes\":" + std::to_string(committed) + ",";
    body += "\"commit_ceiling_bytes\":" + std::to_string(cfg.commit_ceiling_bytes) + ",";
    body += "\"arena_reserve_bytes\":" + std::to_string(cfg.arena_reserve_bytes);
    body += "}";

    return Result::ok(std::move(body));
}

} // namespace

void register_routes(CommandTable& table, EngineContext& /*engine*/) {
    // ---- Core ----
    table.add_unary(CommandId::EngineStatus, handle_engine_status);

    // ---- Pipeline ----
    register_pipeline_routes(table);   // PipelineLoad, PipelineUnload

    // ---- HITL write, rules, agents ----
    register_dev_routes(table);        // write cycle, rules, agents

    // ---- Google Workspace ----
    register_google_routes(table);     // Google
}

} // namespace prime
