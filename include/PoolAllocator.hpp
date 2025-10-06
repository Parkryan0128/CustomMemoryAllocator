#pragma once

#include "MemoryPool.hpp" // Contains IAllocator and the MemoryPool template class
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

        m_pools[0] = new MemoryPool<8>();
        m_pools[1] = new MemoryPool<16>();
        m_pools[2] = new MemoryPool<24>();
        m_pools[3] = new MemoryPool<32>();
        m_pools[4] = new MemoryPool<40>();
        m_pools[5] = new MemoryPool<48>();
        m_pools[6] = new MemoryPool<56>();
        m_pools[7] = new MemoryPool<64>();
        m_pools[8] = new MemoryPool<96>();
        m_pools[9] = new MemoryPool<128>();
        m_pools[10] = new MemoryPool<192>();
        m_pools[11] = new MemoryPool<256>();
        m_pools[12] = new MemoryPool<384>();
        m_pools[13] = new MemoryPool<512>();
    
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


    // void* refill_cache_and_allocate(uint8_t poolIndex, size_t size);
     void refill_cache(uint8_t poolIndex);
    // std::mutex m_mutex; // A single lock for the central allocator
        std::array<std::mutex, POOL_SIZES.size()> m_pool_mutexes;
    std::array<IAllocator*, POOL_SIZES.size()> m_pools{};
    std::array<uint8_t, POOL_SIZES.back() + 1> m_size_to_pool_index{};
};

thread_local ThreadCache g_thread_caches[PoolAllocator::POOL_SIZES.size()];

// void* PoolAllocator::allocate(size_t size) {
//     if (size == 0) {
//         return nullptr;
//     }

//     // 2. Calculate the total size needed: the user's request + our 1-byte header.
//     const size_t total_size = size + sizeof(Header);

//     // 3. Handle large allocations: If the request is too big for our largest pool, fail.
//     if (total_size > POOL_SIZES.back()) {
//         return nullptr; // In a more advanced version, this could fall back to system malloc.
//     }
    
//     // 4. Find the correct pool to use with an O(1) lookup in our pre-calculated table.
//     const uint8_t poolIndex = m_size_to_pool_index[total_size];


//     ThreadCache& cache = g_thread_caches[poolIndex];
//     if (cache.m_head != nullptr) {
//         void* ptr = cache.m_head;
//         cache.m_head = *static_cast<void**>(ptr);
//         cache.m_count--;
//         return static_cast<void*>(static_cast<Header*>(ptr) + 1);
//     }


//     return refill_cache_and_allocate(poolIndex, size);

    
//     // // 5. Request a raw, empty block from the appropriate specialist pool.
//     // void* raw_block = m_pools[poolIndex]->allocate();

//     // // If the pool was exhausted and failed to grow, it will return nullptr.
//     // if (raw_block == nullptr) {
//     //     return nullptr;
//     // }

//     // // 6. "Stamp" the block: Write our 1-byte header into the very beginning of the raw block.
//     // Header* header = static_cast<Header*>(raw_block);
//     // header->pool_index = poolIndex;
    
//     // // 7. Return the offset pointer: Give the user a pointer to the memory *after* our header.
//     // return static_cast<void*>(header + 1);
// }

// void* PoolAllocator::allocate(size_t size) {
//     if (size == 0) return nullptr;
//     const size_t required_size = size + sizeof(Header);
//     if (required_size > POOL_SIZES.back()) return nullptr;

//     const uint8_t poolIndex = m_size_to_pool_index[required_size];
//     ThreadCache& cache = g_thread_caches[poolIndex];

//     // --- FAST PATH ---
//     if (cache.m_head != nullptr) {
//         void* raw_block = cache.m_head;
//         cache.m_head = *static_cast<void**>(raw_block);
//         cache.m_count--;
        
//         Header* header = static_cast<Header*>(raw_block);
//         header->pool_index = poolIndex;
//         return static_cast<void*>(header + 1);
//     }

//     // --- SLOW PATH ---
//     return refill_cache_and_allocate(poolIndex, size);
// }



// void PoolAllocator::deallocate(void* ptr) {
//     // 1. Safety check.
//     if (ptr == nullptr) {
//         return;
//     }

//     // 2. Find the header and read the pool index, just as you did.
//     Header* header = static_cast<Header*>(ptr) - 1;
//     const uint8_t poolIndex = header->pool_index;

//     // 3. Safety check for the index.
//     if (poolIndex >= POOL_SIZES.size()) {
//         std::cerr << "ERROR: Invalid pool index on deallocate! Memory corruption likely.\n";
//         return;
//     }

//     // 4. Get this thread's private cache for that pool index.
//     ThreadCache& cache = g_thread_caches[poolIndex];

//     // 5. Push the block onto the front of this thread's private free list.
//     // This is the fast, lock-free operation.
//     // *static_cast<void**>((void*) header) = cache.m_head;
//     *reinterpret_cast<void**>(header) = cache.m_head;
//     cache.m_head = header;
//     cache.m_count++;
    
//     // The call to m_pools[poolIndex]->deallocate(header) is now correctly removed.
// }


// void* PoolAllocator::refill_cache_and_allocate(uint8_t poolIndex, size_t size) {
//     // 1. This is the only place we need to lock.
//     std::lock_guard<std::mutex> lock(m_mutex);

//     ThreadCache& cache = g_thread_caches[poolIndex];

//     // 2. Fetch a batch of blocks from the central pool.
//     const size_t batch_size = 20; // This can be tuned.
//     void* batch_head = nullptr;
//     void* batch_tail = nullptr;
//     size_t fetched_count = 0;

//     for (size_t i = 0; i < batch_size; ++i) {
//         void* block = m_pools[poolIndex]->allocate();
//         if (block == nullptr) {
//             break; // Central pool is exhausted.
//         }
        
//         // Form a local linked list of the fetched blocks
//         *static_cast<void**>(block) = batch_head;
//         batch_head = block;
//         if (batch_tail == nullptr) {
//             batch_tail = block;
//         }
//         fetched_count++;
//     }

//     // If we failed to get any blocks, the central pool is empty.
//     if (fetched_count == 0) {
//         return nullptr;
//     }

//     // 3. Return ONE block to the user to satisfy the initial request.
//     void* block_to_return = batch_head;
//     batch_head = *static_cast<void**>(block_to_return);
//     fetched_count--;

//     // 4. Place the REST of the batch into the thread's cache.
//     if (fetched_count > 0) {
//         // The tail of our new batch should point to the old head of the cache
//         *static_cast<void**>(batch_tail) = cache.m_head;
//         cache.m_head = batch_head;
//         cache.m_count += fetched_count;
//     }
    
//     // 5. Write the header on the block we are returning and give it to the user.
//     Header* header = static_cast<Header*>(block_to_return);
//     header->pool_index = poolIndex;
//     return static_cast<void*>(header + 1);
// }

void* PoolAllocator::allocate(size_t size) {
    if (size == 0) return nullptr;
    const size_t required_size = size + sizeof(Header);
    if (required_size > POOL_SIZES.back()) return nullptr;

    const uint8_t poolIndex = m_size_to_pool_index[required_size];
    ThreadCache& cache = g_thread_caches[poolIndex];

    // --- FAST PATH ---
    if (cache.m_head != nullptr) {
        Header* header = static_cast<Header*>(cache.m_head);
        cache.m_head = header->next_in_cache;
        cache.m_count--;
        header->pool_index = poolIndex;
        return static_cast<void*>(header + 1);
    }

    // --- SLOW PATH ---
    // Refill the cache first.
    refill_cache(poolIndex);

    // After refilling, try the fast path again.
    if (cache.m_head != nullptr) {
        Header* header = static_cast<Header*>(cache.m_head);
        cache.m_head = header->next_in_cache;
        cache.m_count--;
        header->pool_index = poolIndex;
        return static_cast<void*>(header + 1);
    }
    
    // If the refill failed completely.
    return nullptr;
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

