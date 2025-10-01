#include "SingleSizeAllocator.hpp"
#include <iostream>
#include <vector>
#include <chrono> // The C++ timing library
#include <iomanip>


constexpr size_t BLOCK_SIZE = 32;
SingleSizeAllocator<BLOCK_SIZE> my_pool;

// Wrapper for your allocator to match malloc/free signature
void* my_malloc_test(size_t size) {
    // In a real test, you might want to assert(size == BLOCK_SIZE);
    return my_pool.allocate();
}

void my_free_test(void* ptr) {
    my_pool.deallocate(ptr);
}

constexpr size_t NUM_ALLOCATIONS = 10000000;


int main() {
    std::cout << "--- Allocator Performance Benchmark ---" << std::endl;
    std::cout << "Performing " << NUM_ALLOCATIONS << " allocations of " << BLOCK_SIZE << " bytes." << std::endl;

    std::vector<void*> pointers;
    // Pre-allocating the vector's memory so we don't measure its own reallocations.
    pointers.reserve(NUM_ALLOCATIONS);

    // --- Test 1: System malloc/free ---
    std::cout << "\nTesting system malloc()..." << std::endl;
    auto start_malloc = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        pointers.push_back(malloc(BLOCK_SIZE));
    }
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        free(pointers[i]);
    }
    
    auto end_malloc = std::chrono::high_resolution_clock::now();
    auto duration_malloc = std::chrono::duration_cast<std::chrono::milliseconds>(end_malloc - start_malloc);
    pointers.clear(); // Clear the vector for the next test

    // --- Test 2: Custom my_malloc/my_free ---
    std::cout << "Testing my_malloc_test()..." << std::endl;
    auto start_custom = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        pointers.push_back(my_malloc_test(BLOCK_SIZE));
    }
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        my_free_test(pointers[i]);
    }

    auto end_custom = std::chrono::high_resolution_clock::now();
    auto duration_custom = std::chrono::duration_cast<std::chrono::milliseconds>(end_custom - start_custom);

    // --- Results ---
    std::cout << "\n--- Benchmark Results ---" << std::endl;
    std::cout << "System malloc/free took: " << duration_malloc.count() << " ms" << std::endl;
    std::cout << "Custom allocator took:   " << duration_custom.count() << " ms" << std::endl;

    if (duration_custom < duration_malloc) {
        double factor = static_cast<double>(duration_malloc.count()) / duration_custom.count();
        std::cout << "\nCustom allocator was " << std::fixed << std::setprecision(2) << factor << "x faster!" << std::endl;
    } else {
        double factor = static_cast<double>(duration_custom.count()) / duration_malloc.count();
        std::cout << "\nCustom allocator was " << std::fixed << std::setprecision(2) << factor << "x slower." << std::endl;
    }

    return 0;
}