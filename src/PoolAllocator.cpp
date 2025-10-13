#include "PoolAllocator.hpp"
#include <iostream>

thread_local ThreadCache g_thread_caches[PoolAllocator::POOL_SIZES.size()];

PoolAllocator::PoolAllocator() {
    uint8_t poolIndex = 0;
    for (size_t size = 1; size <= POOL_SIZES.back(); ++size) {
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

PoolAllocator::~PoolAllocator() {
    for (auto* pool : m_pools) {
        delete pool;
    }
}

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
        std::cerr << "ERROR: Invalid pool index!\n";
        return;
    }

    ThreadCache& cache = g_thread_caches[poolIndex];
    
    header->next_in_cache = static_cast<Header*>(cache.m_head);
    cache.m_head = header;
    cache.m_count++;
}

void PoolAllocator::refill_cache(uint8_t poolIndex) {
    std::lock_guard<std::mutex> lock(m_pool_mutexes[poolIndex]);

    ThreadCache& cache = g_thread_caches[poolIndex];
    
    const size_t batch_size = 20;

    for (size_t i = 0; i < batch_size; ++i) {
        void* block = m_pools[poolIndex]->allocate();
        if (block == nullptr) break;
        
        Header* header = static_cast<Header*>(block);
        header->next_in_cache = static_cast<Header*>(cache.m_head);
        cache.m_head = header;
        cache.m_count++;
    }
}