#include "PoolAllocator.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>

// --- Platform-specific includes for memory measurement ---
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#include <sstream>
#endif


#include <cassert>

// --- BENCHMARK PARAMETERS (Tune these as needed) ---
// constexpr size_t NUM_ALLOCATIONS_SINGLE = 10000000; // For single-threaded tests
// constexpr size_t NUM_ALLOCATIONS_MULTI = 2000000;  // Per thread for multi-threaded tests
constexpr size_t FIXED_BLOCK_SIZE = 32;
constexpr size_t MAX_RANDOM_SIZE = 128;

// =================================================================================
// MEMORY MEASUREMENT HELPERS
// =================================================================================

struct ProcessMemoryInfo {
    size_t rss;   // Physical Memory (Resident Set Size)
    size_t vsize; // Virtual Memory
};

#if defined(__APPLE__)
ProcessMemoryInfo getMemoryInfo() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) != KERN_SUCCESS) {
        return {0, 0};
    }
    return {info.resident_size, info.virtual_size};
}
#elif defined(__linux__)
ProcessMemoryInfo getMemoryInfo() {
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return {0, 0};
    }
    std::string line;
    size_t vsize = 0, rss = 0;
    while (std::getline(status_file, line)) {
        std::stringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "VmSize:") {
            ss >> vsize;
        } else if (key == "VmRSS:") {
            ss >> rss;
        }
    }
    return {rss * 1024, vsize * 1024}; // Values are in KB, convert to bytes
}
#else
ProcessMemoryInfo getMemoryInfo() {
    // Unsupported platform
    return {0, 0};
}
#endif

// =================================================================================
// BENCHMARK IMPLEMENTATIONS
// =================================================================================

/**
 * BENCHMARK 1: Single-thread, single-size throughput test.
 */
long long benchmark_single_size(bool useCustomAllocator, size_t num_allocations) {
    // std::cout << "\n--- Benchmark: Single-Size Throughput ---" << std::endl;
    std::vector<void*> pointers;
    pointers.reserve(num_allocations);
    MemoryPool<FIXED_BLOCK_SIZE> my_allocator;

    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_allocations; ++i) {
        if (useCustomAllocator) {
            pointers.push_back(my_allocator.allocate());
        } else {
            pointers.push_back(malloc(FIXED_BLOCK_SIZE));
        }
    }
    for (void* p : pointers) {
        if (useCustomAllocator) {
            my_allocator.deallocate(p);
        } else {
            free(p);
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    // std::cout << "Total Time: " << duration.count() << " ms" << std::endl;
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
}

/**
 * BENCHMARK 2: Single-thread, random-size throughput test.
 */
long long benchmark_random_size(bool useCustomAllocator, size_t num_allocations, PoolAllocator& my_allocator) {
    // std::cout << "\n--- Benchmark: Random-Size Throughput ---" << std::endl;
    std::vector<void*> pointers;
    pointers.reserve(num_allocations);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, MAX_RANDOM_SIZE);

    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_allocations; ++i) {
        size_t size = distrib(gen);
        if (useCustomAllocator) {
            pointers.push_back(my_allocator.allocate(size));
        } else {
            pointers.push_back(malloc(size));
        }
    }
    for (void* p : pointers) {
        if (useCustomAllocator) {
            my_allocator.deallocate(p);
        } else {
            free(p);
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
}

// Worker function for the multi-threaded test
void multi_thread_worker(PoolAllocator* allocator, size_t size, bool useCustom, size_t num_allocations) {
    std::vector<void*> pointers;
    pointers.reserve(num_allocations);
    for (size_t i = 0; i < num_allocations; ++i) {
        if (useCustom) {
            pointers.push_back(allocator->allocate(size));
        } else {
            pointers.push_back(malloc(size));
        }
    }
    for (void* p : pointers) {
        if (useCustom) {
            allocator->deallocate(p);
        } else {
            free(p);
        }
    }
}

/**
 * BENCHMARK 3: Multi-threaded contention test.
 */
long long benchmark_multi_thread(bool useCustomAllocator, size_t num_allocations, PoolAllocator& my_allocator) {
    // std::cout << "\n--- Benchmark: Multi-Threaded Contention ---" << std::endl;
    const unsigned int num_threads = std::thread::hardware_concurrency();
    // std::cout << "Using " << num_threads << " threads." << std::endl;

    std::vector<std::thread> threads;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, PoolAllocator::POOL_SIZES.size() - 1);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    for (unsigned int i = 0; i < num_threads; ++i) {
        // Each thread works on a different, fixed pool size
        size_t size = PoolAllocator::POOL_SIZES[distrib(gen)];
        threads.emplace_back(multi_thread_worker, &my_allocator, size, useCustomAllocator, num_allocations);
    }
    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    return duration.count();
}

long long run_benchmark(bool useCustom, size_t num_alloc_per_thread, PoolAllocator& my_allocator) {
    const unsigned int num_threads = std::thread::hardware_concurrency();
    // PoolAllocator my_allocator;
    std::vector<std::thread> threads;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, PoolAllocator::POOL_SIZES.size() - 1);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    for (unsigned int i = 0; i < num_threads; ++i) {
        size_t size = PoolAllocator::POOL_SIZES[distrib(gen)];
        threads.emplace_back(multi_thread_worker, &my_allocator, size, useCustom, num_alloc_per_thread);
    }
    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
}

/**
 * BENCHMARK 4: Memory footprint analysis.
 */
// void benchmark_memory(bool useCustomAllocator) {
//     std::cout << "\n--- Benchmark: Memory Footprint ---" << std::endl;
    
//     size_t baseline_vsize = getMemoryInfo().vsize;

//     PoolAllocator my_allocator;
//     std::vector<void*> pointers;
//     pointers.reserve(NUM_ALLOCATIONS_MULTI); // Use a smaller number for memory tests

//     for (size_t i = 0; i < NUM_ALLOCATIONS_MULTI; ++i) {
//         char* p;
//         if (useCustomAllocator) {
//             p = static_cast<char*>(my_allocator.allocate(FIXED_BLOCK_SIZE));
//         } else {
//             p = static_cast<char*>(malloc(FIXED_BLOCK_SIZE));
//         }
//         if (p) { p[0] = 1; }
//         pointers.push_back(p);
//     }
    
//     size_t peak_vsize = getMemoryInfo().vsize;
    
//     // Cleanup
//     for (void* p : pointers) {
//         if (useCustomAllocator) {
//             my_allocator.deallocate(p);
//         } else {
//             free(p);
//         }
//     }

//     size_t delta_vsize = peak_vsize - baseline_vsize;
//     std::cout << "Peak Virtual Mem Increase: " << delta_vsize / 1024 << " KB" << std::endl;
// }

// =================================================================================
// MAIN FUNCTION (Argument Parser)
// =================================================================================


int main(int argc, char* argv[]) {
    // std::vector<size_t> allocation_counts = {};

    // const std::vector<size_t> allocation_counts = {
    //     1000, 10000, 50000, 100000, 250000, 500000, 1000000
    // };

    // for (size_t i = 500000; i < 2000000; i += 100000) {
    //     allocation_counts.push_back(i);
    // }

    // for (size_t i = 0; i < 50000; i += 1000) {
    //     allocation_counts.push_back(i);
    // }
    
    // Number of times to run each test to get a stable median.
    // const int num_runs_per_test = 3;

    // // Print the CSV header.
    // std::cout << "allocator_type,benchmark_type,num_allocations,time_ms" << std::endl;

    // // Loop through each allocation count.
    // for (const size_t count : allocation_counts) {
    //     std::vector<long long> system_times;
    //     std::vector<long long> custom_times;

    //     // Run each test multiple times.
    //     for (int i = 0; i < num_runs_per_test; ++i) {
    //         system_times.push_back(benchmark_single_size(false, count));
    //         custom_times.push_back(benchmark_single_size(true, count));
    //     }

    //     // Calculate the median time for each.
    //     std::sort(system_times.begin(), system_times.end());
    //     std::sort(custom_times.begin(), custom_times.end());
    //     long long system_median = system_times[num_runs_per_test / 2];
    //     long long custom_median = custom_times[num_runs_per_test / 2];
        
    //     // Print the results for this allocation count in CSV format.
    //     std::cout << "system,single_size," << count << "," << system_median << std::endl;
    //     std::cout << "custom,single_size," << count << "," << custom_median << std::endl;
    // }




    // PoolAllocator my_allocator;
    // std::cout << "allocator_type,benchmark_type,num_allocations,time_ms" << std::endl;

    // // Loop through each allocation count.
    // for (const size_t count : allocation_counts) {
    //     std::vector<long long> system_times;
    //     std::vector<long long> custom_times;

    //     // Run each test multiple times.
    //     for (int i = 0; i < num_runs_per_test; ++i) {
    //         system_times.push_back(benchmark_random_size(false, count, my_allocator));
    //         custom_times.push_back(benchmark_random_size(true, count, my_allocator));
    //     }

    //     // Calculate the median time for each.
    //     std::sort(system_times.begin(), system_times.end());
    //     std::sort(custom_times.begin(), custom_times.end());
    //     long long system_median = system_times[num_runs_per_test / 2];
    //     long long custom_median = custom_times[num_runs_per_test / 2];
        
    //     // Print the results for this allocation count in CSV format.
    //     std::cout << "system,random_size," << count << "," << system_median << std::endl;
    //     std::cout << "custom,random_size," << count << "," << custom_median << std::endl;
    // }



    // PoolAllocator my_allocator;
    // std::cout << "allocator_type,benchmark_type,num_allocations,time_ms" << std::endl;

    // // Loop through each allocation count.
    // for (const size_t count : allocation_counts) {
    //     std::vector<long long> system_times;
    //     std::vector<long long> custom_times;

    //     // Run each test multiple times.
    //     for (int i = 0; i < num_runs_per_test; ++i) {
    //         system_times.push_back(benchmark_multi_thread(false, count, my_allocator));
    //         custom_times.push_back(benchmark_multi_thread(true, count, my_allocator));
    //     }

    //     // Calculate the median time for each.
    //     std::sort(system_times.begin(), system_times.end());
    //     std::sort(custom_times.begin(), custom_times.end());
    //     long long system_median = system_times[num_runs_per_test / 2];
    //     long long custom_median = custom_times[num_runs_per_test / 2];
        
    //     // Print the results for this allocation count in CSV format.
    //     std::cout << "system,multi_thread," << count << "," << system_median << std::endl;
    //     std::cout << "custom,multi_thread," << count << "," << custom_median << std::endl;
    // }


    //     const std::vector<size_t> allocation_counts = {
    //     10000, 30000, 50000, 100000, 250000, 500000, 1000000
    // };


    // const unsigned int num_threads = std::thread::hardware_concurrency();
    // PoolAllocator my_allocator;
    // std::cout << "allocator_type,benchmark_type,num_allocations,throughput_M_ops_per_sec" << std::endl;

    // for (const size_t count : allocation_counts) {
    //     // Run Custom Allocator Test
    //     long long custom_time_ms = run_benchmark(true, count, my_allocator);
    //     double custom_throughput = (static_cast<double>(count * num_threads) / (custom_time_ms / 1000.0)) / 1000000.0;
    //     std::cout << "custom,multi_thread," << count << "," << custom_throughput << std::endl;
    //     // Run System Malloc Test
    //     long long system_time_ms = run_benchmark(false, count, my_allocator);
    //     double system_throughput = (static_cast<double>(count * num_threads) / (system_time_ms / 1000.0)) / 1000000.0;
    //     std::cout << "system,multi_thread," << count << "," << system_throughput << std::endl;

    // }

    // std::cout << "Benchmark for random sized memory" << std::endl;
    // std::cout << std::endl;
    // std::cout << std::endl;
    // std::cout << "Custom" << std::endl;
    // benchmark_random_size(true, 5000000);
    // std::cout << std::endl;
    // std::cout << std::endl;
    // std::cout << "System" << std::endl;
    // benchmark_random_size(false, 5000000);


    // std::cout << "Benchmark for multi-threading" << std::endl;
    // std::cout << std::endl;
    // std::cout << std::endl;
    // std::cout << "Custom" << std::endl;
    // benchmark_multi_thread(true, 100000);
    // std::cout << std::endl;
    // std::cout << std::endl;
    // std::cout << "System" << std::endl;
    // benchmark_multi_thread(false, 100000);

    // benchmark_memory(use_custom_allocator);





    const unsigned int num_threads = std::thread::hardware_concurrency();
    const size_t num_alloc_per_thread = 5000000;
    std::cout << "--- Starting Final Multi-threaded Benchmark ---" << std::endl;
    std::cout << "Usiddng " << num_threads << " threads, each on a unique pool size." << std::endl;

    // --- Test System Malloc ---
    {
        std::cout << "\nTesting System Malloc..." << std::endl;
        PoolAllocator dummy_allocator; // Only needed for the worker signature
        std::vector<std::thread> threads;
        auto start_time = std::chrono::high_resolution_clock::now();
        for (unsigned int i = 0; i < num_threads; ++i) {
            // Assign each thread a unique size class
            size_t size = PoolAllocator::POOL_SIZES[i % PoolAllocator::POOL_SIZES.size()];
            threads.emplace_back(multi_thread_worker, &dummy_allocator, size, false, num_alloc_per_thread);
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
            // Assign each thread a unique size class
            size_t size = PoolAllocator::POOL_SIZES[i % PoolAllocator::POOL_SIZES.size()];
            threads.emplace_back(multi_thread_worker, &my_allocator, size, true, num_alloc_per_thread);
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