# C++ Custom Memory Allocator

A fixed-size memory allocator written in C++17.

It obtains memory directly from the operating system, divides it into equal-sized blocks, and uses thread-local caches to reduce locking.

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
    │ page becomes fully unused
    ▼
Return memory to the OS
```

The allocator:

1. Maps memory with `mmap` on POSIX or `VirtualAlloc` on Windows.
2. Organizes memory into aligned 64 KiB pages.
3. Carves new blocks using a bump pointer.
4. Stores the free-list pointer inside each freed block.
5. Gives each thread a local cache.
6. Transfers blocks between thread caches and the central pool in batches.
7. Uses a mutex only when accessing the central pool.
8. Returns a page to the OS after all its carved blocks return to the central pool.

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

## Build

Requirements:

- C++17 compiler
- GNU Make
- Python 3 for dashboard generation

Build the project:

```bash
make
```

## Tests

Run all tests:

```bash
make test
```

Run tests with sanitizers:

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

- **Name:** Ryan Park
- **Email:** [parkryan0128@gmail.com](mailto:parkryan0128@gmail.com)
- **LinkedIn:** [linkedin.com/in/parkryan0128](https://www.linkedin.com/in/parkryan0128)
- **GitHub:** [github.com/Parkryan0128](https://github.com/Parkryan0128)
