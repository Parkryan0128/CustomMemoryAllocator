#include <iostream>
#include <vector>
#include <cstdlib>
#include "MemoryPool.hpp"
#include <chrono>
#include <iomanip>
#include <string>
#include <mach/mach.h>
#include <random> 


#include "PoolAllocator.hpp"
#include <cassert> // For assert()


#if defined(__APPLE__)
#include <malloc/malloc.h>
#define GET_ALLOC_SIZE(p) malloc_size(p)
#else
#define GET_ALLOC_SIZE(p) 0
#endif





constexpr size_t SMALL_BLOCK_SIZE = 8;
constexpr size_t NUM_ALLOCATIONS = 5000000;
constexpr size_t MAX_ALLOC_SIZE = 511;

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
void system_malloc_time() {
   std::cout << "\n--- Testing System Malloc (Full Cycle) ---" << std::endl;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // Start the timer *before* the entire workload.
    auto start_time = std::chrono::high_resolution_clock::now();

    // --- WORKLOAD START ---
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        char* p = static_cast<char*>(malloc(SMALL_BLOCK_SIZE));
        if (p) { p[0] = 1; }
        pointers.push_back(p);
    }
    
    long long sum = 0; // Read the memory to ensure it's not optimized away.
    for (void* p : pointers) {
        if (p) sum += static_cast<char*>(p)[0];
    }

    for (void* p : pointers) {
        free(p);
    }
    // --- WORKLOAD END ---

    // Stop the timer *after* the entire workload is complete.
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Total time for system malloc: " << duration.count() << " ms" << std::endl;
    if (sum != NUM_ALLOCATIONS) std::cout << "Sum mismatch!" << std::endl;
}

void system_malloc_time2() {
    std::cout << "\n--- System Malloc Time Analysis ---" << std::endl;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    std::random_device rd;

    // 2. Create a random number engine and seed it with the random_device.
    //    std::mt19937 is a high-quality engine (Mersenne Twister).
    std::mt19937 gen(rd());

    // 3. Create a distribution that will map the engine's output to your desired range [0, 24].
    std::uniform_int_distribution<> distrib(0, 24);

    // Generate and print a random number.
    // int random_number = distrib(gen);
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        pointers.push_back(malloc(distrib(gen)));
    }
    auto end_time = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    for (void* p : pointers) {
        free(p);
    }
    std::cout << "Allocation Time for system malloc:           " << duration.count() << " ms" << std::endl;
}

void test_custom_time() {
   std::cout << "\n--- Testing Custom Allocator (Full Cycle) ---" << std::endl;
    MemoryPool<SMALL_BLOCK_SIZE> my_pool;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // Start the timer.
    auto start_time = std::chrono::high_resolution_clock::now();

    // --- WORKLOAD START ---
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        char* p = static_cast<char*>(my_pool.allocate());
        if (p) { p[0] = 1; }
        pointers.push_back(p);
    }
    
    long long sum = 0;
    for (void* p : pointers) {
        if (p) sum += static_cast<char*>(p)[0];
    }

    for (void* p : pointers) {
        my_pool.deallocate(p);
    }
    // --- WORKLOAD END ---

    // Stop the timer.
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Total time for custom malloc: " << duration.count() << " ms" << std::endl;
    if (sum != NUM_ALLOCATIONS) std::cout << "Sum mismatch!" << std::endl;

}


void test_custom_memory() {
    std::cout << "\n--- Testing Custom Allocator ---" << std::endl;
    
    // 1. Measure the baseline virtual memory before creating the pool or vector.
    size_t baseline_vsize = getMemoryInfo().vsize;

    // Create the allocator and vector locally for a clean test.
    MemoryPool<SMALL_BLOCK_SIZE> my_pool;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // Start timing the allocation loop.
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        pointers.push_back(my_pool.allocate());
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
    // if (sum != NUM_ALLOCATIONS) std::cout << "Sum mismatch!" << std::endl;
}



// void test_custom_allocator2() {
//     std::cout << "\n--- Testing Custom Allocator ---" << std::endl;
    
//     // 1. Measure the baseline virtual memory before creating the pool or vector.
//     size_t baseline_vsize = getMemoryInfo().vsize;

//     // Create the allocator and vector locally for a clean test.
//     // SingleSizeAllocator<SMALL_BLOCK_SIZE> my_pool;
//     MyAlloc::PoolAllocator allocator;
//     std::vector<void*> pointers;
//     pointers.reserve(NUM_ALLOCATIONS);

//     // Start timing the allocation loop.
// std::random_device rd;

//     // 2. Create a random number engine and seed it with the random_device.
//     //    std::mt19937 is a high-quality engine (Mersenne Twister).
//     std::mt19937 gen(rd());

//     // 3. Create a distribution that will map the engine's output to your desired range [0, 24].
//     std::uniform_int_distribution<> distrib(0, 24);

//     auto start_time = std::chrono::high_resolution_clock::now();
//     for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {

//         char* p = static_cast<char*>(allocator.allocate(distrib(gen)));
//         if (p) {
//             p[0] = 1; // "Touch" the memory to ensure it's committed by the OS.
//         }
//         pointers.push_back(p);
//     }
    
//     // Create a data dependency by reading the memory, which prevents
//     // the compiler from optimizing away the allocation loop.
//     long long sum = 0;
//     for (void* p : pointers) {
//         if (p) sum += static_cast<char*>(p)[0];
//     }
    
//     auto end_time = std::chrono::high_resolution_clock::now();

//     // 2. Measure the peak virtual memory after all allocations are complete.
//     size_t peak_vsize = getMemoryInfo().vsize;

//     // Clean up all the memory that was allocated.
//     for (void* p : pointers) {
//         allocator.deallocate(p);
//     }
    
//     // 3. Calculate the difference (delta) and print the results.
//     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//     size_t delta_vsize = peak_vsize - baseline_vsize;


//     std::cout << "Allocation Time:           " << duration.count() << " ms" << std::endl;
//     std::cout << "Peak Virtual Mem Increase: " << delta_vsize / 1024 << " KB" << std::endl;
    
//     // This final check ensures the 'sum' calculation is used and not optimized away.
//     if (sum != NUM_ALLOCATIONS) std::cout << "Sum mismatch!" << std::endl;
// }


void test_custom_pool_time() {
    std::cout << "\n--- Testing Custom PoolAllocator (Random Sizes) ---" << std::endl;
    PoolAllocator my_allocator;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 64);

    // --- Start the timer ---
    auto start_time = std::chrono::high_resolution_clock::now();

    // 1. ALLOCATION PHASE
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        const size_t random_size = distrib(gen);
        char* p = static_cast<char*>(my_allocator.allocate(random_size));
        if (p) { p[0] = 1; }
        pointers.push_back(p);
    }
    
    // 2. USE PHASE
    long long sum = 0;
    for (void* p : pointers) {
        if (p) sum += static_cast<char*>(p)[0];
    }

    // 3. DEALLOCATION PHASE
    for (void* p : pointers) {
        my_allocator.deallocate(p);
    }
    
    // --- Stop the timer ---
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Total time for custom malloc: " << duration.count() << " ms" << std::endl;
    if (sum == 0) std::cout << "Warning: sum was zero!" << std::endl; // Use sum
}



void test_system_pool_time() {
    std::cout << "\n--- Testing System Malloc (Random Sizes) ---" << std::endl;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // Setup for generating random numbers in our desired range.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 64);

    // --- Start the timer for the full cycle ---
    auto start_time = std::chrono::high_resolution_clock::now();

    // 1. ALLOCATION PHASE
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        const size_t random_size = distrib(gen);
        char* p = static_cast<char*>(malloc(random_size));
        if (p) { p[0] = 1; } // "Touch" the memory
        pointers.push_back(p);
    }
    
    // 2. USE PHASE (prevents compiler from optimizing away allocations)
    long long sum = 0;
    for (void* p : pointers) {
        if (p) sum += static_cast<char*>(p)[0];
    }

    // 3. DEALLOCATION PHASE
    for (void* p : pointers) {
        free(p);
    }
    
    // --- Stop the timer ---
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Total time for system malloc: " << duration.count() << " ms" << std::endl;
    if (sum == 0) std::cout << "Warning: sum was zero!" << std::endl; // Use sum
}



int main() {
    // system_malloc_memory();
    // system_malloc_time();

    // test_custom_memory();
    // test_custom_time();
    

    test_custom_pool_time();
    test_system_pool_time();

    //     std::cout << "--- Running PoolAllocator Test Suite ---" << std::endl;

    // test_basic_allocation();
    // test_multiple_pools();
    // test_boundary_conditions();
    // test_exhaustion_and_reuse();


    // std::cout << "\n✅ All tests passed!" << std::endl;

    return 0;
}