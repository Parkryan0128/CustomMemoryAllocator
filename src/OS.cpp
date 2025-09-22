#include "OS.hpp"
#include <iostream> // For debugging

// Use preprocessor directives to include the correct headers for the target OS.
#ifdef _WIN32
#include <windows.h> // For VirtualAlloc and VirtualFree on Windows
#else
#include <sys/mman.h> // For mmap and munmap on POSIX systems (Linux, macOS)
#endif

// This code is now cross-platform.

void* alloc_chunk(size_t size) {
#ifdef _WIN32
    // Windows implementation using VirtualAlloc
    // - NULL: Let the system choose the optimal address.
    // - size: The size of the memory to request.
    // - MEM_COMMIT | MEM_RESERVE: Reserve and commit pages in one step.
    // - PAGE_READWRITE: Set permissions to read and write to the memory.
    void* ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (ptr == NULL) {
        std::cerr << "VirtualAlloc failed to allocate chunk." << std::endl;
        return nullptr;
    }
    return ptr;
#else
    // POSIX implementation using mmap
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "mmap failed to allocate chunk." << std::endl;
        return nullptr;
    }
    return ptr;
#endif
}

void free_chunk(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return;
    }
#ifdef _WIN32
    // Windows implementation using VirtualFree
    // - MEM_RELEASE: Releases the entire region of pages. The size must be 0.
    if (VirtualFree(ptr, 0, MEM_RELEASE) == 0) {
        std::cerr << "VirtualFree failed to free chunk." << std::endl;
    }
#else
    // POSIX implementation using munmap
    // munmap requires the original size of the mapping.
    if (munmap(ptr, size) != 0) {
        std::cerr << "munmap failed to free chunk." << std::endl;
    }
#endif
}