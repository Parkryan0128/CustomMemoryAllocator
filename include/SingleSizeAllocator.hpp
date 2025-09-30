#pragma once

#include "OS.hpp"
#include <cstddef>  // For size_t
#include <iostream> // For logging

/**
 * @class SingleSizeAllocator
 * @brief Manages a pool of memory blocks of a single, compile-time-defined size.
 *
 * This allocator is templated on the block size. It requests one large chunk
 * of memory from the OS and uses a union-based free list to manage available blocks,
 * which is highly memory-efficient.
 *
 * @tparam BlockSize The size of each memory block in bytes.
 */
template <size_t BlockSize>
class SingleSizeAllocator {
public:
    /**
     * @brief Constructor that acquires a memory chunk and initializes the free list.
     */
    SingleSizeAllocator() {
        std::cout << "Creating SingleSizeAllocator for " << BlockSize << " byte blocks." << std::endl;

        // 1. Acquire a large memory chunk from the OS.
        m_chunkStart = alloc_chunk(CHUNK_SIZE);

        if (m_chunkStart) {
            m_chunkSize = CHUNK_SIZE;
            std::cout << "Acquired " << m_chunkSize << " bytes from OS at " << m_chunkStart << std::endl;

            // 2. Splice the chunk into a linked list of free blocks.
            const size_t numBlocks = m_chunkSize / BlockSize;
            std::cout << "Splicing chunk into " << numBlocks << " free blocks." << std::endl;

            // The first block is the initial head of our free list.
            m_head = static_cast<Block*>(m_chunkStart);

            Block* current = m_head;
            for (size_t i = 0; i < numBlocks - 1; ++i) {
                // The next block is at the current address + BlockSize.
                Block* nextBlock = reinterpret_cast<Block*>(reinterpret_cast<char*>(current) + BlockSize);
                current->next = nextBlock; // Link current block to the next one.
                current = nextBlock;       // Move to the next block.
            }

            // The last block's 'next' pointer must be null to terminate the list.
            current->next = nullptr;

        } else {
            std::cerr << "FATAL: Failed to acquire memory from OS. Allocator is unusable." << std::endl;
            m_chunkSize = 0;
            m_head = nullptr;
        }
    }

    /**
     * @brief Destructor that returns the entire memory chunk to the OS.
     */
    ~SingleSizeAllocator() {
        std::cout << "Destroying SingleSizeAllocator. Returning " << m_chunkSize << " bytes to OS." << std::endl;
        free_chunk(m_chunkStart, m_chunkSize);
    }

    // Deleted copy constructor and assignment operator to prevent copying.
    SingleSizeAllocator(const SingleSizeAllocator&) = delete;
    SingleSizeAllocator& operator=(const SingleSizeAllocator&) = delete;

    /**
     * @brief Allocates one block of memory from the pool.
     * @return A pointer to the allocated block, or nullptr if out of memory.
     */
    void* allocate() {
        if (m_head == nullptr) {
            std::cerr << "OUT OF MEMORY: SingleSizeAllocator has no free blocks." << std::endl;
            return nullptr;
        }

        // Pop the head of the free list.
        Block* blockToReturn = m_head;
        m_head = m_head->next; // The new head is the next block in the list.

        std::cout << "Allocated block at address " << static_cast<void*>(blockToReturn) << std::endl;
        return blockToReturn;
    }

    /**
     * @brief Returns a block of memory to the pool.
     * @param ptr A pointer to the memory block to deallocate.
     */
    void deallocate(void* ptr) {
        if (ptr == nullptr) {
            return; // Standard behavior: deallocating null does nothing.
        }

        // Cast the returned pointer back to a Block.
        Block* freedBlock = static_cast<Block*>(ptr);

        // Push the freed block to the front of the free list.
        freedBlock->next = m_head;
        m_head = freedBlock;

        std::cout << "Deallocated block at address " << ptr << std::endl;
    }

private:
    // The size of the memory chunk to request from the OS. (e.g., 64 KB)
    static constexpr size_t CHUNK_SIZE = 64 * 1024;

    /**
     * @brief Represents a single block of memory.
     *
     * It's a union because a block is either part of the free list (using 'next')
     * or it's holding user data (using 'data'), but never both at the same time.
     * This is a memory-efficient way to implement a pool allocator.
     */
    union Block {
        char data[BlockSize]; // Ensures the block is the correct size for the user.
        Block* next;          // Points to the next available free block.
    };

    Block* m_head = nullptr;          // The head of the free list.
    void* m_chunkStart = nullptr;     // The start address of the chunk from the OS.
    size_t m_chunkSize = 0;           // The total size of the chunk.
};