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
#include "dispatch.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace prime {

struct EngineConfig {
    uint64_t    commit_ceiling_bytes = 0;
    std::string config_path;
    std::string matrix_dir;
};

class EngineContext {
public:
    explicit EngineContext(const EngineConfig& cfg);
    ~EngineContext();

    EngineContext(const EngineContext&) = delete;
    EngineContext& operator=(const EngineContext&) = delete;

    MemoryAllocator&  memory()   { return *allocator_; }
    GgufParser&       weights()  { return *weights_; }

    LiveRegistry&     live()     { return live_; }
    const LiveRegistry& live() const { return live_; }

    KvPoolAllocator&  pools()    { return *pools_; }

    PoolMaintenance&  maintenance() { return *maintenance_; }

    ProjectIngest&    project()  { return *project_; }

    KernelBackend&    kernel_backend() { return *kernel_backend_; }

    const EngineConfig& config() const { return cfg_; }

    bool                       has_pipeline() const;
    std::optional<AccessTable> access_table() const;

    void clear_pipeline();

    Rules&       rules()       { return *rules_; }
    const Rules& rules() const { return *rules_; }

private:
    EngineConfig                     cfg_;

    std::unique_ptr<MemoryAllocator> allocator_;
    std::unique_ptr<GgufParser>      weights_;
    std::unique_ptr<KvPoolAllocator> pools_;
    std::unique_ptr<PoolMaintenance> maintenance_;
    std::unique_ptr<ProjectIngest>   project_;
    LiveRegistry                     live_;

    std::unique_ptr<KernelBackend>   kernel_backend_;

    std::unique_ptr<Rules>           rules_;
};

}
