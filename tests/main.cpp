#include "SingleSizeAllocator.hpp"
#include "PoolAllocator.hpp"
#include "PreConfPoolAllocator.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

// --- Config for Test 1 & 2 ---
constexpr size_t BLOCK_SIZE = 32;
SingleSizeAllocator<BLOCK_SIZE> g_single_pool;
void* single_pool_malloc(size_t size) { return g_single_pool.allocate(); }
void single_pool_free(void* ptr) { g_single_pool.deallocate(ptr); }

// --- Config for Test 3 ---
MyAlloc::PoolAllocator g_pool_allocator;
void* pool_alloc_malloc(size_t size) { return g_pool_allocator.allocate(size); }
void pool_alloc_free(void* ptr) { g_pool_allocator.deallocate(ptr); }

// --- Config for Test 4 ---
MyAlloc::PreconfPoolAllocator g_preconf_allocator;
void* preconf_malloc(size_t size) { return g_preconf_allocator.allocate(size); }
void preconf_free(void* ptr) { g_preconf_allocator.deallocate(ptr); }


constexpr size_t NUM_ALLOCATIONS = 10000000;

int main() {
    std::cout << "--- Allocator Performance Benchmark ---" << std::endl;
    std::cout << "Performing " << NUM_ALLOCATIONS << " allocations of " << BLOCK_SIZE << " bytes each." << std::endl;

    std::vector<void*> pointers;
    pointers.reserve(NUM_ALLOCATIONS);

    // --- Test 1: System malloc/free ---
    std::cout << "\n[1] Testing system malloc()..." << std::endl;
    auto start_malloc = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) pointers.push_back(malloc(BLOCK_SIZE));
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) free(pointers[i]);
    auto end_malloc = std::chrono::high_resolution_clock::now();
    auto duration_malloc = std::chrono::duration_cast<std::chrono::milliseconds>(end_malloc - start_malloc);
    pointers.clear();

    // --- Test 2: SingleSizeAllocator (Original) ---
    std::cout << "[2] Testing SingleSizeAllocator (Original)..." << std::endl;
    auto start_single = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) pointers.push_back(single_pool_malloc(BLOCK_SIZE));
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) single_pool_free(pointers[i]);
    auto end_single = std::chrono::high_resolution_clock::now();
    auto duration_single = std::chrono::duration_cast<std::chrono::milliseconds>(end_single - start_single);
    pointers.clear();

    // --- Test 3: PoolAllocator (On-Demand) ---
    std::cout << "[3] Testing PoolAllocator (On-Demand)..." << std::endl;
    auto start_pool = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) pointers.push_back(pool_alloc_malloc(BLOCK_SIZE));
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) pool_alloc_free(pointers[i]);
    auto end_pool = std::chrono::high_resolution_clock::now();
    auto duration_pool = std::chrono::duration_cast<std::chrono::milliseconds>(end_pool - start_pool);
    pointers.clear();

    // --- Test 4: PreconfPoolAllocator with Headers ---
    std::cout << "[4] Testing PreconfPoolAllocator (with Headers)..." << std::endl;
    auto start_preconf = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        pointers.push_back(preconf_malloc(BLOCK_SIZE));
    }
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        preconf_free(pointers[i]);
    }
    auto end_preconf = std::chrono::high_resolution_clock::now();
    auto duration_preconf = std::chrono::duration_cast<std::chrono::milliseconds>(end_preconf - start_preconf);
    
    // --- Results ---
    std::cout << "\n--- Benchmark Results ---" << std::endl;
    std::cout << "System malloc/free took:             " << duration_malloc.count() << " ms" << std::endl;
    std::cout << "SingleSizeAllocator (Original) took: " << duration_single.count() << " ms" << std::endl;
    std::cout << "PoolAllocator (On-Demand) took:      " << duration_pool.count() << " ms" << std::endl;
    std::cout << "PreconfPoolAllocator (Headers) took: " << duration_preconf.count() << " ms" << std::endl;
    
    // --- Mixed-Size Benchmark Section ---
    std::cout << "\n--- Mixed-Size Benchmark ---" << std::endl;
    constexpr size_t NUM_MIXED_ALLOCATIONS_PER_SIZE = 2500000;
    const std::vector<size_t> mixed_sizes = {8, 16, 24, 32};
    std::cout << "Performing " << NUM_MIXED_ALLOCATIONS_PER_SIZE << " allocations for each size in {8, 16, 24, 32}." << std::endl;

    pointers.reserve(NUM_MIXED_ALLOCATIONS_PER_SIZE * mixed_sizes.size());

    // --- Mixed Test 1: System malloc ---
    auto start_mixed_malloc = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_MIXED_ALLOCATIONS_PER_SIZE; ++i) {
        for (const auto& size : mixed_sizes) {
            pointers.push_back(malloc(size));
        }
    }
    for (void* ptr : pointers) {
        free(ptr);
    }
    auto end_mixed_malloc = std::chrono::high_resolution_clock::now();
    auto duration_mixed_malloc = std::chrono::duration_cast<std::chrono::milliseconds>(end_mixed_malloc - start_mixed_malloc);
    pointers.clear();

    // --- Mixed Test 2: PoolAllocator (On-Demand) ---
    auto start_mixed_pool = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_MIXED_ALLOCATIONS_PER_SIZE; ++i) {
        for (const auto& size : mixed_sizes) {
            pointers.push_back(pool_alloc_malloc(size));
        }
    }
    // CORRECTED: The deallocation loop now uses the correct custom free function.
    for (void* ptr : pointers) {
        pool_alloc_free(ptr);
    }
    auto end_mixed_pool = std::chrono::high_resolution_clock::now();
    auto duration_mixed_pool = std::chrono::duration_cast<std::chrono::milliseconds>(end_mixed_pool - start_mixed_pool);
    pointers.clear();

    // --- Mixed-Size Results ---
    std::cout << "\n--- Mixed-Size Results ---" << std::endl;
    std::cout << "System malloc took:                  " << duration_mixed_malloc.count() << " ms" << std::endl;
    std::cout << "PoolAllocator (On-Demand) took:      " << duration_mixed_pool.count() << " ms" << std::endl;

    return 0;
}

