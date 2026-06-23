#pragma once

#include "PlatformMemory.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace cma {

template <size_t BlockSize>
class FixedBlockAllocator {
public:
    static constexpr size_t PAGE_SIZE = 64 * 1024;
    static constexpr size_t PAGE_ALIGNMENT = PAGE_SIZE;
    static constexpr size_t REFILL_BATCH = 512;
    static constexpr size_t HIGH_WATER_MARK = 2048;
    static constexpr size_t FLUSH_BATCH = 512;

    struct Stats {
        size_t active_pages = 0;
        size_t live_blocks = 0;
        size_t capacity_blocks = 0;
        size_t free_blocks = 0;
        size_t mapped_bytes = 0;
        size_t live_bytes = 0;
        size_t free_bytes = 0;
    };

    FixedBlockAllocator() {
        std::lock_guard<std::mutex> lock(m_mutex);
        grow_locked();
    }

    ~FixedBlockAllocator() {
        flush_all_local_cache_to_central();
        std::lock_guard<std::mutex> lock(m_mutex);
        while (m_page_list != nullptr) {
            Page* next = m_page_list->next;
            unmap_page(m_page_list->mapping_base, m_page_list->mapping_size);
            m_page_list = next;
        }
        forget_thread_cache();
    }

    FixedBlockAllocator(const FixedBlockAllocator&) = delete;
    FixedBlockAllocator& operator=(const FixedBlockAllocator&) = delete;

    // -------------------------------------------------------------------------
    // Hot path: thread-local cache first, central pool only on refill/flush.
    // -------------------------------------------------------------------------

    void* allocate() {
        ThreadCache& cache = thread_cache();

        Block* block = take_from_thread_cache(cache);
        if (block == nullptr) {
            if (!refill_thread_cache(cache)) {
                return nullptr;
            }
            block = take_from_thread_cache(cache);
            if (block == nullptr) {
                return nullptr;
            }
        }

        Page* page = find_page(block);
        if (page == nullptr) {
            return nullptr;
        }
        page->live_count.fetch_add(1, std::memory_order_relaxed);
        return static_cast<void*>(block);
    }

    void deallocate(void* ptr) {
        if (ptr == nullptr) {
            return;
        }

        Page* page = find_page(ptr);
        if (page == nullptr) {
            return;
        }
        page->live_count.fetch_sub(1, std::memory_order_relaxed);

        ThreadCache& cache = thread_cache();
        Block* block = static_cast<Block*>(ptr);
        block->next = cache.head;
        cache.head = block;
        ++cache.size;

        if (cache.size > HIGH_WATER_MARK) {
            flush_excess_thread_cache(cache);
        }
    }

    void flush_local_thread_cache() {
        flush_all_local_cache_to_central();
    }

    // -------------------------------------------------------------------------
    // Stats (each takes one lock; stats() returns a consistent snapshot)
    // -------------------------------------------------------------------------

    Stats stats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t active_pages = active_page_count_locked();
        const size_t live_blocks = live_block_count_locked();
        const size_t capacity_blocks = active_pages * blocks_per_page();
        const size_t free_blocks = capacity_blocks - live_blocks;
        return Stats{active_pages,
                     live_blocks,
                     capacity_blocks,
                     free_blocks,
                     active_pages * PAGE_SIZE,
                     live_blocks * BlockSize,
                     free_blocks * BlockSize};
    }

    size_t active_page_count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return active_page_count_locked();
    }

    size_t live_block_count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return live_block_count_locked();
    }

    size_t free_block_count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return active_page_count_locked() * blocks_per_page() - live_block_count_locked();
    }

    size_t capacity_block_count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return active_page_count_locked() * blocks_per_page();
    }

    size_t mapped_bytes() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return active_page_count_locked() * PAGE_SIZE;
    }

    size_t live_bytes() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return live_block_count_locked() * BlockSize;
    }

    size_t free_bytes() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t capacity = active_page_count_locked() * blocks_per_page();
        return (capacity - live_block_count_locked()) * BlockSize;
    }

    static constexpr size_t blocks_per_page() {
        return (PAGE_SIZE - sizeof(Page)) / BlockSize;
    }

private:
    // -------------------------------------------------------------------------
    // Types
    // -------------------------------------------------------------------------

    union Block {
        char data[BlockSize];
        Block* next;
    };

    // A thread's private cache has two sources of blocks:
    //   * head  - an intrusive list of recycled (previously freed) blocks.
    //   * bump  - an untouched contiguous range carved from a single page. Blocks
    //             are handed out by advancing bump_ptr, so their memory is never
    //             written until the caller uses it (no double-touch on growth).
    struct ThreadCache {
        Block* head = nullptr;
        size_t size = 0;
        char* bump_ptr = nullptr;
        char* bump_end = nullptr;
    };

    // Blocks are carved out of a page lazily with a bump pointer (bump_offset)
    // and only linked onto free_list once returned. This avoids touching the
    // whole page (and writing 2000+ next-pointers) every time a page is mapped.
    struct Page {
        Page* next;
        Page* prev;
        std::atomic<size_t> live_count;
        size_t total_blocks;    // capacity in blocks
        size_t bump_offset;     // next never-carved block index
        size_t cached_on_page;  // returned blocks parked on this page's free_list
        Block* free_list;       // intrusive list of returned blocks
        char* block_base;       // start of the block region within the page
        void* mapping_base;
        size_t mapping_size;

        Page()
            : next(nullptr),
              prev(nullptr),
              live_count(0),
              total_blocks(0),
              bump_offset(0),
              cached_on_page(0),
              free_list(nullptr),
              block_base(nullptr),
              mapping_base(nullptr),
              mapping_size(0) {}

        // A page can be reclaimed once every carved block has come home.
        bool fully_returned() const {
            return cached_on_page == bump_offset;
        }
    };

    static_assert(BlockSize >= sizeof(Block), "BlockSize must be large enough to hold Block metadata.");
    static_assert(PAGE_SIZE > sizeof(Page), "PAGE_SIZE must be larger than the Page metadata struct.");

    // -------------------------------------------------------------------------
    // Central pool state
    // -------------------------------------------------------------------------

    mutable std::mutex m_mutex;
    Page* m_page_list = nullptr;
    size_t m_page_count = 0;
    size_t m_central_free_count = 0;  // blocks parked on any page's free_list

    size_t active_page_count_locked() const {
        return m_page_count;
    }

    size_t live_block_count_locked() const {
        size_t total = 0;
        for (Page* page = m_page_list; page != nullptr; page = page->next) {
            total += page->live_count.load(std::memory_order_relaxed);
        }
        return total;
    }

    // Returns one block to a page's free list. Releases the page back to the OS
    // once every carved block is home, except the final page (kept to avoid
    // churn) unless allow_release_last_page is set.
    void push_block_to_page_locked(Page* page, Block* block, bool allow_release_last_page) {
        block->next = page->free_list;
        page->free_list = block;
        page->cached_on_page++;
        m_central_free_count++;

        if (page->fully_returned()) {
            if (allow_release_last_page || active_page_count_locked() > 1) {
                release_page_locked(page);
            }
        }
    }

    // Moves up to max_blocks recycled blocks from a page's free list into a
    // thread cache by splicing the list (O(max_blocks) reads, one write).
    void pull_free_blocks_into_cache_locked(Page* page, ThreadCache& cache, size_t max_blocks) {
        Block* first = page->free_list;
        Block* last = first;
        size_t count = 1;
        while (count < max_blocks && last->next != nullptr) {
            last = last->next;
            ++count;
        }
        page->free_list = last->next;
        page->cached_on_page -= count;
        m_central_free_count -= count;

        last->next = cache.head;
        cache.head = first;
        cache.size += count;
    }

    void release_all_empty_pages_locked() {
        Page* page = m_page_list;
        while (page != nullptr) {
            Page* next = page->next;
            if (page->fully_returned()) {
                release_page_locked(page);
            }
            page = next;
        }
    }

    void release_page_locked(Page* page) {
        if (page == nullptr) {
            return;
        }

        if (page->prev != nullptr) {
            page->prev->next = page->next;
        } else {
            m_page_list = page->next;
        }
        if (page->next != nullptr) {
            page->next->prev = page->prev;
        }
        --m_page_count;
        m_central_free_count -= page->cached_on_page;

        unmap_page(page->mapping_base, page->mapping_size);
    }

    void grow_locked() {
        const size_t mapping_size = PAGE_SIZE + PAGE_ALIGNMENT - 1;
        void* const mapping_base = map_page(mapping_size);
        if (mapping_base == nullptr) {
            return;
        }

        const uintptr_t raw_address = reinterpret_cast<uintptr_t>(mapping_base);
        const uintptr_t aligned_address =
            (raw_address + PAGE_ALIGNMENT - 1) & ~(static_cast<uintptr_t>(PAGE_ALIGNMENT) - 1);

        Page* new_page = new (reinterpret_cast<void*>(aligned_address)) Page();
        new_page->mapping_base = mapping_base;
        new_page->mapping_size = mapping_size;
        new_page->block_base = reinterpret_cast<char*>(new_page) + sizeof(Page);
        new_page->total_blocks = blocks_per_page();
        new_page->next = m_page_list;
        new_page->prev = nullptr;
        if (m_page_list != nullptr) {
            m_page_list->prev = new_page;
        }
        m_page_list = new_page;
        ++m_page_count;
    }

    // -------------------------------------------------------------------------
    // Page lookup (O(1) via page alignment)
    // -------------------------------------------------------------------------

    Page* find_page(void* ptr) const {
        const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
        auto* page = reinterpret_cast<Page*>(address & ~(static_cast<uintptr_t>(PAGE_ALIGNMENT) - 1));

        const uintptr_t block_start = reinterpret_cast<uintptr_t>(page) + sizeof(Page);
        const uintptr_t page_end = reinterpret_cast<uintptr_t>(page) + PAGE_SIZE;
        if (address < block_start || address >= page_end) {
            return nullptr;
        }
        return page;
    }

    // -------------------------------------------------------------------------
    // Thread-local cache
    //
    // Caches are keyed by allocator instance in a thread_local map. A one-entry
    // fast path avoids the hash lookup for the common case of repeatedly hitting
    // the same allocator from the same thread.
    // -------------------------------------------------------------------------

    inline static thread_local const FixedBlockAllocator* s_fast_owner = nullptr;
    inline static thread_local ThreadCache* s_fast_cache = nullptr;

    ThreadCache& thread_cache() {
        if (s_fast_owner == this) {
            return *s_fast_cache;
        }
        ThreadCache& cache = thread_cache_map()[this];
        s_fast_owner = this;
        s_fast_cache = &cache;
        return cache;
    }

    void forget_thread_cache() {
        thread_cache_map().erase(this);
        if (s_fast_owner == this) {
            s_fast_owner = nullptr;
            s_fast_cache = nullptr;
        }
    }

    static std::unordered_map<const FixedBlockAllocator*, ThreadCache>& thread_cache_map() {
        static thread_local std::unordered_map<const FixedBlockAllocator*, ThreadCache> caches;
        return caches;
    }

    // Hands out one block from the cache: recycled blocks first, then the
    // untouched bump range. Returns nullptr only when both are exhausted.
    static Block* take_from_thread_cache(ThreadCache& cache) {
        if (cache.head != nullptr) {
            Block* block = cache.head;
            cache.head = block->next;
            cache.size--;
            return block;
        }
        if (cache.bump_ptr != cache.bump_end) {
            Block* block = reinterpret_cast<Block*>(cache.bump_ptr);
            cache.bump_ptr += BlockSize;
            return block;
        }
        return nullptr;
    }

    // Refills a thread cache from the central pool. Recycled central blocks are
    // reused first (bounds memory use); otherwise a fresh contiguous range is
    // carved (O(1), no block memory touched), growing by a page if needed.
    // Returns false only on OOM.
    bool refill_thread_cache(ThreadCache& cache) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_central_free_count > 0) {
            for (Page* page = m_page_list; page != nullptr; page = page->next) {
                if (page->free_list != nullptr) {
                    pull_free_blocks_into_cache_locked(page, cache, REFILL_BATCH);
                    return true;
                }
            }
        }

        // Only the head page can still have un-carved capacity (we always grow
        // and carve at the head), so a single check suffices.
        Page* page = m_page_list;
        if (page == nullptr || page->bump_offset >= page->total_blocks) {
            grow_locked();
            page = m_page_list;
            if (page == nullptr || page->bump_offset >= page->total_blocks) {
                return false;
            }
        }

        const size_t avail = page->total_blocks - page->bump_offset;
        const size_t take = avail < REFILL_BATCH ? avail : REFILL_BATCH;
        cache.bump_ptr = page->block_base + page->bump_offset * BlockSize;
        cache.bump_end = cache.bump_ptr + take * BlockSize;
        page->bump_offset += take;
        return true;
    }

    void flush_excess_thread_cache(ThreadCache& cache) {
        while (cache.size > HIGH_WATER_MARK) {
            const size_t to_flush = (cache.size > HIGH_WATER_MARK + FLUSH_BATCH)
                                        ? FLUSH_BATCH
                                        : cache.size - HIGH_WATER_MARK;

            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < to_flush; ++i) {
                Block* block = cache.head;
                cache.head = block->next;
                cache.size--;
                Page* page = find_page(block);
                if (page != nullptr) {
                    push_block_to_page_locked(page, block, false);
                }
            }
        }
    }

    void flush_all_local_cache_to_central() {
        ThreadCache& cache = thread_cache();

        Block* head = cache.head;
        char* bump_ptr = cache.bump_ptr;
        char* const bump_end = cache.bump_end;
        cache.head = nullptr;
        cache.size = 0;
        cache.bump_ptr = nullptr;
        cache.bump_end = nullptr;

        if (head == nullptr && bump_ptr == bump_end) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        while (head != nullptr) {
            Block* block = head;
            head = block->next;
            Page* page = find_page(block);
            if (page != nullptr) {
                push_block_to_page_locked(page, block, true);
            }
        }
        // Return the never-used tail of the bump range so its page can be freed.
        for (; bump_ptr != bump_end; bump_ptr += BlockSize) {
            Block* block = reinterpret_cast<Block*>(bump_ptr);
            Page* page = find_page(block);
            if (page != nullptr) {
                push_block_to_page_locked(page, block, true);
            }
        }
        release_all_empty_pages_locked();
    }
};

} // namespace cma
