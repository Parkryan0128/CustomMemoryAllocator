#include <iostream>
#include <vector>
#include <cstdlib>
#include "MemoryPool.hpp"
#include <chrono>
#include <iomanip>
#include <string>
// #include <mach/mach.h>
#include <random> 
#include <thread>


#include "PoolAllocator.hpp"
#include <cassert> // For assert()


#if defined(__APPLE__)
#include <malloc/malloc.h>
#define GET_ALLOC_SIZE(p) malloc_size(p)
#else
#define GET_ALLOC_SIZE(p) 0
#endif





constexpr size_t SMALL_BLOCK_SIZE = 8;
constexpr size_t NUM_ALLOCATIONS = 500000;
constexpr size_t MAX_ALLOC_SIZE = 511;
constexpr size_t MAX_RANDOM_SIZE = 64;
PoolAllocator g_allocator;

void worker_thread() {
    // Each thread will try to allocate 10 small blocks.
    for (int i = 0; i < 10; ++i) {
        void* p = g_allocator.allocate(8);
        if (p == nullptr) {
            std::cout << "[Thread " << std::this_thread::get_id() << "] Allocation failed.\n";
        } else {
            std::cout << "[Thread " << std::this_thread::get_id() << "] Allocation succeeded.\n";
            // In a real program, we would use and then deallocate p.
        }
    }
}

struct ProcessMemoryInfo { size_t rss; size_t vsize; };

// ProcessMemoryInfo getMemoryInfo() {
//     mach_task_basic_info_data_t info;
//     mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
//     if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) != KERN_SUCCESS) {
//         return {0, 0};
//     }
//     return {info.resident_size, info.virtual_size};
// }


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


// void test_custom_memory() {
//     std::cout << "\n--- Testing Custom Allocator ---" << std::endl;
    
//     // 1. Measure the baseline virtual memory before creating the pool or vector.
//     size_t baseline_vsize = getMemoryInfo().vsize;

//     // Create the allocator and vector locally for a clean test.
//     MemoryPool<SMALL_BLOCK_SIZE> my_pool;
//     std::vector<void*> pointers;
//     pointers.reserve(NUM_ALLOCATIONS);

//     // Start timing the allocation loop.
//     auto start_time = std::chrono::high_resolution_clock::now();
//     for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
//         pointers.push_back(my_pool.allocate());
//     }
    
    
//     auto end_time = std::chrono::high_resolution_clock::now();

//     // 2. Measure the peak virtual memory after all allocations are complete.
//     size_t peak_vsize = getMemoryInfo().vsize;

//     // Clean up all the memory that was allocated.
//     for (void* p : pointers) {
//         my_pool.deallocate(p);
//     }
    
//     // 3. Calculate the difference (delta) and print the results.
//     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//     size_t delta_vsize = peak_vsize - baseline_vsize;


//     std::cout << "Allocation Time:           " << duration.count() << " ms" << std::endl;
//     std::cout << "Peak Virtual Mem Increase: " << delta_vsize / 1024 << " KB" << std::endl;
    
//     // This final check ensures the 'sum' calculation is used and not optimized away.
//     // if (sum != NUM_ALLOCATIONS) std::cout << "Sum mismatch!" << std::endl;
// }



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



void test_custom_allocator_time_thread() {
    std::cout << "\n--- Testing Custom PoolAllocator (Random Sizes) ---" << std::endl;
    
    // Create an instance of your allocator.
    PoolAllocator my_allocator;
    
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // Use the same random number generation setup for a fair test.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, MAX_RANDOM_SIZE);

    // --- Start the timer ---
    auto start_time = std::chrono::high_resolution_clock::now();

    // 1. ALLOCATION PHASE
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        const size_t random_size = distrib(gen);
        // Call your allocator's allocate method.
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
        // Call your allocator's deallocate method.
        my_allocator.deallocate(p);
    }
    
    // --- Stop the timer ---
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Total time for custom allocator: " << duration.count() << " ms" << std::endl;
    if (sum == 0) std::cout << "Warning: sum was zero!" << std::endl; // Use sum
}

void test_system_malloc_time() {
    std::cout << "\n--- Testing System Malloc (Random Sizes) ---" << std::endl;
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // Setup for generating random numbers in our desired range.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, MAX_RANDOM_SIZE);

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


// int main() {
    // test_custom_allocator_time_thread();
    // test_system_malloc_time();

    // system_malloc_memory();
    // system_malloc_time();

    // test_custom_memory();
    // test_custom_time();
    

    // test_custom_pool_time();
    // test_system_pool_time();

    //     std::cout << "--- Running PoolAllocator Test Suite ---" << std::endl;

    // test_basic_allocation();
    // test_multiple_pools();
    // test_boundary_conditions();
    // test_exhaustion_and_reuse();


    // std::cout << "\n✅ All tests passed!" << std::endl;
    // std::cout << "--- Starting Multi-threaded Test ---" << std::endl;
    
    // std::vector<std::thread> threads;
    // const int num_threads = 4;

    // for (int i = 0; i < num_threads; ++i) {
    //     threads.emplace_back(worker_thread);
    // }

    // for (auto& t : threads) {
    //     t.join();
    // }

    // std::cout << "--- Test Finished ---" << std::endl;

    // return 0;
// }
constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 500000;

// This worker function now takes a specific size to allocate
void system_malloc_worker(size_t alloc_size) {
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS_PER_THREAD);
    for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; ++i) {
        pointers.push_back(malloc(alloc_size));
    }
    for (void* p : pointers) {
        free(p);
    }
}

// This worker function now takes a specific size to allocate
void custom_allocator_worker(PoolAllocator& allocator, size_t alloc_size) {
    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS_PER_THREAD);
    for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; ++i) {
        pointers.push_back(allocator.allocate(alloc_size));
    }
    for (void* p : pointers) {
        allocator.deallocate(p);
    }
}

int main() {
    const unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "--- Starting Multi-threaded Benchmark (Realistic Workload) ---" << std::endl;
    std::cout << "Using " << num_threads << " threads." << std::endl;
    std::cout << "Allocations per thread: " << NUM_ALLOCATIONS_PER_THREAD << std::endl;

    // --- Generate a set of random (but fixed) tasks for each thread ---
    std::vector<size_t> sizes_for_threads;
    std::random_device rd;
    std::mt19937 gen(rd());
    // Get random pool indices to test contention on different pools
    std::uniform_int_distribution<> distrib(0, PoolAllocator::POOL_SIZES.size() - 1);
    for (unsigned int i = 0; i < num_threads; ++i) {
        sizes_for_threads.push_back(PoolAllocator::POOL_SIZES[distrib(gen)]);
    }

    // --- Test System Malloc ---
    {
        std::cout << "\nTesting System Malloc..." << std::endl;
        std::vector<std::thread> threads;
        auto start_time = std::chrono::high_resolution_clock::now();
        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back(system_malloc_worker, sizes_for_threads[i]);
        }
        for (auto& t : threads) {
            t.join();
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "Total time for system malloc: " << duration.count() << " ms" << std::endl;
    }

    // --- Test Custom Allocator ---
    {
        std::cout << "\nTesting Custom PoolAllocator..." << std::endl;
        PoolAllocator my_allocator;
        std::vector<std::thread> threads;
        auto start_time = std::chrono::high_resolution_clock::now();
        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back(custom_allocator_worker, std::ref(my_allocator), sizes_for_threads[i]);
        }
        for (auto& t : threads) {
            t.join();
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "Total time for custom allocator: " << duration.count() << " ms" << std::endl;
    }

    return 0;
}


// #include "PoolAllocator.hpp"
// #include <iostream>
// #include <vector>
// #include <chrono>
// #include <string>
// #include <thread> // The C++ threading library
// #include <random>

// // --- BENCHMARK PARAMETERS ---
// // Let's use fewer allocations, but run them across many threads.
// constexpr size_t NUM_ALLOCATIONS_PER_THREAD = 1000000;
// constexpr size_t MAX_RANDOM_SIZE = 64;

// /**
//  * @brief The work each thread will do for the system malloc test.
//  */
// void system_malloc_worker() {
//     std::vector<void*> pointers;
//     pointers.reserve(NUM_ALLOCATIONS_PER_THREAD);
//     std::random_device rd;
//     std::mt19937 gen(rd());
//     std::uniform_int_distribution<> distrib(1, MAX_RANDOM_SIZE);

//     for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; ++i) {
//         pointers.push_back(malloc(distrib(gen)));
//     }
//     for (void* p : pointers) {
//         free(p);
//     }
// }

// /**
//  * @brief The work each thread will do for the custom allocator test.
//  * @param allocator A reference to the global custom allocator.
//  */
// void custom_allocator_worker(PoolAllocator& allocator) {
//     std::vector<void*> pointers;
//     pointers.reserve(NUM_ALLOCATIONS_PER_THREAD);
//     std::random_device rd;
//     std::mt19937 gen(rd());
//     std::uniform_int_distribution<> distrib(1, MAX_RANDOM_SIZE);

//     for (size_t i = 0; i < NUM_ALLOCATIONS_PER_THREAD; ++i) {
//         pointers.push_back(allocator.allocate(distrib(gen)));
//     }
//     for (void* p : pointers) {
//         allocator.deallocate(p);
//     }
// }

// int main() {
//     // Determine how many threads to use (based on your CPU cores).
//     const unsigned int num_threads = std::thread::hardware_concurrency();
//     std::cout << "--- Starting Multi-threaded Benchmark ---" << std::endl;
//     std::cout << "Using " << num_threads << " threads." << std::endl;
//     std::cout << "Allocations per thread: " << NUM_ALLOCATIONS_PER_THREAD << std::endl;

//     // --- Test System Malloc ---
//     {
//         std::cout << "\nTesting System Malloc..." << std::endl;
//         std::vector<std::thread> threads;
//         auto start_time = std::chrono::high_resolution_clock::now();
//         for (unsigned int i = 0; i < num_threads; ++i) {
//             threads.emplace_back(system_malloc_worker);
//         }
//         for (auto& t : threads) {
//             t.join();
//         }
//         auto end_time = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//         std::cout << "Total time for system malloc: " << duration.count() << " ms" << std::endl;
//     }

//     // --- Test Custom Allocator ---
//     {
//         std::cout << "\nTesting Custom PoolAllocator..." << std::endl;
//         PoolAllocator my_allocator; // Create the allocator
//         std::vector<std::thread> threads;
//         auto start_time = std::chrono::high_resolution_clock::now();
//         for (unsigned int i = 0; i < num_threads; ++i) {
//             threads.emplace_back(custom_allocator_worker, std::ref(my_allocator));
//         }
//         for (auto& t : threads) {
//             t.join();
//         }
//         auto end_time = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//         std::cout << "Total time for custom allocator: " << duration.count() << " ms" << std::endl;
//     }

//     return 0;
// }