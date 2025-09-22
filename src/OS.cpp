#include "OS.hpp"
#include <iostream>   // For error reporting
#include <sys/mman.h> // For mmap and munmap on POSIX systems (Linux, macOS)

// This implementation is specific to POSIX-compliant systems like Linux.

void* alloc_chunk(size_t size) {
    // POSIX implementation using mmap
    // - nullptr: Let the kernel choose the address.
    // - size: The size of the memory mapping.
    // - PROT_READ | PROT_WRITE: The memory can be read from and written to.
    // - MAP_PRIVATE | MAP_ANONYMOUS: The mapping is private to this process and not backed by any file.
    // - -1, 0: Arguments required for file-backed mappings, not used here.
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (ptr == MAP_FAILED) {
        std::cerr << "mmap failed to allocate chunk." << std::endl;
        return nullptr;
    }
    return ptr;
}

void free_chunk(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return;
    }
    // POSIX implementation using munmap
    // munmap requires the pointer and the original size of the mapping to free the memory.
    if (munmap(ptr, size) != 0) {
        std::cerr << "munmap failed to free chunk." << std::endl;
    }
}