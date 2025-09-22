#include "MyAllocator.hpp"
#include <iostream> // 임시

static MyAllocator g_allocator;

MyAllocator::MyAllocator() {
    // TODO:
    std::cout << "MyAllocator manager created." << std::endl;
}

MyAllocator::~MyAllocator() {
    // TODO:
}

void* MyAllocator::malloc(size_t size) {
    // TODO:
    std::cout << "MyAllocator::malloc called for size " << size << std::endl;
    return nullptr; // 임시
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