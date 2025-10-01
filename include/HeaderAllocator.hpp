#pragma once

#include "OS.hpp"
#include <cstddef>
#include <cstdint> // Required for uint8_t
#include <iostream>

namespace MyAlloc {

/**
 * @class HeaderAllocator
 * @brief A memory pool that allocates blocks with a prepended 1-byte header space.
 *
 * This allocator is similar to SingleSizeAllocator but carves memory into blocks
 * of size (BlockSize + 1 byte) to reserve space for metadata.
 *
 * @tparam UserBlockSize The size of the memory block requested by the user.
 */
template <size_t UserBlockSize>
class HeaderAllocator {
private:
    // Define the size of the header and the actual block size we'll manage.
    static constexpr size_t HEADER_SIZE = 1;
    static constexpr size_t ACTUAL_BLOCK_SIZE = UserBlockSize + HEADER_SIZE;

    // Represents one large chunk of memory obtained from the OS.
    struct Chunk {
        Chunk* next;
    };

    // Represents a single block of memory in the free list.
    // Its size includes space for the user's data and our header.
    union Block {
        char data[ACTUAL_BLOCK_SIZE];
        Block* next;
    };

    // The size of the memory chunk to request from the OS (e.g., 64 KB).
    static constexpr size_t CHUNK_SIZE = 64 * 1024;

    // Compile-time safety checks.
    static_assert(ACTUAL_BLOCK_SIZE >= sizeof(void*), "Block size + header must be large enough to hold a pointer.");
    static_assert(CHUNK_SIZE > sizeof(Chunk), "CHUNK_SIZE must be larger than the Chunk metadata struct.");

    Block* m_head = nullptr;      // The head of the free block list.
    Chunk* m_chunkList = nullptr; // The head of the list of all allocated chunks.

public:
    /**
     * @brief Constructor that allocates the initial chunk of memory.
     */
    HeaderAllocator() {
        grow();
    }

    /**
     * @brief Destructor that returns all allocated chunks to the OS.
     */
    ~HeaderAllocator() {
        Chunk* current = m_chunkList;
        while (current != nullptr) {
            Chunk* next = current->next;
            free_chunk(current, CHUNK_SIZE);
            current = next;
        }
    }

    // This allocator is a unique resource manager and should not be copyable.
    HeaderAllocator(const HeaderAllocator&) = delete;
    HeaderAllocator& operator=(const HeaderAllocator&) = delete;

    /**
     * @brief Allocates one block of memory.
     * @return A pointer to the user's memory area (after the header space).
     */
    void* allocate() {
        if (m_head == nullptr) {
            grow();
        }
        if (m_head == nullptr) {
            return nullptr; // Growth failed.
        }

        // Pop a raw block (which includes header space) from the free list.
        Block* raw_block = m_head;
        m_head = m_head->next;

        // Return a pointer to the memory area just AFTER the 1-byte header space.
        return static_cast<void*>(reinterpret_cast<char*>(raw_block) + HEADER_SIZE);
    }

    /**
     * @brief Deallocates a block of memory.
     * @param ptr The pointer to the user's memory area.
     */
    void deallocate(void* ptr) {
        if (ptr == nullptr) {
            return;
        }

        // Calculate the address of the original raw block by subtracting the header size.
        Block* raw_block = reinterpret_cast<Block*>(static_cast<char*>(ptr) - HEADER_SIZE);

        // Push the entire raw block back onto the front of the free list.
        raw_block->next = m_head;
        m_head = raw_block;
    }

private:
    /**
     * @brief Acquires a new chunk and splices it into blocks of ACTUAL_BLOCK_SIZE.
     */
    void grow() {
        Chunk* newChunk = static_cast<Chunk*>(alloc_chunk(CHUNK_SIZE));
        if (newChunk == nullptr) {
            std::cerr << "FATAL: HeaderAllocator failed to grow; OS memory allocation failed.\n";
            return;
        }

        newChunk->next = m_chunkList;
        m_chunkList = newChunk;

        // Carve up the new chunk into blocks of the correct total size.
        char* const chunkStart = reinterpret_cast<char*>(newChunk);
        char* const blockStart = chunkStart + sizeof(Chunk);
        char* const chunkEnd = chunkStart + CHUNK_SIZE;

        for (char* p = blockStart; p + ACTUAL_BLOCK_SIZE <= chunkEnd; p += ACTUAL_BLOCK_SIZE) {
            Block* newBlock = reinterpret_cast<Block*>(p);
            newBlock->next = m_head;
            m_head = newBlock;
        }
    }
};

} // namespace MyAlloc
