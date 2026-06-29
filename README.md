# C++ Custom Memory Allocator

[![CI](https://github.com/Parkryan0128/CustomMemoryAllocator/actions/workflows/ci.yml/badge.svg)](https://github.com/Parkryan0128/CustomMemoryAllocator/actions/workflows/ci.yml)
[![Language](https://img.shields.io/badge/Language-C%2B%2B-blue.svg)]()

A fixed-size block allocator written in C++. It serves one constant block size using an intrusive free list backed by OS memory pages, and includes a benchmark suite comparing it against `malloc`/`free`.

Live site: https://parkryan0128.github.io/CustomMemoryAllocator/

## Key Features

* **Fixed block size:** All allocations are the same size, so there is no size-class routing or per-request bookkeeping.
* **Lock-free common path:** Allocate/deallocate hit a thread-local cache with no lock; the central pool is locked only for batched refills, flushes, and page growth.
* **Lazy bump carving:** Fresh capacity is handed to a thread as an untouched contiguous range, so block memory is only written when the caller uses it (no double-touch on growth).
* **Empty-page release:** Returns fully unused 64 KB pages to the OS. Call `flush_local_thread_cache()` to reclaim a thread's cached blocks first.
* **Cross-platform platform memory layer:** Uses `mmap` on POSIX systems and `VirtualAlloc` on Windows.

## Project Structure

```
├── include/
│   ├── FixedBlockAllocator.hpp  # Fixed-size allocator
│   └── PlatformMemory.hpp       # OS page map/unmap interface
├── src/
│   └── PlatformMemory.cpp       # mmap / VirtualAlloc implementation
├── tests/                       # Tests
├── dashboard/
│   ├── load_data.py
│   ├── generate.py              # unified index.html
│   ├── requirements.txt         # Python deps for make plot
│   └── data/                    # generated CSV, traces, PNGs
├── .github/workflows/
├── Makefile
└── README.md
```

All production code lives in the `cma` namespace (`Custom Memory Allocator`).

## How to Build and Run

### Requirements

* C++17 compiler (GCC, Clang, or MSVC)
* `make`
* Python 3; install `dashboard/requirements.txt` for `make plot`

### Build

```bash
make
```

This builds both binaries:

| Binary | Purpose |
|--------|---------|
| `unit_tests` | Full test suite (95 tests) |
| `allocator_test` | Benchmark / plot / trace CLI (`-O2` release build) |

### Run Unit Tests

```bash
make test
```

Sanitizer builds (require Clang or GCC with sanitizer support):

```bash
make test-asan    # AddressSanitizer
make test-tsan    # ThreadSanitizer
make test-ubsan   # UndefinedBehaviorSanitizer
```

Or pass `SANITIZE=address|thread|undefined` directly: `make test SANITIZE=address`.

### Continuous Integration

GitHub Actions (`.github/workflows/ci.yml`) runs on every push and pull request:

| Job | Platform | What it checks |
|-----|----------|----------------|
| **test** | Ubuntu + macOS | Full 95-test suite (debug build) |
| **asan** | Ubuntu + macOS | Tests under AddressSanitizer |
| **tsan** | Ubuntu | Tests under ThreadSanitizer |
| **ubsan** | Ubuntu | Tests under UndefinedBehaviorSanitizer |

### Run Benchmarks

Quick console comparison:

```bash
make benchmark
# or
./allocator_test benchmark
```

Generate legacy matplotlib plots (requires `pip install -r dashboard/requirements.txt`):

```bash
make plot
```

### Interactive dashboard

Benchmark charts and lifecycle traces in one page (`index.html`):

```bash
make dashboard
# open index.html
```

### Clean

```bash
make clean
```

## How It Works

### Platform Memory Layer (`cma::map_page` / `cma::unmap_page`)

The lowest layer requests large virtual memory regions from the operating system:

* **POSIX:** `mmap` / `munmap`
* **Windows:** `VirtualAlloc` / `VirtualFree`

Failures return `nullptr` (map) or are ignored (unmap of invalid/null pointer).

### `cma::FixedBlockAllocator<BlockSize>`

A template that manages one fixed block size:

1. On construction (or when a thread cache runs dry), the central pool maps a 64 KB page from the OS. Each page is 64 KB-aligned so any block address masks back to its page header in O(1).
2. Blocks are **carved lazily** with a bump pointer: a refill hands a thread an untouched `[bump_ptr, bump_end)` range, so no block memory is written until the caller uses it.
3. Freed blocks are recycled through an intrusive free list (the `next` pointer lives inside each free block) — thread-local first, spilling to per-page central lists in batches.

Allocation takes a recycled block from the thread cache, or carves from its bump range, falling back to a locked refill. Deallocation pushes to the thread cache and only touches the central pool when the cache exceeds a high-water mark. A page is returned to the OS once every carved block has come home; because freed blocks linger in thread caches, call `flush_local_thread_cache()` to force reclamation.

#### Public query API

| Method | Description |
|--------|-------------|
| `stats()` | Consistent snapshot of all counters under one lock |
| `active_page_count()` | Mapped pages |
| `live_block_count()` | Blocks currently allocated to callers |
| `free_block_count()` | Free blocks on central page lists |
| `capacity_block_count()` | Total blocks across mapped pages |
| `mapped_bytes()` / `live_bytes()` / `free_bytes()` | Byte equivalents |
| `flush_local_thread_cache()` | Return this thread's cached blocks to the central pool |

`allocate()` returns `nullptr` on mapping failure. Call `flush_local_thread_cache()` on worker threads before they exit so cached blocks are not stranded.

## Performance

`./allocator_test benchmark` compares the allocator against the system `malloc`/`free`
for 32-byte blocks across two axes:

* **Threading:** single-thread and multi-thread (one private allocator per thread in the
  multi case — the idiomatic way to use a fixed-block pool).
* **Workload:** *interleaved* (allocate then immediately free), *batch* (allocate many,
  hold, then free them all), and *random_mix* (pseudo-random alloc/free with a live set).

Each measurement defeats dead-code elimination (every block is written through an
optimization barrier so `malloc` cannot be elided), warms up once, and reports the
median of several runs. The console prints `ratio = custom / malloc` (values below 1 mean
the custom allocator is faster). Representative results (Apple Silicon, 8 threads, 5M ops):

| Scenario | Custom (ms) | System malloc (ms) | Ratio (custom/malloc) |
|----------|------------:|-------------------:|----------------------:|
| single, interleaved | 29 | 99 | 0.29 |
| single, batch | 93 | 106 | 0.88 |
| multi, interleaved | 6 | 50 | 0.12 |
| multi, batch | 31 | 27 | 1.15 |

Run `make benchmark` for all six scenarios (including random_mix). The interactive
dashboard (`make dashboard`) charts the full CSV.

Run `make plot` to regenerate `dashboard/data/results.csv` and `benchmark_*.png` charts
(install deps with `pip install -r dashboard/requirements.txt`).
