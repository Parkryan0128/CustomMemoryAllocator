#include "FixedBlockAllocator.hpp"
#include "workload_common.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr size_t kBlockSize = 32;

// Sink used to defeat dead-code elimination: without consuming the allocated
// memory the optimizer is free to delete an alloc/free pair entirely (which
// makes malloc appear to take 0 ms). Both allocators pay the same touch cost.
std::atomic<unsigned long long> g_sink{0};

// Forces the compiler to treat the pointer as escaped so it cannot elide the
// surrounding malloc/free pair (otherwise malloc benchmarks measure nothing).
inline void do_not_optimize(void* p) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "r,m"(p) : "memory");
#else
    g_sink.fetch_add(reinterpret_cast<uintptr_t>(p) & 1U, std::memory_order_relaxed);
#endif
}

inline void touch_block(void* block, size_t i) {
    auto* bytes = static_cast<unsigned char*>(block);
    bytes[0] = static_cast<unsigned char>(i);
    bytes[kBlockSize - 1] = static_cast<unsigned char>(i >> 8);
}

inline unsigned long long read_block(void* block) {
    const auto* bytes = static_cast<const unsigned char*>(block);
    return static_cast<unsigned long long>(bytes[0]) + bytes[kBlockSize - 1];
}

enum class Workload {
    Interleaved,
    Batch,
    RandomMix,
};

enum class Threading {
    Single,
    Multi,
};

using Clock = std::chrono::high_resolution_clock;

template <typename Fn>
long long measure_ms(Fn fn) {
    const auto start = Clock::now();
    fn();
    const auto end = Clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

unsigned int default_thread_count() {
    const unsigned int hw = std::thread::hardware_concurrency();
    return hw > 0 ? hw : 4U;
}

constexpr Workload kAllWorkloads[] = {
    Workload::Interleaved,
    Workload::Batch,
    Workload::RandomMix,
};

// Deterministic mix: ~50% alloc / ~50% free when live is non-empty; always alloc when empty.
// Use xor'd hash bits for the decision — (salt & 1) correlates with op parity and collapses
// to a 0/1 live toggle instead of a real mix.
template <typename Alloc, typename Free>
unsigned long long run_random_mix(size_t operations,
                                  unsigned int seed,
                                  Alloc alloc,
                                  Free free_fn) {
    std::vector<void*> live;
    live.reserve(64);
    unsigned long long checksum = 0;

    for (size_t op = 0; op < operations; ++op) {
        const size_t salt = workload::random_mix_salt(op, seed);
        if (workload::random_mix_should_alloc(live.size(), salt)) {
            void* block = alloc();
            do_not_optimize(block);
            touch_block(block, op);
            live.push_back(block);
        } else {
            const size_t index = salt % live.size();
            void* block = live[index];
            checksum += read_block(block);
            live[index] = live.back();
            live.pop_back();
            free_fn(block);
        }
    }

    for (void* block : live) {
        checksum += read_block(block);
        free_fn(block);
    }
    return checksum;
}

template <typename Alloc, typename Free>
unsigned long long run_workload(Workload workload,
                                size_t iterations,
                                unsigned int seed,
                                Alloc alloc,
                                Free free_fn) {
    unsigned long long checksum = 0;

    if (workload == Workload::Interleaved) {
        for (size_t i = 0; i < iterations; ++i) {
            void* block = alloc();
            do_not_optimize(block);
            touch_block(block, i);
            checksum += read_block(block);
            free_fn(block);
        }
        return checksum;
    }

    if (workload == Workload::RandomMix) {
        return run_random_mix(iterations, seed, alloc, free_fn);
    }

    std::vector<void*> blocks;
    blocks.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        void* block = alloc();
        do_not_optimize(block);
        touch_block(block, i);
        blocks.push_back(block);
    }
    for (void* block : blocks) {
        checksum += read_block(block);
        free_fn(block);
    }
    return checksum;
}

void run_single_custom(Workload workload, size_t iterations) {
    cma::FixedBlockAllocator<kBlockSize> allocator;
    const unsigned long long checksum = run_workload(
        workload, iterations, 0U, [&]() { return allocator.allocate(); },
        [&](void* p) { allocator.deallocate(p); });
    g_sink.fetch_add(checksum, std::memory_order_relaxed);
}

void run_single_malloc(Workload workload, size_t iterations) {
    const unsigned long long checksum = run_workload(
        workload, iterations, 0U, [&]() { return std::malloc(kBlockSize); },
        [&](void* p) { std::free(p); });
    g_sink.fetch_add(checksum, std::memory_order_relaxed);
}

void run_multi_custom(Workload workload, size_t iterations_per_thread, unsigned int thread_count) {
    // Each thread owns a private allocator. A fixed-block pool is typically used
    // per-thread/per-subsystem, which lets the design scale without lock
    // contention - the fair counterpart to the process-wide system malloc.
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (unsigned int t = 0; t < thread_count; ++t) {
        threads.emplace_back([workload, iterations_per_thread, t]() {
            cma::FixedBlockAllocator<kBlockSize> allocator;
            const unsigned long long checksum = run_workload(
                workload, iterations_per_thread, t, [&]() { return allocator.allocate(); },
                [&](void* p) { allocator.deallocate(p); });
            allocator.flush_local_thread_cache();
            g_sink.fetch_add(checksum, std::memory_order_relaxed);
        });
    }

    for (std::thread& thread : threads) {
        thread.join();
    }
}

void run_multi_malloc(Workload workload, size_t iterations_per_thread, unsigned int thread_count) {
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (unsigned int t = 0; t < thread_count; ++t) {
        threads.emplace_back([workload, iterations_per_thread, t]() {
            const unsigned long long checksum = run_workload(
                workload, iterations_per_thread, t, [&]() { return std::malloc(kBlockSize); },
                [&](void* p) { std::free(p); });
            g_sink.fetch_add(checksum, std::memory_order_relaxed);
        });
    }

    for (std::thread& thread : threads) {
        thread.join();
    }
}

long long benchmark(bool use_custom,
                    Threading threading,
                    Workload workload,
                    size_t iterations,
                    unsigned int thread_count) {
    if (threading == Threading::Single) {
        if (use_custom) {
            return measure_ms([&]() { run_single_custom(workload, iterations); });
        }
        return measure_ms([&]() { run_single_malloc(workload, iterations); });
    }

    const size_t iterations_per_thread = iterations / thread_count;
    if (use_custom) {
        return measure_ms(
            [&]() { run_multi_custom(workload, iterations_per_thread, thread_count); });
    }
    return measure_ms([&]() { run_multi_malloc(workload, iterations_per_thread, thread_count); });
}

// Warms caches/CPU frequency with one untimed run, then reports the median of
// several timed runs so the comparison is stable and order-independent.
long long stable_ms(bool use_custom,
                    Threading threading,
                    Workload workload,
                    size_t iterations,
                    unsigned int thread_count,
                    int runs = 5) {
    benchmark(use_custom, threading, workload, iterations, thread_count);

    std::vector<long long> times;
    times.reserve(runs);
    for (int i = 0; i < runs; ++i) {
        times.push_back(benchmark(use_custom, threading, workload, iterations, thread_count));
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

const char* workload_name(Workload workload) {
    switch (workload) {
    case Workload::Interleaved:
        return "interleaved";
    case Workload::Batch:
        return "batch";
    case Workload::RandomMix:
        return "random_mix";
    }
    return "unknown";
}

const char* threading_name(Threading threading) {
    return threading == Threading::Single ? "single" : "multi";
}

std::string benchmark_type(Threading threading, Workload workload) {
    return std::string(threading_name(threading)) + "_" + workload_name(workload);
}

void print_result_row(const std::string& label, long long custom_ms, long long malloc_ms) {
    const double ratio = malloc_ms > 0 ? static_cast<double>(custom_ms) / malloc_ms : 0.0;
    std::cout << std::left << std::setw(28) << label << " custom: " << std::setw(6) << custom_ms
              << " ms  malloc: " << std::setw(6) << malloc_ms << " ms  ratio: " << std::fixed
              << std::setprecision(2) << ratio << "x\n";
}

void run_console_benchmark() {
    const size_t single_iterations = 5'000'000;
    const unsigned int thread_count = default_thread_count();
    const size_t multi_iterations = single_iterations;

    std::cout << "\n" << std::string(72, '=') << "\n";
    std::cout << "  Fixed-Size Allocator Benchmark (" << kBlockSize << " bytes)\n";
    std::cout << std::string(72, '=') << "\n\n";

    std::cout << "Single-thread (" << single_iterations << " operations)\n";
    std::cout << std::string(72, '-') << "\n";

    for (Workload workload : kAllWorkloads) {
        const long long custom_ms =
            stable_ms(true, Threading::Single, workload, single_iterations, 1);
        const long long malloc_ms =
            stable_ms(false, Threading::Single, workload, single_iterations, 1);
        print_result_row(benchmark_type(Threading::Single, workload), custom_ms, malloc_ms);
    }

    std::cout << "\nMulti-thread (" << thread_count << " threads, " << multi_iterations
              << " total operations)\n";
    std::cout << std::string(72, '-') << "\n";

    for (Workload workload : kAllWorkloads) {
        const long long custom_ms =
            stable_ms(true, Threading::Multi, workload, multi_iterations, thread_count);
        const long long malloc_ms =
            stable_ms(false, Threading::Multi, workload, multi_iterations, thread_count);
        print_result_row(benchmark_type(Threading::Multi, workload), custom_ms, malloc_ms);
    }

    std::cout << std::string(72, '=') << "\n";
}

void generate_plot_data() {
    const std::vector<size_t> allocation_counts = {
        10000, 50000, 100000, 250000, 500000, 1000000, 2000000,
    };
    const unsigned int thread_count = default_thread_count();
    const int num_runs_per_test = 3;

    std::cout << "--- Generating results.csv ---\n";

    std::ofstream file("results.csv");
    file << "allocator_type,benchmark_type,num_allocations,time_ms\n";

    for (const size_t count : allocation_counts) {
        for (Threading threading : {Threading::Single, Threading::Multi}) {
            for (Workload workload : kAllWorkloads) {
                const std::string bench_type = benchmark_type(threading, workload);
                const unsigned int threads = threading == Threading::Multi ? thread_count : 1U;

                std::vector<long long> system_times;
                std::vector<long long> custom_times;

                for (int run = 0; run < num_runs_per_test; ++run) {
                    system_times.push_back(
                        benchmark(false, threading, workload, count, threads));
                    custom_times.push_back(benchmark(true, threading, workload, count, threads));
                }

                std::sort(system_times.begin(), system_times.end());
                std::sort(custom_times.begin(), custom_times.end());

                const long long system_median = system_times[num_runs_per_test / 2];
                const long long custom_median = custom_times[num_runs_per_test / 2];

                file << "system," << bench_type << "," << count << "," << system_median << "\n";
                file << "custom," << bench_type << "," << count << "," << custom_median << "\n";
            }
        }
    }

    std::cout << "CSV generation complete.\n";
}

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " [command]\n\n"
              << "Commands:\n"
              << "  benchmark   Compare custom vs malloc (single and multi-thread).\n"
              << "  plot        Generate results.csv for plotting.\n"
              << "  trace       Sample allocator stats to JSON (see: trace --help via missing args).\n";
}

} // namespace

int run_benchmark_cli(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];

    if (command == "benchmark") {
        run_console_benchmark();
    } else if (command == "plot") {
        generate_plot_data();
    } else {
        std::cerr << "Error: Unknown command '" << command << "'\n\n";
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}

#ifndef CMA_NO_MAIN
int main(int argc, char* argv[]) {
    return run_benchmark_cli(argc, argv);
}
#endif
