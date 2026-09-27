// memory_allocator.cpp — Prime Engine block allocator implementation

#include "memory_allocator.h"

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
#endif

namespace prime {

namespace {

// The OS's allocation granularity. On Windows this is dwAllocationGranularity
// (64KB on every current target); elsewhere it is the page size. Asking the OS
// rather than hardcoding it is deliberate — this is the one number in the
// allocator that is a hardware fact rather than a decision.
uint64_t os_granularity() {
#if defined(_WIN32)
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return static_cast<uint64_t>(si.dwAllocationGranularity);
#else
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? static_cast<uint64_t>(p) : 4096ull;
#endif
}

uint8_t* os_acquire(uint64_t bytes) {
#if defined(_WIN32)
    void* p = VirtualAlloc(nullptr, static_cast<SIZE_T>(bytes),
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return static_cast<uint8_t*>(p);
#else
    void* p = mmap(nullptr, static_cast<size_t>(bytes),
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
#endif
}

void os_release(uint8_t* p, uint64_t bytes) {
#if defined(_WIN32)
    (void)bytes; // MEM_RELEASE requires size 0 and frees the whole reservation
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, static_cast<size_t>(bytes));
#endif
}

} // namespace

MemoryAllocator::MemoryAllocator(uint64_t commit_ceiling)
    : granularity_(os_granularity()),
      commit_ceiling_(commit_ceiling) {}

MemoryAllocator::~MemoryAllocator() = default;

uint64_t MemoryAllocator::round_up_to_granularity(uint64_t bytes) const {
    if (bytes == 0) return granularity_;
    const uint64_t g = granularity_;
    return ((bytes + g - 1) / g) * g;
}

uint8_t* MemoryAllocator::acquire(uint64_t block_size) {
    if (block_size == 0) return nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // The one limit in the system, checked in the one place it lives. A
        // caller past the ceiling has run out of VRAM; it is told so plainly
        // rather than handed something smaller than it asked for.
        if (committed_total_ + block_size > commit_ceiling_) return nullptr;
        committed_total_ += block_size;
    }

    uint8_t* p = os_acquire(block_size);
    if (p == nullptr) {
        // The OS refused despite the ceiling allowing it. Unwind the charge so
        // the accounting stays exact — a failed acquisition must leave no trace.
        std::lock_guard<std::mutex> lock(mutex_);
        committed_total_ -= block_size;
        return nullptr;
    }
    return p;
}

void MemoryAllocator::release(uint8_t* block, uint64_t block_size) {
    if (block == nullptr || block_size == 0) return;

    os_release(block, block_size);

    std::lock_guard<std::mutex> lock(mutex_);
    // Uncharged immediately, not on some later sweep: the whole point of the
    // block model is that memory a pool has finished with is available to the
    // next pool that asks, right now.
    committed_total_ -= block_size;
}

uint64_t MemoryAllocator::total_committed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return committed_total_;
}

} // namespace prime
