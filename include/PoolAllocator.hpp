#pragma once

#include "SingleSizeAllocator.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <tuple> // Required for std::tuple

namespace MyAlloc {

class PoolAllocator {
private:
    // Helper struct to get the type of the Nth allocator in our tuple
    template<std::size_t I>
    using AllocatorType = std::tuple_element_t<I, std::tuple<
        SingleSizeAllocator<8>, SingleSizeAllocator<16>, SingleSizeAllocator<24>,
        SingleSizeAllocator<32>, SingleSizeAllocator<40>, SingleSizeAllocator<48>,
        SingleSizeAllocator<56>, SingleSizeAllocator<64>, SingleSizeAllocator<96>,
        SingleSizeAllocator<128>, SingleSizeAllocator<192>, SingleSizeAllocator<256>,
        SingleSizeAllocator<384>, SingleSizeAllocator<512>
    >>;

    // A recursive template helper to call allocate on the correct allocator in the tuple
    template<std::size_t I = 0>
    void* dispatch_allocate(size_t index) {
        if constexpr (I < std::tuple_size_v<decltype(m_pools)>) {
            if (index == I) {
                return std::get<I>(m_pools).allocate();
            }
            return dispatch_allocate<I + 1>(index);
        }
        return nullptr; // Should not happen
    }

    // A recursive template helper to call deallocate on the correct allocator in the tuple
    template<std::size_t I = 0>
    void dispatch_deallocate(size_t index, void* ptr) {
        if constexpr (I < std::tuple_size_v<decltype(m_pools)>) {
            if (index == I) {
                std::get<I>(m_pools).deallocate(ptr);
                return;
            }
            dispatch_deallocate<I + 1>(index, ptr);
        }
    }

    struct Header {
        uint8_t pool_index;
    };

    static constexpr std::array<size_t, 14> POOL_SIZES = {
        8, 16, 24, 32, 40, 48, 56, 64,
        96, 128, 192, 256, 384, 512
    };

    std::array<uint8_t, POOL_SIZES.back() + 1> m_size_to_pool_index;

    // The tuple holds the actual allocator objects directly, avoiding pointers and virtual calls.
    std::tuple<
        SingleSizeAllocator<8>, SingleSizeAllocator<16>, SingleSizeAllocator<24>,
        SingleSizeAllocator<32>, SingleSizeAllocator<40>, SingleSizeAllocator<48>,
        SingleSizeAllocator<56>, SingleSizeAllocator<64>, SingleSizeAllocator<96>,
        SingleSizeAllocator<128>, SingleSizeAllocator<192>, SingleSizeAllocator<256>,
        SingleSizeAllocator<384>, SingleSizeAllocator<512>
    > m_pools;

public:
    PoolAllocator() {
        // Pre-calculate the lookup table.
        uint8_t poolIndex = 0;
        for (size_t size = 1; size <= POOL_SIZES.back(); ++size) {
            if (size > POOL_SIZES[poolIndex]) {
                poolIndex++;
            }
            m_size_to_pool_index[size] = poolIndex;
        }
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    void* allocate(size_t size) {
        if (size == 0) return nullptr;
        const size_t total_size = size + sizeof(Header);

        if (total_size > POOL_SIZES.back()) return nullptr;
        
        const uint8_t poolIndex = m_size_to_pool_index[total_size];

        // This now calls a template function which results in a direct, non-virtual call.
        void* raw_block = dispatch_allocate(poolIndex);

        if (raw_block == nullptr) return nullptr;

        reinterpret_cast<Header*>(raw_block)->pool_index = poolIndex;
        return static_cast<void*>(static_cast<char*>(raw_block) + sizeof(Header));
    }

    void deallocate(void* ptr) {
        if (ptr == nullptr) return;

        void* raw_block = static_cast<void*>(static_cast<char*>(ptr) - sizeof(Header));
        const uint8_t poolIndex = reinterpret_cast<Header*>(raw_block)->pool_index;

        if (poolIndex < POOL_SIZES.size()) {
            // This also results in a direct, non-virtual call.
            dispatch_deallocate(poolIndex, raw_block);
        }
    }
};

} // namespace MyAlloc