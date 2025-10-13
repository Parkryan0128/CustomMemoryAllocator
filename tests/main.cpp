#include "PoolAllocator.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <functional>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#include <sstream>
#endif


struct ProcessMemoryInfo { size_t rss; size_t vsize; };
ProcessMemoryInfo getMemoryInfo() { return {0,0}; }


constexpr size_t FIXED_BLOCK_SIZE = 32;
constexpr size_t MAX_RANDOM_SIZE = 128;

long long benchmark_single_size(bool useCustomAllocator, size_t num_allocations) {
    std::vector<void*> pointers;
    pointers.reserve(num_allocations);
    
    // This benchmark specifically tests the raw, single-threaded MemoryPool
    MemoryPool<FIXED_BLOCK_SIZE> my_pool;

    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_allocations; ++i) {
        if (useCustomAllocator) {
            pointers.push_back(my_pool.allocate());
        } else {
            pointers.push_back(malloc(FIXED_BLOCK_SIZE));
        }
    }
    for (void* p : pointers) {
        if (useCustomAllocator) {
            my_pool.deallocate(p);
        } else {
            free(p);
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
}

long long benchmark_random_size(bool useCustomAllocator, size_t num_allocations, PoolAllocator& my_allocator) {
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
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
}

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

long long benchmark_multi_thread(bool useCustomAllocator, size_t num_allocations_per_thread, PoolAllocator& my_allocator) {
    const unsigned int num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, PoolAllocator::POOL_SIZES.size() - 1);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    for (unsigned int i = 0; i < num_threads; ++i) {
        size_t size = PoolAllocator::POOL_SIZES[distrib(gen)];
        threads.emplace_back(multi_thread_worker, &my_allocator, size, useCustomAllocator, num_allocations_per_thread);
    }
    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
}

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void run_console_benchmarks() {
    const size_t num_alloc_single = 5000000;
    const size_t num_alloc_multi_per_thread = 1000000;
    PoolAllocator my_allocator; // Single instance for all relevant tests

    print_header("Benchmark 1: Single-Thread, Fixed-Size (MemoryPool only)");
    long long custom_single_time = benchmark_single_size(true, num_alloc_single);
    long long system_single_time = benchmark_single_size(false, num_alloc_single);
    std::cout << "Custom MemoryPool Time: " << std::setw(5) << custom_single_time << " ms\n";
    std::cout << "System Malloc Time:     " << std::setw(5) << system_single_time << " ms\n";

    print_header("Benchmark 2: Single-Thread, Random-Size (PoolAllocator)");
    long long custom_random_time = benchmark_random_size(true, num_alloc_single, my_allocator);
    long long system_random_time = benchmark_random_size(false, num_alloc_single, my_allocator);
    std::cout << "Custom Allocator Time:  " << std::setw(5) << custom_random_time << " ms\n";
    std::cout << "System Malloc Time:     " << std::setw(5) << system_random_time << " ms\n";

    const unsigned int num_threads = std::thread::hardware_concurrency();
    print_header("Benchmark 3: Multi-Threaded Contention (" + std::to_string(num_threads) + " threads)");
    long long custom_multi_time = benchmark_multi_thread(true, num_alloc_multi_per_thread, my_allocator);
    long long system_multi_time = benchmark_multi_thread(false, num_alloc_multi_per_thread, my_allocator);
    std::cout << "Custom Allocator Time:  " << std::setw(5) << custom_multi_time << " ms\n";
    std::cout << "System Malloc Time:     " << std::setw(5) << system_multi_time << " ms\n";
    std::cout << std::string(60, '=') << "\n";
}

void generate_plot_data() {
    PoolAllocator my_allocator; // Single instance for all relevant tests
    const std::vector<size_t> allocation_counts = {
        10000, 50000, 100000, 250000, 500000, 1000000, 2000000
    };
    const int num_runs_per_test = 3;

    std::cout << "--- Generating CSV data for plotting ---\n";

    // File 1: Single Size (results.csv)
    std::cout << "Generating results.csv for single-size (MemoryPool) benchmark..." << std::endl;
    std::ofstream file1("results.csv");
    file1 << "allocator_type,benchmark_type,num_allocations,time_ms\n";
    for (const size_t count : allocation_counts) {
        std::vector<long long> system_times, custom_times;
        for (int i = 0; i < num_runs_per_test; ++i) {
            system_times.push_back(benchmark_single_size(false, count));
            custom_times.push_back(benchmark_single_size(true, count));
        }
        std::sort(system_times.begin(), system_times.end());
        std::sort(custom_times.begin(), custom_times.end());
        file1 << "system,single_size," << count << "," << system_times[num_runs_per_test / 2] << "\n";
        file1 << "custom,single_size," << count << "," << custom_times[num_runs_per_test / 2] << "\n";
    }
    file1.close();

    // File 2: Random Size (results2.csv)
    std::cout << "Generating results2.csv for random-size (PoolAllocator) benchmark..." << std::endl;
    std::ofstream file2("results2.csv");
    file2 << "allocator_type,benchmark_type,num_allocations,time_ms\n";
    for (const size_t count : allocation_counts) {
        std::vector<long long> system_times, custom_times;
        for (int i = 0; i < num_runs_per_test; ++i) {
            system_times.push_back(benchmark_random_size(false, count, my_allocator));
            custom_times.push_back(benchmark_random_size(true, count, my_allocator));
        }
        std::sort(system_times.begin(), system_times.end());
        std::sort(custom_times.begin(), custom_times.end());
        file2 << "system,random_size," << count << "," << system_times[num_runs_per_test / 2] << "\n";
        file2 << "custom,random_size," << count << "," << custom_times[num_runs_per_test / 2] << "\n";
    }
    file2.close();

    // File 3: Multi-Thread Throughput (results3.csv)
    std::cout << "Generating results3.csv for multi-threaded (PoolAllocator) benchmark..." << std::endl;
    std::ofstream file3("results3.csv");
    const unsigned int num_threads = std::thread::hardware_concurrency();
    file3 << "allocator_type,benchmark_type,num_allocations,throughput_M_ops_per_sec\n";
    for (const size_t count : allocation_counts) {
        long long custom_time_ms = benchmark_multi_thread(true, count, my_allocator);
        double custom_throughput = (custom_time_ms > 0) ? (static_cast<double>(count * num_threads * 2) / (custom_time_ms / 1000.0)) / 1000000.0 : 0;
        file3 << "custom,multi_thread," << count << "," << custom_throughput << "\n";

        long long system_time_ms = benchmark_multi_thread(false, count, my_allocator);
        double system_throughput = (system_time_ms > 0) ? (static_cast<double>(count * num_threads * 2) / (system_time_ms / 1000.0)) / 1000000.0 : 0;
        file3 << "system,multi_thread," << count << "," << system_throughput << "\n";
    }
    file3.close();

    std::cout << "\nCSV generation complete.\n";
}

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " [command]\n\n"
              << "Commands:\n"
              << "  benchmark   Run a quick comparison and print results to the console.\n"
              << "  plot        Generate CSV files (results.csv, results2.csv, results3.csv) for plotting.\n";
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "benchmark") {
        run_console_benchmarks();
    } else if (command == "plot") {
        generate_plot_data();
    } else {
        std::cerr << "Error: Unknown command '" << command << "'\n\n";
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}