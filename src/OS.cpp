#include "OS.hpp"
#include <iostream>

// Platform-specific includes
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h> // For mmap and munmap on POSIX systems (Linux, macOS)
#endif

/**
 * @brief Allocates a memory chunk of a specified size from the operating system.
 */
void* alloc_chunk(size_t size) {
#if defined(_WIN32)
    void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (ptr == nullptr) {
        std::cerr << "VirtualAlloc failed to allocate chunk." << std::endl;
        return nullptr;
    }
    return ptr;
#else
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (ptr == MAP_FAILED) {
        std::cerr << "mmap failed to allocate chunk." << std::endl;
        return nullptr;
    }
    return ptr;
#endif
}

/**
 * @brief Returns a previously allocated memory chunk to the operating system.
 */
void free_chunk(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return;
    }
#if defined(_WIN32)
    (void)size; // Prevents "unused parameter" warnings
    if (VirtualFree(ptr, 0, MEM_RELEASE) == 0) {
        std::cerr << "VirtualFree failed to free chunk." << std::endl;
    }
#else
    // POSIX implementation using munmap
    // munmap requires the pointer and the original size of the mapping to free the memory.
    if (munmap(ptr, size) != 0) {
        std::cerr << "munmap failed to free chunk." << std::endl;
    }
#endif
}