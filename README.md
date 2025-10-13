# C/C++ Custom Memory Allocator

[![Language](https://img.shields.io/badge/Language-C-blue.svg)]()

A custom, high-performance memory pool allocator written in C/C++. It is designed for multi-threaded applications to significantly reduce the overhead and contention associated with standard library memory functions like `malloc` and `free`.

***

## 📋 Table of Contents

* [Key Features](#-key-features)
* [Performance Results](#-performance-results)
* [How It Works (Architecture)](#-how-it-works-architecture)
    * [1. OS Abstraction Layer (`OS.cpp`)](#1-os-abstraction-layer-oscpp)
    * [2. The Core `MemoryPool<T>` (`MemoryPool.hpp`)](#2-the-core-memorypoolt-memorypoolhpp)
    * [3. The `PoolAllocator` (`PoolAllocator.hpp`)](#3-the-poolallocator-poolallocatorhpp)
* [How to Build and Run](#-how-to-build-and-run)
* [Project Structure](#-project-structure)
* [Dependencies](#-dependencies)

***

## ✨ Key Features

* **High-Performance:** Significantly faster than the standard system `malloc` in multi-threaded scenarios by reducing lock contention.
* **Thread-Safe by Design:** Utilizes `thread_local` caches to give each thread its own private pool of memory blocks, minimizing the need for expensive mutex locks.
* **Contention Reduction:** Padded mutexes (`PaddedMutex`) are used to prevent **false sharing** between CPU caches, a common performance killer in multi-core systems.
* **Efficient Memory Usage:** Employs a **segregated-fit** strategy, maintaining separate memory pools for different block sizes to reduce fragmentation.
* **Batch Memory Management:** Thread caches are refilled from the central pool in batches, amortizing the cost of locking. A high-water mark mechanism returns excess memory back to the central pool.
* **Cross-Platform:** An OS abstraction layer uses `mmap` on POSIX systems (Linux, macOS) and `VirtualAlloc` on Windows to request memory directly from the operating system.
* **Comprehensive Benchmarking:** Includes a detailed benchmark suite to compare the performance against the system's default allocator in various scenarios (single-thread, multi-thread, random sizes).

***
## 🚀 Performance Results
<!---

The primary goal of this allocator is to outperform the standard library's `malloc` in high-contention, multi-threaded environments. Benchmarks were run to simulate intense allocation/deallocation patterns.

The results below show the total time taken for both the custom `PoolAllocator` and the system's `malloc` to perform an increasing number of allocations across multiple threads.


*Performance Comparison in a Multi-Threaded Contention Benchmark*

#### Key Takeaways:

* **Scalability:** The custom `PoolAllocator` exhibits significantly better performance as the number of allocations and threads increases. Its execution time remains almost flat, demonstrating minimal contention.
* **System `malloc` Bottleneck:** The system allocator's performance degrades linearly, becoming a major bottleneck due to lock contention as multiple threads compete for access to the global heap.
* **Throughput:** In the test environment ([mention your CPU, e.g., Apple M1 Pro with 8 cores]), the custom allocator achieved a throughput up to **[XX] times higher** than `malloc`.
-->
***

## 🏗️ How It Works (Architecture)

The allocator is built on a three-layer architecture, providing a clear separation of concerns from the OS level up to the user-facing API.

### 1. OS Abstraction Layer (`OS.cpp`)

This is the lowest level, responsible for interacting with the operating system to request large chunks of virtual memory.

* On **POSIX** systems (Linux, macOS), it uses `mmap`.
* On **Windows**, it uses `VirtualAlloc` and `VirtualFree`.

This abstracts away platform-specific details, allowing the higher-level allocators to work with a simple `alloc_chunk()` and `free_chunk()` interface.

### 2. The Core `MemoryPool<T>` (`MemoryPool.hpp`)

This class is the workhorse for a **single size class**.

* It is a C++ template (`MemoryPool<size_t BlockSize>`) that manages memory blocks of one specific size.
* When initialized or exhausted, it requests a large **chunk** (e.g., 64KB) from the OS Abstraction Layer.
* This chunk is then partitioned into smaller `BlockSize` blocks.
* These blocks are managed using an **intrusive free-list**, where the `next` pointer is stored within the free block itself, eliminating the need for separate metadata and maximizing memory usage.

### 3. The `PoolAllocator` (`PoolAllocator.hpp`)

This is the top-level, user-facing class that ties everything together. It is responsible for handling requests for various sizes and managing thread safety.

1.  **Segregated Pools:** It maintains an array of `MemoryPool<T>` instances, each for a different size class (e.g., 8, 16, 24, ..., 512 bytes). A lookup table (`m_size_to_pool_index`) allows for $O(1)$ mapping of a requested size to the appropriate pool.

2.  **Thread-Local Caching (The Key to Performance):**
    To avoid locking a global mutex on every `allocate`/`deallocate` call, each thread has its own private cache of free blocks for each size class.
    * **Allocation:** When `allocate(size)` is called, it first checks the current thread's local cache. If the cache has a free block, it's returned immediately—**with no locking required**.
    * **Cache Miss:** If the thread's cache is empty, it calls a `refill_cache()` function. This function locks the corresponding global `MemoryPool`'s mutex, grabs a **batch** of blocks (e.g., 64), adds them to the thread-local cache, and then unlocks the mutex. This amortizes the cost of locking over many allocations.
    * **Deallocation:** When `deallocate(ptr)` is called, the block is returned to the thread's local cache—again, **with no locking**.

3.  **Returning Memory:** If a thread's local cache grows too large (exceeds the `HIGH_WATER_MARK`), a batch of its free blocks are moved back to the central `MemoryPool` to be made available to other threads.

***

## ⚙️ How to Build and Run

#### Requirements

* A C++ compiler that supports C++17 (e.g., GCC, Clang, MSVC)
* Python 3.x
* Python libraries: `pandas`, `seaborn`, `matplotlib`

#### 1. Compile the Benchmark

You can compile the project using g++ or Clang.

```bash
# Using g++
g++ -std=c++17 -O3 -pthread main.cpp OS.cpp -o benchmark

# Using Clang
clang++ -std=c++17 -O3 -pthread main.cpp OS.cpp -o benchmark
