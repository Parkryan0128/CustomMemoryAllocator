#pragma once

#include <cstddef> // For size_t

/**
 * @class SingleSizeAllocator
 * @brief Manages a pool of memory blocks of a single, compile-time-defined size.
 *
 * @tparam BlockSize The size of each memory block in bytes.
 */
template <size_t BlockSize>
class SingleSizeAllocator {
public:
    // Constructor: Acquires a memory chunk and initializes the free list.
    SingleSizeAllocator();

    // Destructor: Returns the entire memory chunk to the operating system.
    ~SingleSizeAllocator();

    // Deleted copy constructor and assignment operator to prevent copying.
    SingleSizeAllocator(const SingleSizeAllocator&) = delete;
    SingleSizeAllocator& operator=(const SingleSizeAllocator&) = delete;

    // Allocates one block of memory from the pool.
    void* allocate();

    // Returns a block of memory to the pool.
    void deallocate(void* ptr);

private:
    static constexpr size_t CHUNK_SIZE = 64 * 1024;

    union Block {
        char data[BlockSize];
        Block* next;
    };

    Block* m_head = nullptr;
    void* m_chunkStart = nullptr;
    size_t m_chunkSize = 0;
};

// Include the template implementation file.
// This is the standard way to separate template declaration from de