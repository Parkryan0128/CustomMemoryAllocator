#pragma once
#include <cstddef> // To use size_t

class FixedSizeAllocator {
public:
    // Constructor: Takes block size as an argument.
    FixedSizeAllocator(size_t blockSize);
    // Destructor: Returns allocated memory to the OS.
    ~FixedSizeAllocator();

    // Allocate memory (not yet implemented)
    void* allocate();
    // Deallocate memory (not yet implemented)
    void deallocate(void* ptr);

private:
    size_t m_blockSize;
    void* m_chunkStart = nullptr; // Start address of the chunk allocated from the OS
    size_t m_chunkSize = 0;       // Total size of the chunk allocated from the OS
};