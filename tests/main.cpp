#include "SingleSizeAllocator.hpp"
#include <iostream>
#include <vector>
#include <cassert> // For using the assert() macro

// The block size for our test must be defined. Let's use 128 bytes.
constexpr size_t TEST_BLOCK_SIZE = 128;

// This must match the CHUNK_SIZE inside the SingleSizeAllocator class.
constexpr size_t CHUNK_SIZE = 64 * 1024;

int main() {
    std::cout << "--- Starting SingleSizeAllocator Test ---" << std::endl;

    // The allocator is created on the stack.
    SingleSizeAllocator<TEST_BLOCK_SIZE> allocator;

    // --- Test 1: Basic Allocation and Deallocation ---
    std::cout << "\n## [TEST 1] Basic allocation and deallocation\n";
    void* block1 = allocator.allocate();
    assert(block1 != nullptr); // The first allocation should always succeed.
    std::cout << "  -> Successfully allocated one block.\n";

    allocator.deallocate(block1);
    std::cout << "  -> Successfully deallocated the block.\n";
    std::cout << "  -> PASSED ✅\n";

    // --- Test 2: Exhaust the allocator ---
    std::cout << "\n## [TEST 2] Allocate all blocks until empty\n";
    const size_t numBlocks = CHUNK_SIZE / TEST_BLOCK_SIZE;
    std::vector<void*> allocatedBlocks;
    allocatedBlocks.reserve(numBlocks);

    for (size_t i = 0; i < numBlocks; ++i) {
        void* block = allocator.allocate();
        assert(block != nullptr); // All of these should succeed.
        allocatedBlocks.push_back(block);
    }
    std::cout << "  -> Successfully allocated all " << numBlocks << " blocks.\n";

    void* outOfMemoryBlock = allocator.allocate();
    assert(outOfMemoryBlock == nullptr); // This must return nullptr.
    std::cout << "  -> Correctly returned nullptr on out-of-memory attempt.\n";
    std::cout << "  -> PASSED ✅\n";

    // --- Test 3: Deallocate all blocks and test reuse ---
    std::cout << "\n## [TEST 3] Deallocate all and re-allocate\n";
    for (void* block : allocatedBlocks) {
        allocator.deallocate(block);
    }
    std::cout << "  -> Successfully deallocated all " << numBlocks << " blocks.\n";

    void* reusedBlock = allocator.allocate();
    assert(reusedBlock != nullptr); // Should be able to allocate again.
    std::cout << "  -> Successfully re-allocated a block after freeing.\n";
    allocator.deallocate(reusedBlock); // Clean up the last allocation.
    std::cout << "  -> PASSED ✅\n";

    std::cout << "\n--- All SingleSizeAllocator Tests Passed ---" << std::endl;

    // The allocator's destructor is automatically called here when main exits.
    return 0;
}