#pragma once

#include "OS.hpp"
#include <cstddef>  // For size_t
#include <iostream> // For logging


class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual void* allocate() = 0;
    virtual void deallocate(void* ptr) = 0;
};

/**
 * @class SingleSizeAllocator
 * @brief A growable memory pool for blocks of a single, compile-time-defined size.
 *
 * This allocator requests memory from the OS in large chunks. When the initial
 * chunk is exhausted, it automatically requests additional chunks as needed.
 * All memory is returned to the OS upon destruction.
 *
 * @tparam BlockSize The size of each memory block in bytes.
 */
template <size_t BlockSize>
class SingleSizeAllocator : public IAllocator {
public:
    /**
     * @brief Constructor that allocates the initial chunk of memory.
     */
    SingleSizeAllocator() {
        grow(); // Allocate the first chunk immediately.
    }

    /**
     * @brief Destructor that traverses the list of chunks and returns them to the OS.
     */
    ~SingleSizeAllocator() {
        Chunk* current = m_chunkList;
        while (current != nullptr) {
            Chunk* next = current->next;
            free_chunk(current, CHUNK_SIZE);
            current = next;
        }
    }

    SingleSizeAllocator(const SingleSizeAllocator&) = delete;
    SingleSizeAllocator& operator=(const SingleSizeAllocator&) = delete;

    /**
     * @brief Allocates one block of memory. If the pool is empty, it attempts to grow.
     * @return A pointer to the allocated block, or nullptr if memory is exhausted.
     */
    void* allocate() override {
        if (m_head == nullptr) {
            grow();
        }

        if (m_head == nullptr) {
            return nullptr;
        }

        // Pop a block from the front of the free list (stack behavior).
        Block* freeBlock = m_head;
        m_head = m_head->next;
        return static_cast<void*>(freeBlock);
    }

    /**
     * @brief Returns a block of memory to the pool.
     * @param ptr A pointer to the memory block to deallocate.
     */
    void deallocate(void* ptr) override {
        if (ptr == nullptr) {
            return;
        }

        Block* blockToFree = static_cast<Block*>(ptr);
        blockToFree->next = m_head;
        m_head = blockToFree;
    }

private:
    // Represents one large chunk of memory obtained from the OS.
    struct Chunk {
        Chunk* next; 
    };

    // Represents a single block of memory, which is part of a chunk.
    union Block {
        Block* next;         
    };
    // The size of the memory chunk to request from the OS (e.g., 64 KB).
    static constexpr size_t CHUNK_SIZE = 64 * 1024;

    // Compile-time safety checks to prevent invalid template instantiations.
    // static_assert(BlockSize >= sizeof(Block), "BlockSize must be large enough to hold Block metadata.");
    // static_assert(CHUNK_SIZE > sizeof(Chunk), "CHUNK_SIZE must be larger than the Chunk metadata struct.");

    Block* m_head = nullptr;      // The head of the free block list (our stack).
    Chunk* m_chunkList = nullptr; // The head of the list of all allocated chunks.

    /**
     * @brief Acquires a new chunk of memory from the OS and adds its blocks to the free list.
     */
    void grow() {
        Chunk* newChunk = static_cast<Chunk*>(alloc_chunk(CHUNK_SIZE));
        if (newChunk == nullptr) return;

        newChunk->next = m_chunkList;
        m_chunkList = newChunk;

        char* const chunkStart = reinterpret_cast<char*>(newChunk);
        char* const blockStart = chunkStart + sizeof(Chunk);
        char* const chunkEnd = chunkStart + CHUNK_SIZE;

        for (char* p = blockStart; p + BlockSize <= chunkEnd; p += BlockSize) {
            Block* newBlock = reinterpret_cast<Block*>(p);
            newBlock->next = m_head;
            m_head = newBlock;
        }
    }
};