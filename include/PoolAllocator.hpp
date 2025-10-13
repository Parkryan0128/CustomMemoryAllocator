#pragma once

#include "MemoryPool.hpp"
#include <array>
#include <cstdint>
#include <mutex>

struct ThreadCache {
    void* m_head = nullptr;
    size_t m_count = 0;
};

class PoolAllocator {
public:
    static constexpr std::array<size_t, 14> POOL_SIZES = {
        8, 16, 24, 32, 40, 48, 56, 64,
        96, 128, 192, 256, 384, 512
    };

    PoolAllocator();
    ~PoolAllocator();

    // Disable copy and move semantics
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    void* allocate(size_t size);
    void deallocate(void* ptr);

private:
    union Header {
        uint8_t pool_index;
        Header* next_in_cache;
    };

    void refill_cache(uint8_t poolIndex);

    std::array<std::mutex, POOL_SIZES.size()> m_pool_mutexes;
    std::array<IAllocator*, POOL_SIZES.size()> m_pools{};
    std::array<uint8_t, POOL_SIZES.back() + 1> m_size_to_pool_index{};
};