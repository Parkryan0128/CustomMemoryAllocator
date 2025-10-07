#pragma once

#include "MemoryPool.hpp"
#include <array>
#include <cstdint>
#include <mutex>
#include <tuple>

constexpr size_t CACHELINE_SIZE = 64; // Use a conservative 64-byte cache line size.

struct alignas(CACHELINE_SIZE) PaddedMutex {
    std::mutex m;
    char padding[CACHELINE_SIZE - sizeof(std::mutex)];
};

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
        uint8_t poolIndex = 0;
        for (size_t size = 1; size <= POOL_SIZES.back(); ++size) {
            if (poolIndex < POOL_SIZES.size() - 1 && size > POOL_SIZES[poolIndex]) {
                poolIndex++;
            }
            m_size_to_pool_index[size] = poolIndex;
        }

        // m_pools[0] = new MemoryPool<8>();
        // m_pools[1] = new MemoryPool<16>();
        // m_pools[2] = new MemoryPool<24>();
        // m_pools[3] = new MemoryPool<32>();
        // m_pools[4] = new MemoryPool<40>();
        // m_pools[5] = new MemoryPool<48>();
        // m_pools[6] = new MemoryPool<56>();
        // m_pools[7] = new MemoryPool<64>();
        // m_pools[8] = new MemoryPool<96>();
        // m_pools[9] = new MemoryPool<128>();
        // m_pools[10] = new MemoryPool<192>();
        // m_pools[11] = new MemoryPool<256>();
        // m_pools[12] = new MemoryPool<384>();
        // m_pools[13] = new MemoryPool<512>();
    
    }
    ~PoolAllocator();
    void* allocate(size_t size);
    void deallocate(void* ptr);

private:
    union Header {
        uint8_t pool_index;   // Used when the block is allocated.
        Header* next_in_cache; // Used when the block is in a thread cache's free list.
    };
    static constexpr size_t BATCH_SIZE = 64;

    static constexpr size_t HIGH_WATER_MARK = 2 * BATCH_SIZE;

    void refill_cache(uint8_t poolIndex);
    std::array<PaddedMutex, POOL_SIZES.size()> m_pool_mutexes;

    // std::array<IAllocator*, POOL_SIZES.size()> m_pools{};
    std::tuple<
        MemoryPool<8>, MemoryPool<16>, MemoryPool<24>, MemoryPool<32>,
        MemoryPool<40>, MemoryPool<48>, MemoryPool<56>, MemoryPool<64>,
        MemoryPool<96>, MemoryPool<128>, MemoryPool<192>, MemoryPool<256>,
        MemoryPool<384>, MemoryPool<512>
    > m_pools;
    std::array<uint8_t, POOL_SIZES.back() + 1> m_size_to_pool_index{};
};

thread_local ThreadCache g_thread_caches[PoolAllocator::POOL_SIZES.size()];

void* PoolAllocator::allocate(size_t size) {
    if (size == 0) return nullptr;
    const size_t required_size = size + sizeof(Header);
    if (required_size > POOL_SIZES.back()) return nullptr;

    const uint8_t poolIndex = m_size_to_pool_index[required_size];
    ThreadCache& cache = g_thread_caches[poolIndex];

    if (cache.m_head == nullptr) {
        refill_cache(poolIndex);
    }

    if (cache.m_head != nullptr) {
        Header* header = static_cast<Header*>(cache.m_head);
        cache.m_head = header->next_in_cache;
        cache.m_count--;
        header->pool_index = poolIndex;
        return static_cast<void*>(header + 1);
    }
    
    return nullptr;
}

void PoolAllocator::deallocate(void* ptr) {
    if (ptr == nullptr) return;

    Header* header = static_cast<Header*>(ptr) - 1;
    const uint8_t poolIndex = header->pool_index;

    if (poolIndex >= POOL_SIZES.size()) {
        return;
    }

    ThreadCache& cache = g_thread_caches[poolIndex];
    
    header->next_in_cache = static_cast<Header*>(cache.m_head);
    cache.m_head = header;
    cache.m_count++;

    if (cache.m_count > HIGH_WATER_MARK) {
        std::lock_guard<std::mutex> lock(m_pool_mutexes[poolIndex].m);

        Header* return_list_head = static_cast<Header*>(cache.m_head);
        Header* keep_list_head = return_list_head;
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            keep_list_head = keep_list_head->next_in_cache;
        }
        cache.m_head = keep_list_head;
        
        Header* current = return_list_head;
        while (current != keep_list_head) {
            Header* next = current->next_in_cache;
            switch (poolIndex) {
                case 0:  std::get<0>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 1:  std::get<1>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 2:  std::get<2>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 3:  std::get<3>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 4:  std::get<4>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 5:  std::get<5>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 6:  std::get<6>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 7:  std::get<7>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 8:  std::get<8>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 9:  std::get<9>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 10:  std::get<10>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 11:  std::get<11>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 12:  std::get<12>(m_pools).deallocate(static_cast<void*>(current)); break;
                case 13: std::get<13>(m_pools).deallocate(static_cast<void*>(current)); break;
            }
            current = next;
        }

        cache.m_count -= BATCH_SIZE;
    }
}

void PoolAllocator::refill_cache(uint8_t poolIndex) {
    std::lock_guard<std::mutex> lock(m_pool_mutexes[poolIndex].m);
    ThreadCache& cache = g_thread_caches[poolIndex];
    
    for (size_t i = 0; i < BATCH_SIZE; ++i) {
        void* block = nullptr;
        switch (poolIndex) {
            case 0:  block = std::get<0>(m_pools).allocate(); break;
            case 1:  block = std::get<1>(m_pools).allocate(); break;
            case 2:  block = std::get<2>(m_pools).allocate(); break;
            case 3:  block = std::get<3>(m_pools).allocate(); break;
            case 4:  block = std::get<4>(m_pools).allocate(); break;
            case 5:  block = std::get<5>(m_pools).allocate(); break;
            case 6:  block = std::get<6>(m_pools).allocate(); break;
            case 7:  block = std::get<7>(m_pools).allocate(); break;
            case 8:  block = std::get<8>(m_pools).allocate(); break;
            case 9:  block = std::get<9>(m_pools).allocate(); break;
            case 10:  block = std::get<10>(m_pools).allocate(); break;
            case 11:  block = std::get<11>(m_pools).allocate(); break;
            case 12:  block = std::get<12>(m_pools).allocate(); break;
            case 13: block = std::get<13>(m_pools).allocate(); break;
        }

        if (block == nullptr) {
            break;
        }
        
        Header* header = static_cast<Header*>(block);
        header->next_in_cache = static_cast<Header*>(cache.m_head);
        cache.m_head = header;
        cache.m_count++;
    }
}


PoolAllocator::~PoolAllocator() {
    // for (auto* pool : m_pools) {
    //     delete pool;
    // }
}

