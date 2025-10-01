#pragma once

#include "HeaderAllocator.hpp" // Uses the new HeaderAllocator
#include <array>
#include <cstdint>
#include <iostream>
#include <tuple>

namespace MyAlloc {

/**
 * @class PreconfPoolAllocator
 * @brief A pool allocator that uses pre-configured HeaderAllocator instances for its pools.
 *
 * This allocator creates all of its underlying memory pools at startup. It uses the
 * header space provided by each HeaderAllocator to store metadata, enabling fast,
 * size-agnostic deallocation.
 */
class PreconfPoolAllocator {
private:
    // Template helpers for compile-time dispatch to the correct allocator in the tuple.
    template<std::size_t I = 0>
    void* dispatch_allocate(size_t index) {
        if constexpr (I < std::tuple_size_v<decltype(m_pools)>) {
            if (index == I) return std::get<I>(m_pools).allocate();
            return dispatch_allocate<I + 1>(index);
        }
        return nullptr;
    }
    template<std::size_t I = 0>
    void dispatch_deallocate(size_t index, void* ptr) {
        if constexpr (I < std::tuple_size_v<decltype(m_pools)>) {
            if (index == I) { std::get<I>(m_pools).deallocate(ptr); return; }
            dispatch_deallocate<I + 1>(index, ptr);
        }
    }

    // This is the header that this PoolAllocator will write into the space
    // provided by the underlying HeaderAllocator.
    struct Header {
        uint8_t pool_index;
    };

    static constexpr std::array<size_t, 14> POOL_SIZES = {
        8, 16, 24, 32, 40, 48, 56, 64,
        96, 128, 192, 256, 384, 512
    };

    // The lookup table maps a requested size to a pool index for O(1) lookups.
    std::array<uint8_t, POOL_SIZES.back() + 1> m_size_to_pool_index;

    // The tuple holds the HeaderAllocator instances directly. All are constructed
    // when this parent class is constructed.
    std::tuple<
        HeaderAllocator<8>, HeaderAllocator<16>, HeaderAllocator<24>,
        HeaderAllocator<32>, HeaderAllocator<40>, HeaderAllocator<48>,
        HeaderAllocator<56>, HeaderAllocator<64>, HeaderAllocator<96>,
        HeaderAllocator<128>, HeaderAllocator<192>, HeaderAllocator<256>,
        HeaderAllocator<384>, HeaderAllocator<512>
    > m_pools;

public:
    PreconfPoolAllocator() {
        // Pre-calculate the size-to-index lookup table once during construction.
        uint8_t poolIndex = 0;
        for (size_t size = 1; size <= POOL_SIZES.back(); ++size) {
            if (size > POOL_SIZES[poolIndex]) {
                poolIndex++;
            }
            m_size_to_pool_index[size] = poolIndex;
        }
    }

    PreconfPoolAllocator(const PreconfPoolAllocator&) = delete;
    PreconfPoolAllocator& operator=(const PreconfPoolAllocator&) = delete;

    /**
     * @brief Allocates a block of memory.
     * @param size The minimum number of bytes the user requests.
     * @return A pointer to the allocated memory, or nullptr on failure.
     */
    void* allocate(size_t size) {
        if (size == 0 || size > POOL_SIZES.back()) {
            return nullptr;
        }

        const uint8_t poolIndex = m_size_to_pool_index[size];

        // 1. Allocate a block from the appropriate HeaderAllocator.
        // This pointer is already offset past the 1-byte header space.
        void* user_ptr = dispatch_allocate(poolIndex);

        if (user_ptr == nullptr) {
            return nullptr; // Underlying pool is out of memory.
        }

        // 2. Get the address of the header space and write our metadata into it.
        void* header_ptr = static_cast<char*>(user_ptr) - sizeof(Header);
        reinterpret_cast<Header*>(header_ptr)->pool_index = poolIndex;

        // 3. Return the original user pointer.
        return user_ptr;
    }

    /**
     * @brief Deallocates a block of memory.
     * @param ptr The pointer to the user's memory area.
     */
    void deallocate(void* ptr) {
        if (ptr == nullptr) return;

        // 1. Get the address of the header space.
        void* header_ptr = static_cast<char*>(ptr) - sizeof(Header);

        // 2. Read our metadata (the pool index) from the header.
        const uint8_t poolIndex = reinterpret_cast<Header*>(header_ptr)->pool_index;

        // 3. Dispatch the deallocation call to the correct underlying HeaderAllocator.
        if (poolIndex < POOL_SIZES.size()) {
            dispatch_deallocate(poolIndex, ptr);
        } else {
            std::cerr << "Deallocation error: Invalid memory block or corrupted header.\n";
        }
    }
};

} // namespace MyAlloc
