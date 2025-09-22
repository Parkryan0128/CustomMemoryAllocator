#include "MyAllocator.hpp"
#include <iostream> // 임시

static MyAllocator g_allocator;

MyAllocator::MyAllocator() {
    for (size_t i = 0; i < NUM_POOLS; ++i) {
        // 8, 16, 24, 32, 40, 48, 56, 64 FixedSizeAllocator
        size_t size = (i + 1) * 8;
        m_pools[i] = new FixedSizeAllocator(size);
    }
    std::cout << "MyAllocator manager created." << std::endl;
}

MyAllocator::~MyAllocator() {
    for (size_t i = 0; i < NUM_POOLS; ++i) {
        delete m_pools[i];
    }
}

void* MyAllocator::malloc(size_t size) {
    // TODO:
    std::cout << "MyAllocator::malloc called for size " << size << std::endl;
    return nullptr;
}

void MyAllocator::free(void* ptr) {
    // TODO:
    std::cout << "MyAllocator::free called." << std::endl;
}

// ... calloc, realloc  ...

void* my_malloc(size_t size) {
    return g_allocator.malloc(size);
}

void my_free(void* ptr) {
    g_allocator.free(ptr);
}