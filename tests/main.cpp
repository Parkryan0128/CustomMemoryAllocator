#include "SingleSizeAllocator.hpp"
#include <iostream>
#include <vector>
#include <cassert>

constexpr size_t TEST_BLOCK_SIZE = 128;

// This friend class has public helper functions to access the allocator's private data.
class AllocatorFriend {
public:
    template <size_t BlockSize>
    static const typename SingleSizeAllocator<BlockSize>::Chunk* getChunkList(const SingleSizeAllocator<BlockSize>& alloc) {
        return alloc.m_chunkList;
    }

    template <size_t BlockSize>
    static constexpr size_t getChunkSize() {
        return SingleSizeAllocator<BlockSize>::CHUNK_SIZE;
    }
    
    template <size_t BlockSize>
    static constexpr size_t getChunkStructSize() {
        return sizeof(typename SingleSizeAllocator<BlockSize>::Chunk);
    }
};

int main() {
    std::cout << "--- Starting Comprehensive Growable Allocator Test ---" << std::endl;

    SingleSizeAllocator<TEST_BLOCK_SIZE> allocator;

    // --- Test 1: Basic allocation and deallocation ---
    std::cout << "\n## [TEST 1] Basic allocation and deallocation\n";
    void* block1 = allocator.allocate();
    assert(block1 != nullptr);
    std::cout << "  -> Successfully allocated one block.\n";
    allocator.deallocate(block1);
    std::cout << "  -> Successfully deallocated the block.\n";
    std::cout << "  -> PASSED ✅\n";

    // --- Test 2: Exhaust the first chunk and trigger grow() ---
    std::cout << "\n## [TEST 2] Exhausting the first chunk and triggering grow()\n";

    // CORRECTED: This calculation now uses the friend class to get the private data.
    const size_t numBlocksInChunk = (AllocatorFriend::getChunkSize<TEST_BLOCK_SIZE>() - AllocatorFriend::getChunkStructSize<TEST_BLOCK_SIZE>()) / TEST_BLOCK_SIZE;

    std::vector<void*> allocatedBlocks;
    allocatedBlocks.reserve(numBlocksInChunk + 1);

    for (size_t i = 0; i < numBlocksInChunk; ++i) {
        void* block = allocator.allocate();
        assert(block != nullptr);
        allocatedBlocks.push_back(block);
    }
    std::cout << "  -> Successfully allocated all " << numBlocksInChunk << " blocks from the first chunk.\n";
    assert(AllocatorFriend::getChunkList(allocator) != nullptr && AllocatorFriend::getChunkList(allocator)->next == nullptr);

    void* blockFromNewChunk = allocator.allocate();
    assert(blockFromNewChunk != nullptr);
    allocatedBlocks.push_back(blockFromNewChunk);

    std::cout << "  -> Successfully allocated one more block, forcing a new chunk to be created.\n";
    assert(AllocatorFriend::getChunkList(allocator)->next != nullptr);
    std::cout << "  -> PASSED ✅\n";

    // --- Test 3: Deallocate all blocks ---
    std::cout << "\n## [TEST 3] Deallocating all blocks from both chunks\n";
    for (void* block : allocatedBlocks) {
        allocator.deallocate(block);
    }
    std::cout << "  -> Successfully deallocated all " << allocatedBlocks.size() << " blocks.\n";
    std::cout << "  -> PASSED ✅\n";

    std::cout << "\n--- All Growable Allocator Tests Passed ---" << std::endl;
    return 0;
}