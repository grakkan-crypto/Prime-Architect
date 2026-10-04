// memory_allocator.h — Prime Engine block allocator
//
// Hands out fixed-size blocks of real memory and takes them back. That is its
// entire job. It knows nothing about tokens, pools, models or agents.
//
// WHY THE ARENA IS GONE (this replaces the reserve-a-window model)
//   The previous design reserved one large contiguous arena at startup and
//   carved every pool a permanent, non-overlapping window out of it. Because a
//   window could never move or overlap, its extent had to be decided the moment
//   the pool was born and could never change. That is where per-pool ceilings
//   came from: not from the hardware, and not from anything the architecture
//   asked for, but purely as a consequence of demanding that a pool be one
//   unbroken run of addresses.
//
//   The ruling is that no pool has a ceiling and the only real limit is physical
//   VRAM. That is incompatible with pre-carved windows, so the arena is removed
//   rather than worked around. Memory is now an undifferentiated supply of
//   blocks. A pool takes blocks as content lands and hands them straight back
//   when it shrinks or dies, at which point they are immediately available to
//   any other pool. Nothing is claimed in advance and nothing is stranded, so
//   one pool genuinely can grow to occupy the great majority of VRAM if that is
//   what its content requires — no other pool has a prior claim standing in the
//   way.
//
// THE ONE LIMIT
//   commit_ceiling is the single global cap on live bytes, set to physical VRAM
//   at launch. It is checked here, in one place, on every acquisition. There is
//   no second limit anywhere in the system, and no component below or above this
//   one is entitled to invent one.
//
// BLOCK SIZE IS THE CALLER'S, NOT OURS
//   acquire() takes the size it is asked for rather than imposing a house
//   figure, because the right block size depends on the caller's bytes-per-token
//   — a concern one layer up. Sizes are rounded up to the OS allocation
//   granularity, which is a hardware fact, not a policy choice.
//
// NO BLOCK CACHE
//   release() returns memory to the OS immediately rather than parking it on a
//   free list. A cached block is still physically committed, so caching would
//   mean either charging it against the ceiling (making the ceiling lie about
//   what is in use) or not charging it (making the ceiling lie about what is
//   committed). Neither is acceptable, so committed_total is exact: it is the
//   bytes currently held by live pools and nothing else. If block churn ever
//   shows up as a real cost under profiling, a cache can be added with its
//   accounting reasoned through properly — it is not being pre-empted here on a
//   guess.

#pragma once

#include <cstdint>
#include <mutex>

namespace prime {

class MemoryAllocator {
public:

    explicit MemoryAllocator(uint64_t commit_ceiling);
    ~MemoryAllocator();

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;

    uint8_t* acquire(uint64_t block_size);

    void release(uint8_t* block, uint64_t block_size);

    uint64_t total_committed() const;

    uint64_t commit_ceiling() const { return commit_ceiling_; }

    uint64_t granularity() const { return granularity_; }

    uint64_t round_up_to_granularity(uint64_t bytes) const;

private:
    uint64_t           granularity_     = 0;
    uint64_t           commit_ceiling_  = 0;
    uint64_t           committed_total_ = 0;
    mutable std::mutex mutex_;
};

}
