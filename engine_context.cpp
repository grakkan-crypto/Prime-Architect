// engine_context.cpp — Prime Engine long-lived runtime context

#include "engine_context.h"

namespace prime {

namespace {
// The banner every project block pool is minted under. It is the pipeline's
// vocabulary, matched verbatim against pools_<pipeline>.h's own declaration —
// not a name invented here.
constexpr const char* kProjectBanner = "Project";
} // namespace

EngineContext::EngineContext(const EngineConfig& cfg)
    : cfg_(cfg) {
    // Build the foundation in dependency order. The allocator charges nothing at
    // construction — it holds a ceiling and a granularity and no memory at all.
    // Nothing is mapped or charged until a pipeline load arrives.
    allocator_   = std::make_unique<MemoryAllocator>(cfg.commit_ceiling_bytes);
    weights_     = std::make_unique<GgufParser>();
    pools_       = std::make_unique<KvPoolAllocator>(*allocator_);
    maintenance_ = std::make_unique<PoolMaintenance>(*pools_);
    project_     = std::make_unique<ProjectIngest>(*maintenance_, kProjectBanner);

    // Rules is a real pool and goes through the same maintenance layer as every
    // other. Built after it, torn down before it.
    rules_       = std::make_unique<Rules>(*maintenance_);

    // Bind the placeholder kernel backend. Every dispatch lands on this until the
    // generator replaces it with the Prime_Power backend; the whole dispatch path
    // is exercisable end to end in the meantime.
    kernel_backend_ = std::make_unique<StubKernelBackend>();
}

EngineContext::~EngineContext() = default;

// ---------------------------------------------------------------------------
// Pipeline state — reported by the layer that actually holds it
// ---------------------------------------------------------------------------
bool EngineContext::has_pipeline() const {
    return maintenance_->loaded();
}

std::optional<AccessTable> EngineContext::access_table() const {
    return maintenance_->access_table();
}

void EngineContext::clear_pipeline() {
    // Rules first: it is a pool, and the maintenance layer is about to drop
    // everything it has. Taking it down through Rules rather than letting
    // unload_pipeline take it silently keeps that object's record of what exists
    // in step with the maintenance layer's.
    rules_->unload();

    // Project next: its blocks are pools too, and clearing the project after the
    // maintenance layer had dropped them would leave it holding source paths for
    // blocks that no longer exist.
    project_->clear();
    maintenance_->unload_pipeline();
}

} // namespace prime
