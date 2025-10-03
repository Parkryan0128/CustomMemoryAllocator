#pragma once

#include "MemoryPool.hpp" // Contains IAllocator and the MemoryPool template class
#include <array>
#include <cstdint>
#include <iostream>
#include <tuple>



class PoolAllocator {
public:
    /**
     * @brief Constructor: Initializes the lookup table and creates all the memory pools.
     */
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
    struct Header {
        uint8_t pool_index;
    };

    static constexpr std::array<size_t, 14> POOL_SIZES = {
        8, 16, 24, 32, 40, 48, 56, 64,
        96, 128, 192, 256, 384, 512
    };

    std::array<IAllocator*, POOL_SIZES.size()> m_pools{};
    std::array<uint8_t, POOL_SIZES.back() + 1> m_size_to_pool_index{};
};



void* PoolAllocator::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    // 2. Calculate the total size needed: the user's request + our 1-byte header.
    const size_t total_size = size + sizeof(Header);

    // 3. Handle large allocations: If the request is too big for our largest pool, fail.
    if (total_size > POOL_SIZES.back()) {
        std::cerr << "Request for " << size << " bytes is too large for any pool.\n";
        return nullptr; // In a more advanced version, this could fall back to system malloc.
    }
    
    // 4. Find the correct pool to use with an O(1) lookup in our pre-calculated table.
    const uint8_t poolIndex = m_size_to_pool_index[total_size];
    
    // 5. Request a raw, empty block from the appropriate specialist pool.
    void* raw_block = m_pools[poolIndex]->allocate();

    // If the pool was exhausted and failed to grow, it will return nullptr.
    if (raw_block == nullptr) {
        return nullptr;
    }

    // 6. "Stamp" the block: Write our 1-byte header into the very beginning of the raw block.
    Header* header = static_cast<Header*>(raw_block);
    header->pool_index = poolIndex;
    
    // 7. Return the offset pointer: Give the user a pointer to the memory *after* our header.
    return static_cast<void*>(header + 1);
}


void PoolAllocator::deallocate(void* ptr) {
    // 1. Safety check: C++ standard says deleting a nullptr is safe, so we'll match that.
    if (ptr == nullptr) {
        return;
    }

    // 2. Calculate the original block's address by moving back by the size of the header.
    // This is the reverse of the offset we applied in allocate().
    Header* header = static_cast<Header*>(ptr) - 1;
    
    // 3. Read the "return label": the 1-byte pool index from the header.
    const uint8_t poolIndex = header->pool_index;

    // 4. A safety check to ensure the header hasn't been corrupted.
    if (poolIndex < POOL_SIZES.size()) {
        // 5. Return the original raw block (the 'header' pointer) to the correct pool.
        m_pools[poolIndex]->deallocate(header);
    } else {
        // This is a sign of a serious bug, like memory corruption or trying
        // to free memory that wasn't allocated by this allocator.
        std::cerr << "ERROR: Invalid pool index detected during deallocation! Memory corruption likely." << std::endl;
    }
}

PoolAllocator::~PoolAllocator() {
    // std::cout << "Destroying PoolAllocator and freeing all pools." << std::endl;
    for (auto* pool : m_pools) {
        delete pool;
    }
}