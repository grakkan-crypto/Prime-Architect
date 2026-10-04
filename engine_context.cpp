// engine_context.cpp — Prime Engine long-lived runtime context

#include "engine_context.h"

namespace prime {

namespace {

constexpr const char* kProjectBanner = "Project";
}

EngineContext::EngineContext(const EngineConfig& cfg)
    : cfg_(cfg) {

    allocator_   = std::make_unique<MemoryAllocator>(cfg.commit_ceiling_bytes);
    weights_     = std::make_unique<GgufParser>();
    pools_       = std::make_unique<KvPoolAllocator>(*allocator_);
    maintenance_ = std::make_unique<PoolMaintenance>(*pools_);
    project_     = std::make_unique<ProjectIngest>(*maintenance_, kProjectBanner);

    rules_       = std::make_unique<Rules>(*maintenance_);

    kernel_backend_ = std::make_unique<StubKernelBackend>();
}

EngineContext::~EngineContext() = default;

bool EngineContext::has_pipeline() const {
    return maintenance_->loaded();
}

std::optional<AccessTable> EngineContext::access_table() const {
    return maintenance_->access_table();
}

void EngineContext::clear_pipeline() {

    rules_->unload();

    project_->clear();
    maintenance_->unload_pipeline();
}

}
