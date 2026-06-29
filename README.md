# C++ Custom Memory Allocator

[![CI](https://github.com/Parkryan0128/CustomMemoryAllocator/actions/workflows/ci.yml/badge.svg)](https://github.com/Parkryan0128/CustomMemoryAllocator/actions/workflows/ci.yml)
[![Language](https://img.shields.io/badge/Language-C%2B%2B-blue.svg)]()

A high-performance, fixed-size block allocator written in C++. It manages memory via an intrusive free list backed directly by OS pages, offering significant performance improvements over the standard system `malloc`/`free`. 

Live Dashboard & Benchmarks: [parkryan0128.github.io/CustomMemoryAllocator](https://parkryan0128.github.io/CustomMemoryAllocator/)

***

## 📋 Table of Contents

* [Key Features](#key-features)
* [Project Structure](#project-structure)
* [How to Build and Run](#how-to-build-and-run)
* [Internal Architecture](#internal-architecture)
* [Performance](#performance)
* [Contact](#contact)

***
<a id="key-features"></a>
## ✨ Key Features

* **Fixed-Block Architecture:** Allocations are uniformly sized, eliminating the need for per-request bookkeeping overhead.
* **Lock-Free Fast Path:** Allocation and deallocation utilize a lockless thread-local cache. The central pool requires locking only for operations like batched refills, flushes, and page growth.
* **Lazy Bump Allocation:** Fresh capacity is provided to threads as an untouched contiguous memory range. Physical memory is only committed when explicitly used, preventing redundant page faults.
* **Aggressive Memory Reclamation:** Fully unused 64 KB pages are automatically unmapped and returned to the OS.
* **Cross-Platform Abstraction:** Leverages native OS APIs (`mmap` on POSIX, `VirtualAlloc` on Windows) for direct virtual memory management.

***
<a id="project-structure"></a>
## 📁 Project Structure

```text
├── include/
│   ├── FixedBlockAllocator.hpp  # Core allocator implementation
│   └── PlatformMemory.hpp       # OS page map/unmap interface
├── src/
│   └── PlatformMemory.cpp       # OS-specific memory mappings
├── tests/                       # Unit and integration test suites
├── dashboard/
│   ├── load_data.py
│   ├── generate.py              # Generates the unified index.html
│   ├── requirements.txt         # Python dependencies for plotting
│   └── data/                    # Output directory for CSVs and PNGs
├── .github/workflows/           # CI/CD pipelines
├── Makefile
└── README.md
```

***
<a id="how-to-build-and-run"></a>
## ⚙️ How to Build and Run

### Prerequisites

* **Compiler:** C++17 compliant (GCC, Clang, or MSVC)
* **Build System:** `make`
* **Python 3:** (Optional) Required only for generating benchmark plots and the web dashboard (`pip install -r dashboard/requirements.txt`).

### Build

To compile the project, run:
```bash
make
```

This generates two primary target binaries:

| Target Binary | Description |
|---------------|-------------|
| `unit_tests`  | Comprehensive test suite (debug build). |
| `allocator_test` | CLI tool for benchmarks, plotting, and tracing (compiled with `-O2` optimizations). |

### Unit Testing & Memory Safety

Execute the standard test suite (95 automated tests):
```bash
make test
```

**Sanitizer Builds:**
To compile and run tests with LLVM/GCC sanitizers enabled (requires a compatible compiler):
```bash
make test-asan    # AddressSanitizer (Memory errors)
make test-tsan    # ThreadSanitizer (Data races)
make test-ubsan   # UndefinedBehaviorSanitizer (UB checks)
```
*(Note: Alternatively, you can pass the flag directly: `make test SANITIZE=address`)*

### Continuous Integration (CI)

Automated GitHub Actions workflows (`.github/workflows/ci.yml`) are triggered on all pushes and PRs to ensure main branch stability:

| Job | OS Platform | Validation Scope |
|-----|-------------|------------------|
| **test** | Ubuntu, macOS | Standard test suite execution |
| **asan** | Ubuntu, macOS | Memory leak and out-of-bounds detection |
| **tsan** | Ubuntu | Concurrency and lock-free thread safety |
| **ubsan** | Ubuntu | Undefined behavior compliance |

### Benchmarking & Visualization

**1. Console Benchmark:**
Run a fast CLI comparison against the standard system allocator:
```bash
make benchmark
# Alternatively: ./allocator_test benchmark
```

**2. Legacy Matplotlib Plots:**
Generate static PNG charts representing allocation latencies (requires Python dependencies):
```bash
make plot
```

**3. Interactive Web Dashboard:**
Compile results and trace logs into a unified HTML dashboard for visual inspection:
```bash
make dashboard
# This generates and automatically opens index.html
```

### Clean

Remove all compiled binaries and build artifacts:
```bash
make clean
```

***
<a id="internal-architecture"></a>
## 🏗️ Internal Architecture

### Platform Memory Layer

The foundational layer requests large, contiguous virtual memory regions directly from the operating system:
* **POSIX Systems:** Uses `mmap` / `munmap`
* **Windows Systems:** Uses `VirtualAlloc` / `VirtualFree`

Memory mapping failures gracefully return `nullptr`, and unmapping invalid or null pointers is safely ignored.

### `cma::FixedBlockAllocator<BlockSize>`

A template class governing a specific constant block size. The memory lifecycle follows these core phases:

1. **Mapping & Alignment:** When a thread cache is depleted, the central pool allocates a 64 KB page from the OS. Pages are strictly 64 KB-aligned, allowing any given block pointer to resolve its parent page header in O(1) time via bitwise masking.
2. **Lazy Bump Allocation:** Blocks are allocated via a bump pointer. A refill provides the thread with an uninitialized `[bump_ptr, bump_end)` memory range. This ensures physical memory pages are not dirtied until they are explicitly accessed by the application.
3. **Intrusive Free List:** Freed blocks are managed via an intrusive free list (the `next` pointer is stored directly inside the unallocated block). Blocks are pushed to the thread-local cache first, and then spilled over to the central pool in batches to minimize lock contention.

**Deallocation Strategy:** Calling `deallocate()` pushes blocks back to the thread-local cache. If the cache exceeds a predefined high-water mark, it transfers a batch to the central pool. A page is fully unmapped and returned to the OS once all of its constituent blocks are freed. 

***
<a id="performance"></a>
## 📊 Performance

The benchmark suite (`./allocator_test benchmark`) evaluates this allocator against the standard system `malloc`/`free` using 32-byte blocks.

**Evaluation Scenarios:**
* **Threading:** Single-threaded vs. Multi-threaded (utilizing independent, thread-local allocator instances).
* **Workloads:**
  * *Interleaved:* Allocate and immediately free.
  * *Batch:* Allocate in bulk, hold, then free in bulk.
  * *Random Mix:* Pseudo-random allocations and deallocations maintaining an active live set.

**Representative Results**

| Scenario | Custom (ms) | System malloc (ms) | Ratio (custom/malloc) |
|----------|------------:|-------------------:|----------------------:|
| Single, Interleaved | 29 | 99 | **0.29** |
| Single, Batch | 93 | 106 | **0.88** |
| Multi, Interleaved | 6 | 50 | **0.12** |
| Multi, Batch | 31 | 27 | 1.15 |

View full interactive results here: [parkryan0128.github.io/CustomMemoryAllocator](https://parkryan0128.github.io/CustomMemoryAllocator/)

***
<a id="contact"></a>
## 📧 Contact

- **Name:** Ryan Park
- **Email:** [parkryan0128@gmail.com](mailto:parkryan0128@gmail.com)
- **LinkedIn:** [https://www.linkedin.com/in/parkryan0128](https://www.linkedin.com/in/parkryan0128)
- **GitHub:** [https://github.com/Parkryan0128](https://github.com/Parkryan0128)
