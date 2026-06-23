#pragma once

#include "FixedBlockAllocator.hpp"
#include "test_runner.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

namespace cma_test {

inline constexpr size_t kBlockSize = 32;
using Allocator = cma::FixedBlockAllocator<kBlockSize>;

template <size_t BlockSize = kBlockSize, typename AllocatorType = cma::FixedBlockAllocator<BlockSize>>
std::vector<void*> allocate_blocks(AllocatorType& allocator, size_t count) {
    std::vector<void*> blocks;
    blocks.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        blocks.push_back(allocator.allocate());
    }
    return blocks;
}

template <size_t BlockSize = kBlockSize, typename AllocatorType = cma::FixedBlockAllocator<BlockSize>>
void deallocate_blocks(AllocatorType& allocator, const std::vector<void*>& blocks) {
    for (void* block : blocks) {
        allocator.deallocate(block);
    }
}

template <size_t BlockSize = kBlockSize, typename AllocatorType = cma::FixedBlockAllocator<BlockSize>>
void expect_stats_consistent(const AllocatorType& allocator) {
    const typename cma::FixedBlockAllocator<BlockSize>::Stats snapshot = allocator.stats();

    if (snapshot.active_pages == 0) {
        if (snapshot.live_blocks != 0 || snapshot.capacity_blocks != 0 || snapshot.mapped_bytes != 0 ||
            snapshot.live_bytes != 0 || snapshot.free_blocks != 0) {
            throw TestFailure("Expected zeroed stats when no pages are mapped");
        }
        return;
    }

    if (snapshot.live_blocks + snapshot.free_blocks != snapshot.capacity_blocks) {
        throw TestFailure("Expected live + free to equal capacity");
    }

    if (snapshot.mapped_bytes != snapshot.active_pages * cma::FixedBlockAllocator<BlockSize>::PAGE_SIZE) {
        throw TestFailure("Expected mapped_bytes to match active pages");
    }

    if (snapshot.live_bytes != snapshot.live_blocks * BlockSize) {
        throw TestFailure("Expected live_bytes to match live blocks");
    }

    if (snapshot.free_bytes != snapshot.free_blocks * BlockSize) {
        throw TestFailure("Expected free_bytes to match free blocks");
    }
}

inline void flush_thread_cache(cma::FixedBlockAllocator<kBlockSize>& allocator) {
    allocator.flush_local_thread_cache();
}

template <size_t BlockSize>
void flush_thread_cache(cma::FixedBlockAllocator<BlockSize>& allocator) {
    allocator.flush_local_thread_cache();
}

class PhaseBarrier {
public:
    explicit PhaseBarrier(unsigned int participant_count)
        : participant_count_(participant_count), generation_(0), waiting_(0) {}

    void arrive_and_wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        const unsigned int generation = generation_;
        if (++waiting_ == participant_count_) {
            waiting_ = 0;
            ++generation_;
            cv_.notify_all();
            return;
        }
        cv_.wait(lock, [&] { return generation != generation_; });
    }

private:
    const unsigned int participant_count_;
    unsigned int generation_;
    unsigned int waiting_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace cma_test
