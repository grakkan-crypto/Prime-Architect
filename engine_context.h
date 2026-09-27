// engine_context.h — Prime Engine long-lived runtime context
//
// One instance of this exists for the lifetime of the engine process. It owns
// the foundation singletons — the block allocator, the weight loader, the KV
// pool allocator, the pool maintenance layer, the project ingest, the LIVE
// registry, and the bound kernel backend — and hands references to the command
// and dispatch layers above it.
//
// DESIGN — nothing resident at startup
//   Per SPEC_PrimeEngine_v2_2 §7.2, the engine has nothing loaded when it comes
//   up. It constructs these owners empty and waits. The first pipeline-load
//   command from the frontend is what populates the weight store, compiles the
//   pool matrix, and creates the pools. Construction here allocates structure
//   only — no model is mapped and not one block of memory is charged until the
//   frontend acts.
//
// THE POOL TABLE IS NO LONGER HANDED IN
//   set_access_table() is gone. Nothing outside PoolMaintenance may install a
//   rule table, because a rule table arriving from elsewhere is by definition a
//   set of rules whose pools nobody guaranteed exist. access_table() now reports
//   what the maintenance layer actually has, rather than whatever a route
//   handler last happened to construct. Reading it still yields a snapshot copy,
//   unchanged, so no caller changes.
//
// This object makes no orchestration or policy decisions. It is the container
// the rest of the engine reaches through to get at the foundation.

#pragma once

#include "../foundation/memory_allocator.h"
#include "../foundation/gguf_parser.h"
#include "../foundation/kv_pool.h"
#include "../foundation/pool_compiler.h"
#include "pool_maintenance.h"
#include "project_ingest.h"
#include "rules.h"
#include "live_registry.h"
#include "dispatch.h"          // KernelBackend, StubKernelBackend

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace prime {

// Startup sizing and the frontend paths the engine reads.
//
// There is exactly ONE memory figure now. The arena reservation is gone with the
// arena: memory is no longer carved into per-pool windows, so there is nothing
// to reserve up front and no proportion to decide in advance. commit_ceiling is
// physical VRAM and is the only limit in the system.
//
// config_path and matrix_dir point into the frontend codebase on the shared
// filesystem — the frontend is the single source of truth for agent resolution
// and pool layout, and the engine reads those definitions by path rather than
// receiving them as payloads.
struct EngineConfig {
    uint64_t    commit_ceiling_bytes = 0; // physical VRAM — the one limit
    std::string config_path;              // stamped config.json (frontend-owned)
    std::string matrix_dir;               // dir the frontend emits <n>.matrix.json into
};

class EngineContext {
public:
    explicit EngineContext(const EngineConfig& cfg);
    ~EngineContext();

    EngineContext(const EngineContext&) = delete;
    EngineContext& operator=(const EngineContext&) = delete;

    // Foundation accessors. The command and dispatch layers reach the foundation
    // exclusively through these — they never construct their own.
    MemoryAllocator&  memory()   { return *allocator_; }
    GgufParser&       weights()  { return *weights_; }

    // The single resident answer to "what is currently true about this
    // pipeline" — agents, teams, split parents, temperatures, pipeline
    // identity. Reached directly: one process, so a read is a method call.
    // Replaces SlotRegistry, ConfigReader and the UI-side temperature store.
    LiveRegistry&     live()     { return live_; }
    const LiveRegistry& live() const { return live_; }

    // The pool allocator, for READING pools — addresses, stats, the tail. Its
    // lifecycle operations (commit, release, rename) are PoolMaintenance's alone;
    // calling them from anywhere else re-creates the drift the maintenance layer
    // exists to remove. See pool_maintenance.h.
    KvPoolAllocator&  pools()    { return *pools_; }

    // The single authority for any pool coming into or going out of existence.
    PoolMaintenance&  maintenance() { return *maintenance_; }

    // The project's residency — the code on disk, chunked and minted.
    ProjectIngest&    project()  { return *project_; }

    // The bound kernel backend — the seam Prime_Power implements. Until the
    // generator is wired, this is the StubKernelBackend, which answers every
    // dispatch with "kernel unavailable".
    KernelBackend&    kernel_backend() { return *kernel_backend_; }

    const EngineConfig& config() const { return cfg_; }

    // The compiled access table for the currently-loaded pipeline, as the
    // maintenance layer currently holds it. Empty until a pipeline is loaded.
    // A snapshot copy: minting and destroying pools moves underneath a caller,
    // so nothing is handed a reference into it.
    bool                       has_pipeline() const;
    std::optional<AccessTable> access_table() const;

    // Tear the whole pipeline down — pools, project residency, and the rules
    // that went with them.
    void clear_pipeline();

    // ---- Rules and Directive ----------------------------------------------
    //
    // ONE POOL, segmented: AI Rules unmasked to the whole fleet, then a Rules
    // segment and a Directive segment per named agent. Not fields an agent may
    // consult — pool segments, in front of the right agent on every parse.
    //
    // Access is fixed: an agent's Rules are read by that agent and whatever
    // validates it; an agent's Directive by that agent alone. Nothing else, ever.
    //
    // Reached directly. There is one process, so a read is a method call.
    Rules&       rules()       { return *rules_; }
    const Rules& rules() const { return *rules_; }

private:
    EngineConfig                     cfg_;

    // Construction order matters: the allocator owns the memory every pool draws
    // from, so it is built first and destroyed last. Maintenance is built after
    // the pool allocator it drives; project ingest after the maintenance it
    // calls.
    std::unique_ptr<MemoryAllocator> allocator_;
    std::unique_ptr<GgufParser>      weights_;
    std::unique_ptr<KvPoolAllocator> pools_;
    std::unique_ptr<PoolMaintenance> maintenance_;
    std::unique_ptr<ProjectIngest>   project_;
    LiveRegistry                     live_;

    std::unique_ptr<KernelBackend>   kernel_backend_;

    // Built in the constructor body, after the maintenance layer it drives, and
    // torn down before it. Never null once construction completes.
    std::unique_ptr<Rules>           rules_;
};

} // namespace prime
