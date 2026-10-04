# C++ Custom Memory Allocator

A C++17 allocator for fixed-size blocks. It gets memory from the operating system and uses thread-local caches to reduce locking.

[Benchmarks and dashboard](https://parkryan0128.github.io/CustomMemoryAllocator/)

## How it works

```text
allocate()
    │
    ▼
Thread-local cache
    │ empty
    ▼
Central pool (mutex)
    ├── Reuse freed blocks
    ├── Carve new blocks with a bump pointer
    └── Request more memory from the OS
```

```text
deallocate()
    │
    ▼
Thread-local intrusive free list
    │ cache becomes too large
    ▼
Central pool
    │ all carved blocks returned
    ▼
Page can be released to the OS
```

Memory is organized into aligned 64 KiB pages, backed by `mmap` on POSIX or `VirtualAlloc` on Windows. Freed blocks store their own free-list pointers.

Thread caches exchange blocks with the central pool in batches. A page stays mapped while any carved block is still allocated or held in a thread cache.

## Usage

Each allocator instance handles one block size:

```cpp
cma::FixedBlockAllocator<32> allocator;

void* block = allocator.allocate();
allocator.deallocate(block);
```

Thread-local caches should be flushed before a worker thread exits:

```cpp
allocator.flush_local_thread_cache();
```

The allocator must outlive every thread using it.

## Project structure

```text
include/    Allocator and platform memory headers
src/        OS memory mapping implementation
tests/      Unit, integration, concurrency, and benchmark tests
dashboard/  Benchmark dashboard generator
```

## Build and test

Requires a C++17 compiler and GNU Make. Dashboard generation also needs Python 3.

```bash
make
make test
```

To run the tests with sanitizers:

```bash
make test-asan
make test-tsan
make test-ubsan
```

## Benchmarks

Compare the allocator with system `malloc` and `free`:

```bash
make benchmark
```

Generate benchmark data and rebuild the dashboard:

```bash
make dashboard
```

## Contact

Ryan Park · [Email](mailto:parkryan0128@gmail.com) · [LinkedIn](https://www.linkedin.com/in/parkryan0128) · [GitHub](https://github.com/Parkryan0128)
