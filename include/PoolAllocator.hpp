#pragma once

#include "MemoryPool.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <mutex>


struct ThreadCache {
    void* m_head = nullptr; // Head of this thread's private free list.
    size_t m_count = 0;     // Number of blocks in this list.
};

class PoolAllocator {
public:
    /**
     * @brief Constructor: Initializes the lookup table and creates all the memory pools.
     */

    static constexpr std::array<size_t, 14> POOL_SIZES = {
        8, 16, 24, 32, 40, 48, 56, 64,
        96, 128, 192, 256, 384, 512
    };
    PoolAllocator() {
        // --- 1. Pre-calculate the size-to-pool lookup table ---
        // This is a one-time cost at startup that makes every future
        // allocate() call an O(1) operation.
        uint8_t poolIndex = 0;
        for (size_t size = 1; size <= POOL_SIZES.back(); ++size) {
            // If the current size has exceeded the capacity of the current pool,
            // we move to the next larger pool index.
            if (poolIndex < POOL_SIZES.size() - 1 && size > POOL_SIZES[poolIndex]) {
                poolIndex++;
            }
            m_size_to_pool_index[size] = poolIndex;
        }

        m_pools[0] = static_cast<IAllocator*>(new MemoryPool<8>());
        m_pools[1] = static_cast<IAllocator*>(new MemoryPool<16>());
        m_pools[2] = static_cast<IAllocator*>(new MemoryPool<24>());
        m_pools[3] = static_cast<IAllocator*>(new MemoryPool<32>());
        m_pools[4] = static_cast<IAllocator*>(new MemoryPool<40>());
        m_pools[5] = static_cast<IAllocator*>(new MemoryPool<48>());
        m_pools[6] = static_cast<IAllocator*>(new MemoryPool<56>());
        m_pools[7] = static_cast<IAllocator*>(new MemoryPool<64>());
        m_pools[8] = static_cast<IAllocator*>(new MemoryPool<96>());
        m_pools[9] = static_cast<IAllocator*>(new MemoryPool<128>());
        m_pools[10] = static_cast<IAllocator*>(new MemoryPool<192>());
        m_pools[11] = static_cast<IAllocator*>(new MemoryPool<256>());
        m_pools[12] = static_cast<IAllocator*>(new MemoryPool<384>());
        m_pools[13] = static_cast<IAllocator*>(new MemoryPool<512>());
    
    }
    // ... Destructor, allocate, and deallocate functions will go here ...
    ~PoolAllocator();
    void* allocate(size_t size);
    void deallocate(void* ptr);

private:
    union Header {
        uint8_t pool_index;   // Used when the block is allocated.
        Header* next_in_cache; // Used when the block is in a thread cache's free list.
    };

    void refill_cache(uint8_t poolIndex);
    std::array<std::mutex, POOL_SIZES.size()> m_pool_mutexes;
    std::array<IAllocator*, POOL_SIZES.size()> m_pools{};
    std::array<uint8_t, POOL_SIZES.back() + 1> m_size_to_pool_index{};
};

thread_local ThreadCache g_thread_caches[PoolAllocator::POOL_SIZES.size()];

void* PoolAllocator::allocate(size_t size) {
    if (size == 0) return nullptr;
    const size_t required_size = size + sizeof(Header);
    if (required_size > POOL_SIZES.back()) return nullptr;

    const uint8_t poolIndex = m_size_to_pool_index[required_size];
    ThreadCache& cache = g_thread_caches[poolIndex];

FastPath: // A label for our goto
    if (cache.m_head != nullptr) {
        Header* header = static_cast<Header*>(cache.m_head);
        cache.m_head = header->next_in_cache;
        cache.m_count--;
        header->pool_index = poolIndex;
        return static_cast<void*>(header + 1);
    }

    // Slow path: Refill the cache and try the fast path again.
    refill_cache(poolIndex);

    // If the refill added blocks, jump back to the fast path logic.
    if (cache.m_head != nullptr) {
        goto FastPath;
    }
    
    return nullptr; // Refill failed.
}

void PoolAllocator::deallocate(void* ptr) {
    if (ptr == nullptr) return;

    Header* header = static_cast<Header*>(ptr) - 1;
    const uint8_t poolIndex = header->pool_index;

    if (poolIndex >= POOL_SIZES.size()) {
        std::cerr << "ERROR: Invalid pool index!\n";
        return;
    }

    ThreadCache& cache = g_thread_caches[poolIndex];
    
    // The memory where pool_index was stored can now be used for the 'next' pointer.
    header->next_in_cache = static_cast<Header*>(cache.m_head);
    cache.m_head = header;
    cache.m_count++;
}

void PoolAllocator::refill_cache(uint8_t poolIndex) {
    // Lock only the mutex for the specific pool we need.
    std::lock_guard<std::mutex> lock(m_pool_mutexes[poolIndex]);

    ThreadCache& cache = g_thread_caches[poolIndex];
    
    // How many blocks to fetch.
    const size_t batch_size = 20;

    // Fetch a batch of blocks from the central pool.
    for (size_t i = 0; i < batch_size; ++i) {
        void* block = m_pools[poolIndex]->allocate();
        if (block == nullptr) break;
        
        // Push the new block onto the thread's cache.
        Header* header = static_cast<Header*>(block);
        header->next_in_cache = static_cast<Header*>(cache.m_head);
        cache.m_head = header;
        cache.m_count++;
    }
}

PoolAllocator::~PoolAllocator() {
    // std::cout << "Destroying PoolAllocator and freeing all pools." << std::endl;
    for (auto* pool : m_pools) {
        delete pool;
    }
}