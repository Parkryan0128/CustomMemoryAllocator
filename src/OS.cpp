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
    // Windows implementation using VirtualAlloc
    // - nullptr: Let the system determine where to allocate the region.
    // - size: The size of the region, in bytes.
    // - MEM_COMMIT | MEM_RESERVE: Reserves and commits pages in one step.
    // - PAGE_READWRITE: The memory can be read from and written to.
    void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (ptr == nullptr) {
        // GetLastError() can be used for more detailed error info on Windows.
        std::cerr << "VirtualAlloc failed to allocate chunk." << std::endl;
        return nullptr;
    }
    return ptr;
#else
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
    // Windows implementation using VirtualFree
    // - ptr: The base address of the region to be decommitted.
    // - 0: When using MEM_RELEASE, this parameter must be 0. The entire region is released.
    // - MEM_RELEASE: Releases the specified region of pages.
    // The 'size' parameter is not needed for VirtualFree with MEM_RELEASE but is kept for API consistency.
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