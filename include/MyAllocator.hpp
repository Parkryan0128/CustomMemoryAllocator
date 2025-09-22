#pragma once
#include <cstddef>
#include "FixedSizeAllocator.hpp"

constexpr size_t NUM_POOLS = 8;

class MyAllocator {
public:
    MyAllocator();
    ~MyAllocator();

    void* malloc(size_t size);
    void free(void* ptr);
    void* calloc(size_t num, size_t size);
    void* realloc(void* ptr, size_t newSize);

private:
    FixedSizeAllocator* m_pools[NUM_POOLS];
};

void* my_malloc(size_t size);
void my_free(void* ptr);