#include "FixedSizeAllocator.hpp"
#include "OS.hpp"       // To use alloc_chunk, free_chunk functions
#include <iostream>

// Define the size of the memory chunk to be allocated from the OS at once as a constant. (e.g., 64 KB)
constexpr size_t CHUNK_SIZE = 64 * 1024;

FixedSizeAllocator::FixedSizeAllocator(size_t blockSize) : m_blockSize(blockSize) {
    // std::cout << "Creating an allocator for " << m_blockSize << " byte blocks." << std::endl;

    // ✅ Mission: Allocate a memory chunk from the OS.
    m_chunkStart = alloc_chunk(CHUNK_SIZE);
    
    if (m_chunkStart) {
        m_chunkSize = CHUNK_SIZE; // Store the size only on successful allocation.
        // std::cout << "Successfully acquired " << m_chunkSize << " bytes from OS at address " << m_chunkStart << std::endl;
    } else {
        // std::cerr << "FATAL: Failed to acquire memory from OS. Allocator is unusable." << std::endl;
        m_chunkSize = 0;
    }
}

FixedSizeAllocator::~FixedSizeAllocator() {
    // std::cout << "Destroying allocator. Returning " << m_chunkSize << " bytes to OS." << std::endl;
    // ✅ Mission: Return the allocated memory chunk to the OS.
    free_chunk(m_chunkStart, m_chunkSize);
}

void* FixedSizeAllocator::allocate() {
    // TODO: To be implemented in the next mission.
    // std::cout << "Allocating " << m_blockSize << " bytes..." << std::endl;
    return nullptr;
}

void FixedSizeAllocator::deallocate(void* ptr) {
    // TODO: To be implemented in the next mission.
    // std::cout << "Deallocating " << m_blockSize << " bytes..." << std::endl;
}