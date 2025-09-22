#pragma once
#include <cstddef>

class FixedSizeAllocator {
public:
    FixedSizeAllocator(size_t blockSize);
    ~FixedSizeAllocator();

    void* allocate();

    void deallocate(void* ptr);

private:
    // TODO: Declare member variable
    size_t m_blockSize;
};