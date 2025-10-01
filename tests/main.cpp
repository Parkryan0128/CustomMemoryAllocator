#include <iostream>
#include <vector>
#include <cstdlib>
#include "SingleSizeAllocator.hpp"
#include <chrono>
#include <iomanip>
#include <string>
#include <mach/mach.h>



#if defined(__APPLE__)
#include <malloc/malloc.h>
#define GET_ALLOC_SIZE(p) malloc_size(p)
#else
#define GET_ALLOC_SIZE(p) 0
#endif

constexpr size_t SMALL_BLOCK_SIZE = 8;
constexpr size_t NUM_ALLOCATIONS = 5000000;

struct ProcessMemoryInfo { size_t rss; size_t vsize; };

ProcessMemoryInfo getMemoryInfo() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) != KERN_SUCCESS) {
        return {0, 0};
    }
    return {info.resident_size, info.virtual_size};
}


void system_malloc_memory() {
    std::cout << "\n--- System Malloc Analysis ---" << std::endl;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);


    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        pointers.push_back(malloc(SMALL_BLOCK_SIZE));
    }

    size_t total_usable_size = 0;
    for (void* p : pointers) {
        total_usable_size += GET_ALLOC_SIZE(p);
    }
    
    // Also add the memory for the vector itself!
    size_t vector_overhead = NUM_ALLOCATIONS * sizeof(void*);
    size_t total_footprint_best_case = total_usable_size + vector_overhead;

    std::cout << "Total usable block size (from malloc's internal report): " 
              << total_usable_size / 1024 << " KB" << std::endl;
    std::cout << "Vector overhead: " << vector_overhead / 1024 << " KB" << std::endl;
    std::cout << "Total Best-Case Footprint: " << total_footprint_best_case / 1024 << " KB" << std::endl;

    for (void* p : pointers) {
        free(p);

    }
}


void test_custom_allocator() {
    std::cout << "\n--- Testing Custom Allocator ---" << std::endl;
    
    // 1. Measure the baseline virtual memory before creating the pool or vector.
    size_t baseline_vsize = getMemoryInfo().vsize;

    // Create the allocator and vector locally for a clean test.
    SingleSizeAllocator<SMALL_BLOCK_SIZE> my_pool;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // Start timing the allocation loop.
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {

        char* p = static_cast<char*>(my_pool.allocate());
        if (p) {
            p[0] = 1; // "Touch" the memory to ensure it's committed by the OS.
        }
        pointers.push_back(p);
    }
    
    // Create a data dependency by reading the memory, which prevents
    // the compiler from optimizing away the allocation loop.
    long long sum = 0;
    for (void* p : pointers) {
        if (p) sum += static_cast<char*>(p)[0];
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();

    // 2. Measure the peak virtual memory after all allocations are complete.
    size_t peak_vsize = getMemoryInfo().vsize;

    // Clean up all the memory that was allocated.
    for (void* p : pointers) {
        my_pool.deallocate(p);
    }
    
    // 3. Calculate the difference (delta) and print the results.
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    size_t delta_vsize = peak_vsize - baseline_vsize;


    std::cout << "Allocation Time:           " << duration.count() << " ms" << std::endl;
    std::cout << "Peak Virtual Mem Increase: " << delta_vsize / 1024 << " KB" << std::endl;
    
    // This final check ensures the 'sum' calculation is used and not optimized away.
    if (sum != NUM_ALLOCATIONS) std::cout << "Sum mismatch!" << std::endl;
}

int main() {
    test_custom_allocator();
    system_malloc_memory();
    return 0;
}

