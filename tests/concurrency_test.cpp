#include "FixedBlockAllocator.hpp"
#include "test_helpers.hpp"
#include "test_runner.hpp"
#include "workload_common.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace {

using cma_test::Allocator;
using cma_test::PhaseBarrier;
using cma_test::allocate_blocks;
using cma_test::deallocate_blocks;
using cma_test::expect_stats_consistent;
using cma_test::flush_thread_cache;
using cma_test::kBlockSize;

unsigned int default_thread_count() {
    const unsigned int hw = std::thread::hardware_concurrency();
    const unsigned int count = hw > 0 ? hw : 4U;
#ifdef CMA_TSAN_BUILD
    return count > 2 ? 2U : count;
#else
    return count;
#endif
}

#ifdef CMA_TSAN_BUILD
constexpr size_t tsan_scale(size_t value) {
    return value / 4 + 1;
}

constexpr unsigned int tsan_threads(unsigned int count) {
    return count > 2 ? 2U : count;
}
#else
constexpr size_t tsan_scale(size_t value) {
    return value;
}

constexpr unsigned int tsan_threads(unsigned int count) {
    return count;
}
#endif

void stamp_block(void* block, unsigned int thread_id, size_t iteration) {
    auto* bytes = static_cast<unsigned char*>(block);
    std::memset(bytes, 0, kBlockSize);
    bytes[0] = static_cast<unsigned char>(thread_id);
    bytes[1] = static_cast<unsigned char>((iteration >> 0) & 0xFF);
    bytes[2] = static_cast<unsigned char>((iteration >> 8) & 0xFF);
    bytes[3] = static_cast<unsigned char>((iteration >> 16) & 0xFF);
}

void verify_block_stamp(void* block, unsigned int thread_id, size_t iteration) {
    const auto* bytes = static_cast<const unsigned char*>(block);
    EXPECT_EQ(bytes[0], static_cast<unsigned char>(thread_id));
    EXPECT_EQ(bytes[1], static_cast<unsigned char>((iteration >> 0) & 0xFF));
    EXPECT_EQ(bytes[2], static_cast<unsigned char>((iteration >> 8) & 0xFF));
    EXPECT_EQ(bytes[3], static_cast<unsigned char>((iteration >> 16) & 0xFF));
}

void worker_alloc_free(Allocator* allocator, size_t iterations) {
    for (size_t i = 0; i < iterations; ++i) {
        void* block = allocator->allocate();
        EXPECT_NOT_NULL(block);
        stamp_block(block, 0, i);
        verify_block_stamp(block, 0, i);
        allocator->deallocate(block);
    }
    flush_thread_cache(*allocator);
}

void worker_alloc_free_stamped(Allocator* allocator, unsigned int thread_id, size_t iterations) {
    for (size_t i = 0; i < iterations; ++i) {
        void* block = allocator->allocate();
        EXPECT_NOT_NULL(block);
        stamp_block(block, thread_id, i);
        verify_block_stamp(block, thread_id, i);
        allocator->deallocate(block);
    }
    flush_thread_cache(*allocator);
}

template <size_t BlockSize>
void run_parallel_alloc_free_smoke(size_t iterations, unsigned int thread_count = 4) {
    cma::FixedBlockAllocator<BlockSize> allocator;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < iterations; ++j) {
                void* block = allocator.allocate();
                EXPECT_NOT_NULL(block);
                allocator.deallocate(block);
            }
            flush_thread_cache(allocator);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

} // namespace

TEST(Concurrency_HighVolumeParallelWorkload) {
    Allocator allocator;
    const unsigned int thread_count = default_thread_count();
    const size_t iterations_per_thread = 1000;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back(worker_alloc_free, &allocator, iterations_per_thread);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_EachThreadCanDrainToZeroPages) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    std::thread first([&]() {
        auto blocks = allocate_blocks(allocator, blocks_per_page);
        deallocate_blocks(allocator, blocks);
        flush_thread_cache(allocator);
    });

    std::thread second([&]() {
        auto blocks = allocate_blocks(allocator, blocks_per_page);
        deallocate_blocks(allocator, blocks);
        flush_thread_cache(allocator);
    });

    first.join();
    second.join();

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_CrossThreadDeallocate) {
    Allocator allocator;
    void* block = nullptr;

    std::thread allocator_thread([&]() {
        block = allocator.allocate();
        EXPECT_NOT_NULL(block);
        stamp_block(block, 1, 42);
    });
    allocator_thread.join();

    std::thread deallocator_thread([&]() {
        verify_block_stamp(block, 1, 42);
        allocator.deallocate(block);
        flush_thread_cache(allocator);
    });
    deallocator_thread.join();

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_CrossThreadManyHandoffs) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t handoffs_per_pair = tsan_scale(500);

    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<void*> block_queue;
    std::atomic<bool> producers_done{false};

    std::vector<std::thread> producers;
    for (unsigned int i = 0; i < thread_count; ++i) {
        producers.emplace_back([&, i]() {
            for (size_t j = 0; j < handoffs_per_pair; ++j) {
                void* block = allocator.allocate();
                EXPECT_NOT_NULL(block);
                stamp_block(block, i, j);
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    block_queue.push(block);
                }
                queue_cv.notify_one();
            }
        });
    }

    std::atomic<size_t> consumed_total{0};
    std::vector<std::thread> consumers;
    for (unsigned int i = 0; i < thread_count; ++i) {
        consumers.emplace_back([&]() {
            while (true) {
                void* block = nullptr;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    queue_cv.wait(lock, [&]() {
                        return !block_queue.empty() ||
                               (producers_done.load() && block_queue.empty());
                    });
                    if (block_queue.empty()) {
                        break;
                    }
                    block = block_queue.front();
                    block_queue.pop();
                }
                EXPECT_NOT_NULL(block);
                const auto* bytes = static_cast<const unsigned char*>(block);
                EXPECT_TRUE(bytes[0] < thread_count);
                allocator.deallocate(block);
                consumed_total.fetch_add(1);
            }
            flush_thread_cache(allocator);
        });
    }

    for (std::thread& thread : producers) {
        thread.join();
    }
    producers_done.store(true);
    queue_cv.notify_all();

    for (std::thread& thread : consumers) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(consumed_total.load(), thread_count * handoffs_per_pair);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_ProducerConsumerContinuous) {
    Allocator allocator;
    const size_t total_blocks = 4096;
    std::atomic<size_t> produced{0};
    std::atomic<size_t> consumed{0};

    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<void*> block_queue;

    std::thread producer([&]() {
        for (size_t i = 0; i < total_blocks; ++i) {
            void* block = allocator.allocate();
            EXPECT_NOT_NULL(block);
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                block_queue.push(block);
            }
            queue_cv.notify_one();
            produced.fetch_add(1);
        }
    });

    std::thread consumer([&]() {
        while (consumed.load() < total_blocks) {
            void* block = nullptr;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [&]() { return !block_queue.empty(); });
                block = block_queue.front();
                block_queue.pop();
            }
            allocator.deallocate(block);
            consumed.fetch_add(1);
        }
        flush_thread_cache(allocator);
    });

    producer.join();
    consumer.join();

    flush_thread_cache(allocator);
    EXPECT_EQ(produced.load(), total_blocks);
    EXPECT_EQ(consumed.load(), total_blocks);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_BarrierSimultaneousAllocThenFree) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t blocks_per_thread = 128;
    PhaseBarrier barrier(thread_count);

    std::vector<std::vector<void*>> per_thread_blocks(thread_count);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            auto& blocks = per_thread_blocks[i];
            blocks.reserve(blocks_per_thread);
            for (size_t j = 0; j < blocks_per_thread; ++j) {
                blocks.push_back(allocator.allocate());
                EXPECT_NOT_NULL(blocks.back());
            }
            barrier.arrive_and_wait();
            deallocate_blocks(allocator, blocks);
            flush_thread_cache(allocator);
        });
    }

    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_ParallelGrowthBeyondOnePage) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    const unsigned int thread_count = 4;
    const size_t blocks_per_thread = (blocks_per_page / thread_count) + 1;
    PhaseBarrier alloc_barrier(thread_count + 1);
    PhaseBarrier free_barrier(thread_count + 1);

    std::vector<std::vector<void*>> held(thread_count);
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            held[i] = allocate_blocks(allocator, blocks_per_thread);
            alloc_barrier.arrive_and_wait();
            free_barrier.arrive_and_wait();
            deallocate_blocks(allocator, held[i]);
            flush_thread_cache(allocator);
        });
    }

    alloc_barrier.arrive_and_wait();
    EXPECT_GE(allocator.active_page_count(), 2U);
    EXPECT_EQ(allocator.live_block_count(), thread_count * blocks_per_thread);
    free_barrier.arrive_and_wait();

    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_ParallelGrowthMultiPage) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    const unsigned int thread_count = default_thread_count();
    const size_t target_pages = 3;
    const size_t blocks_per_thread = (blocks_per_page * target_pages) / thread_count + 1;
    PhaseBarrier alloc_barrier(thread_count + 1);
    PhaseBarrier free_barrier(thread_count + 1);

    std::vector<std::vector<void*>> held(thread_count);
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            held[i] = allocate_blocks(allocator, blocks_per_thread);
            alloc_barrier.arrive_and_wait();
            free_barrier.arrive_and_wait();
            deallocate_blocks(allocator, held[i]);
            flush_thread_cache(allocator);
        });
    }

    alloc_barrier.arrive_and_wait();
    EXPECT_GE(allocator.active_page_count(), target_pages);
    free_barrier.arrive_and_wait();

    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_ConcurrentEmptyPageRelease) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    const unsigned int thread_count = 4;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            for (int cycle = 0; cycle < 3; ++cycle) {
                auto blocks = allocate_blocks(allocator, blocks_per_page);
                deallocate_blocks(allocator, blocks);
                flush_thread_cache(allocator);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    cma_test::flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_ConcurrentEmptyPageReleaseCrossThread) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    std::mutex block_mutex;
    std::vector<void*> shared_blocks;
    shared_blocks.reserve(blocks_per_page);

    std::thread producer([&]() {
        for (size_t i = 0; i < blocks_per_page; ++i) {
            void* block = allocator.allocate();
            EXPECT_NOT_NULL(block);
            std::lock_guard<std::mutex> lock(block_mutex);
            shared_blocks.push_back(block);
        }
    });

    std::thread consumer([&]() {
        producer.join();
        while (true) {
            void* block = nullptr;
            {
                std::lock_guard<std::mutex> lock(block_mutex);
                if (shared_blocks.empty()) {
                    break;
                }
                block = shared_blocks.back();
                shared_blocks.pop_back();
            }
            allocator.deallocate(block);
        }
        flush_thread_cache(allocator);
    });

    consumer.join();
    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_MemoryPatternsUniquePerThread) {
    Allocator allocator;
    const unsigned int thread_count = tsan_threads(8);
    const size_t iterations = tsan_scale(500);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back(worker_alloc_free_stamped, &allocator, i, iterations);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_StatsConsistentAfterParallelWorkload) {
    Allocator allocator;
    const unsigned int thread_count = default_thread_count();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back(worker_alloc_free, &allocator, 1500);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_StatsQueriesDuringWorkload) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t iterations = tsan_scale(2000);
    std::atomic<bool> stop{false};

    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < thread_count; ++i) {
        workers.emplace_back([&]() {
            for (size_t j = 0; j < iterations; ++j) {
                void* block = allocator.allocate();
                EXPECT_NOT_NULL(block);
                allocator.deallocate(block);
            }
            flush_thread_cache(allocator);
        });
    }

    std::thread stats_thread([&]() {
        while (!stop.load()) {
            (void)allocator.live_block_count();
            (void)allocator.free_block_count();
            (void)allocator.capacity_block_count();
            (void)allocator.mapped_bytes();
            (void)allocator.live_bytes();
            (void)allocator.free_bytes();
            (void)allocator.active_page_count();
        }
    });

    for (std::thread& thread : workers) {
        thread.join();
    }
    stop.store(true);
    stats_thread.join();

    cma_test::flush_thread_cache(allocator);
    cma_test::expect_stats_consistent(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_PartialLiveBlocksAcrossThreads) {
    Allocator allocator;
    const unsigned int thread_count = tsan_threads(6);
    const size_t held_per_thread = 32;

    std::vector<std::vector<void*>> held(thread_count);
    std::vector<std::thread> threads;

    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() { held[i] = allocate_blocks(allocator, held_per_thread); });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(allocator.live_block_count(), thread_count * held_per_thread);

    for (unsigned int i = 0; i < thread_count; ++i) {
        std::thread releaser([&, i]() {
            deallocate_blocks(allocator, held[i]);
            flush_thread_cache(allocator);
        });
        releaser.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_HighWaterMarkFlushStress) {
    Allocator allocator;
    const unsigned int thread_count = tsan_threads(8);
    const size_t iterations = tsan_scale(3000);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < iterations; ++j) {
                void* a = allocator.allocate();
                void* b = allocator.allocate();
                EXPECT_NOT_NULL(a);
                EXPECT_NOT_NULL(b);
                allocator.deallocate(a);
                allocator.deallocate(b);
            }
            flush_thread_cache(allocator);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_RefillBatchBoundary) {
    Allocator allocator;
    const size_t refill = Allocator::REFILL_BATCH;
    const unsigned int thread_count = 4;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            for (size_t cycle = 0; cycle < 5; ++cycle) {
                std::vector<void*> blocks;
                blocks.reserve(refill + 1);
                for (size_t j = 0; j < refill + 1; ++j) {
                    blocks.push_back(allocator.allocate());
                }
                for (size_t j = 0; j < refill; ++j) {
                    allocator.deallocate(blocks[j]);
                }
                allocator.deallocate(blocks[refill]);
                flush_thread_cache(allocator);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_ManyThreadsMoreThanCores) {
    Allocator allocator;
    const unsigned int thread_count = std::max(16U, default_thread_count() * 2);
    const size_t iterations = 400;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back(worker_alloc_free_stamped, &allocator, i, iterations);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_ShortLivedThreadBurst) {
    Allocator allocator;
    const unsigned int burst_count = 24;
    const size_t iterations = 200;

    for (unsigned int burst = 0; burst < burst_count; ++burst) {
        std::thread worker(worker_alloc_free, &allocator, iterations);
        worker.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_TwoAllocatorsIndependentUnderThreads) {
    cma::FixedBlockAllocator<32> first;
    cma::FixedBlockAllocator<32> second;
    const unsigned int thread_count = 4;
    const size_t iterations = tsan_scale(800);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            if (i % 2 == 0) {
                worker_alloc_free_stamped(&first, i, iterations);
            } else {
                worker_alloc_free_stamped(&second, i, iterations);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(first);
    flush_thread_cache(second);
    EXPECT_EQ(first.live_block_count(), 0U);
    EXPECT_EQ(second.live_block_count(), 0U);
    cma_test::expect_stats_consistent(first);
    cma_test::expect_stats_consistent(second);
}

TEST(Concurrency_TemplateBlockSize8Parallel) {
    run_parallel_alloc_free_smoke<8>(1500);
}

TEST(Concurrency_TemplateBlockSize64Parallel) {
    run_parallel_alloc_free_smoke<64>(1500);
}

TEST(Concurrency_RepeatedWavePattern) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t wave_size = 256;
    const size_t waves = 8;

    for (size_t wave = 0; wave < waves; ++wave) {
        PhaseBarrier wave_barrier(thread_count);
        std::vector<std::thread> threads;
        std::vector<std::vector<void*>> wave_blocks(thread_count);

        for (unsigned int i = 0; i < thread_count; ++i) {
            threads.emplace_back([&, i]() {
                wave_blocks[i] = allocate_blocks(allocator, wave_size);
                wave_barrier.arrive_and_wait();
                deallocate_blocks(allocator, wave_blocks[i]);
                flush_thread_cache(allocator);
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_RandomMixAllocFree) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t operations = 4000;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            std::vector<void*> live;
            live.reserve(64);
            for (size_t op = 0; op < operations; ++op) {
                const size_t salt = workload::random_mix_salt(op, i);
                if (workload::random_mix_should_alloc(live.size(), salt)) {
                    void* block = allocator.allocate();
                    EXPECT_NOT_NULL(block);
                    live.push_back(block);
                } else {
                    const size_t index = salt % live.size();
                    void* block = live[index];
                    live[index] = live.back();
                    live.pop_back();
                    allocator.deallocate(block);
                }
            }
            deallocate_blocks(allocator, live);
            flush_thread_cache(allocator);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_SingleBlockPingPong) {
    Allocator allocator;
    std::mutex slot_mutex;
    std::condition_variable slot_cv;
    void* slot = nullptr;
    bool slot_occupied = false;
    bool producer_done = false;
    const unsigned int rounds = 5000;

    std::thread producer([&]() {
        for (unsigned int i = 0; i < rounds; ++i) {
            void* block = allocator.allocate();
            EXPECT_NOT_NULL(block);
            {
                std::unique_lock<std::mutex> lock(slot_mutex);
                slot_cv.wait(lock, [&]() { return !slot_occupied; });
                stamp_block(block, 1, i);
                slot = block;
                slot_occupied = true;
            }
            slot_cv.notify_one();
        }
        {
            std::lock_guard<std::mutex> lock(slot_mutex);
            producer_done = true;
        }
        slot_cv.notify_one();
    });

    std::thread consumer([&]() {
        for (unsigned int i = 0; i < rounds; ++i) {
            void* block = nullptr;
            {
                std::unique_lock<std::mutex> lock(slot_mutex);
                slot_cv.wait(lock, [&]() { return slot_occupied || producer_done; });
                if (!slot_occupied) {
                    break;
                }
                block = slot;
                slot = nullptr;
                slot_occupied = false;
            }
            slot_cv.notify_one();
            verify_block_stamp(block, 1, i);
            allocator.deallocate(block);
        }
        flush_thread_cache(allocator);
    });

    producer.join();
    consumer.join();

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_OddEvenSplitFreeResponsibility) {
    Allocator allocator;
    const size_t total_blocks = 512;
    std::vector<void*> blocks = allocate_blocks(allocator, total_blocks);

    std::thread odds([&]() {
        for (size_t i = 1; i < blocks.size(); i += 2) {
            allocator.deallocate(blocks[i]);
        }
        flush_thread_cache(allocator);
    });

    std::thread evens([&]() {
        for (size_t i = 0; i < blocks.size(); i += 2) {
            allocator.deallocate(blocks[i]);
        }
        flush_thread_cache(allocator);
    });

    odds.join();
    evens.join();

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_StaggeredThreadCompletion) {
    Allocator allocator;
    const unsigned int thread_count = 5;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        const size_t iterations = tsan_scale(300 * (i + 1));
        threads.emplace_back(worker_alloc_free, &allocator, iterations);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_MappedBytesAfterParallelGrowth) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    const unsigned int thread_count = 4;

    std::vector<std::thread> allocators;
    for (unsigned int i = 0; i < thread_count; ++i) {
        allocators.emplace_back([&]() {
            auto blocks = allocate_blocks(allocator, blocks_per_page);
            EXPECT_EQ(blocks.size(), blocks_per_page);
        });
    }
    for (std::thread& thread : allocators) {
        thread.join();
    }

    EXPECT_GE(allocator.mapped_bytes(), 2 * Allocator::PAGE_SIZE);
    EXPECT_GE(allocator.active_page_count(), 2U);

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), thread_count * blocks_per_page);
}

TEST(Concurrency_FullDrainReachesZeroPages) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    const unsigned int thread_count = 4;
    std::vector<std::vector<void*>> all_blocks(thread_count);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            all_blocks[i] = allocate_blocks(allocator, blocks_per_page);
            flush_thread_cache(allocator);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_GE(allocator.active_page_count(), thread_count);

    for (unsigned int i = 0; i < thread_count; ++i) {
        std::thread releaser([&, i]() {
            deallocate_blocks(allocator, all_blocks[i]);
            flush_thread_cache(allocator);
        });
        releaser.join();
    }

    cma_test::flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(Concurrency_InterleavedAllocFreeNoLeak) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t iterations = tsan_scale(2500);
    std::atomic<size_t> max_local_live_seen{0};

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            std::vector<void*> live;
            size_t max_local_live = 0;
            for (size_t j = 0; j < iterations; ++j) {
                void* block = allocator.allocate();
                EXPECT_NOT_NULL(block);
                live.push_back(block);
                while (live.size() > 16) {
                    allocator.deallocate(live.back());
                    live.pop_back();
                }
                max_local_live = std::max(max_local_live, live.size());
            }
            size_t expected = max_local_live_seen.load();
            while (max_local_live > expected &&
                   !max_local_live_seen.compare_exchange_weak(expected, max_local_live)) {
            }
            deallocate_blocks(allocator, live);
            flush_thread_cache(allocator);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    cma_test::flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    EXPECT_LE(max_local_live_seen.load(), 16U);
}

TEST(Concurrency_DestructorSafeAfterMultithreadedUse) {
    const unsigned int thread_count = 4;
    const size_t iterations = tsan_scale(500);

    std::vector<std::thread> threads;
    {
        Allocator allocator;
        for (unsigned int i = 0; i < thread_count; ++i) {
            threads.emplace_back(worker_alloc_free, &allocator, iterations);
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        flush_thread_cache(allocator);
    }
}

TEST(Concurrency_WorkerFlushReturnsUnusedRefillBlocks) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    std::vector<void*> blocks;
    std::thread worker([&]() {
        blocks = allocate_blocks(allocator, blocks_per_page);
        flush_thread_cache(allocator);
    });
    worker.join();

    EXPECT_EQ(allocator.live_block_count(), blocks_per_page);
    EXPECT_GE(allocator.active_page_count(), 1U);

    deallocate_blocks(allocator, blocks);
    cma_test::flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(Concurrency_MainThreadFlushAfterWorkerCaches) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t iterations = tsan_scale(1000);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < iterations; ++j) {
                void* block = allocator.allocate();
                EXPECT_NOT_NULL(block);
                allocator.deallocate(block);
            }
            flush_thread_cache(allocator);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    cma_test::flush_thread_cache(allocator);
    cma_test::expect_stats_consistent(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_AlternatingGrowthAndReleaseCycles) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    const unsigned int thread_count = 4;

    for (int cycle = 0; cycle < 4; ++cycle) {
        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < thread_count; ++i) {
            threads.emplace_back([&]() {
                auto blocks = allocate_blocks(allocator, blocks_per_page / 4 + 1);
                deallocate_blocks(allocator, blocks);
                flush_thread_cache(allocator);
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        cma_test::flush_thread_cache(allocator);
        EXPECT_EQ(allocator.live_block_count(), 0U);
    }
}

TEST(Concurrency_BlocksRemainDistinctUnderContention) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t blocks_per_thread = 128;
    std::mutex results_mutex;
    std::vector<void*> all_blocks;
    all_blocks.reserve(thread_count * blocks_per_thread);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            auto blocks = allocate_blocks(allocator, blocks_per_thread);
            std::lock_guard<std::mutex> lock(results_mutex);
            all_blocks.insert(all_blocks.end(), blocks.begin(), blocks.end());
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    for (size_t i = 0; i < all_blocks.size(); ++i) {
        for (size_t j = i + 1; j < all_blocks.size(); ++j) {
            EXPECT_NE(all_blocks[i], all_blocks[j]);
        }
    }

    deallocate_blocks(allocator, all_blocks);
    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_NullDeallocateFromMultipleThreads) {
    Allocator allocator;
    const unsigned int thread_count = tsan_threads(8);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < 1000; ++j) {
                allocator.deallocate(nullptr);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_InvalidPointerDeallocateFromMultipleThreads) {
    Allocator allocator;
    void* invalid = allocator.allocate();
    EXPECT_NOT_NULL(invalid);
    allocator.deallocate(invalid);

    const uintptr_t page_base =
        reinterpret_cast<uintptr_t>(invalid) & ~(static_cast<uintptr_t>(Allocator::PAGE_ALIGNMENT) - 1);
    void* header_ptr = reinterpret_cast<void*>(page_base);

    const unsigned int thread_count = 4;
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < 500; ++j) {
                allocator.deallocate(header_ptr);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Concurrency_ConcurrentAllocateOnlyThenBulkFree) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t per_thread = 256;
    std::mutex blocks_mutex;
    std::vector<void*> all_blocks;
    all_blocks.reserve(thread_count * per_thread);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            auto blocks = allocate_blocks(allocator, per_thread);
            std::lock_guard<std::mutex> lock(blocks_mutex);
            all_blocks.insert(all_blocks.end(), blocks.begin(), blocks.end());
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(allocator.live_block_count(), all_blocks.size());

    std::thread bulk_freer([&]() {
        deallocate_blocks(allocator, all_blocks);
        flush_thread_cache(allocator);
    });
    bulk_freer.join();

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_ConcurrentBulkFreeByMultipleThreads) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t per_thread = 128;
    std::vector<std::vector<void*>> per_thread_blocks(thread_count);

    for (unsigned int i = 0; i < thread_count; ++i) {
        per_thread_blocks[i] = allocate_blocks(allocator, per_thread);
    }

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            deallocate_blocks(allocator, per_thread_blocks[i]);
            flush_thread_cache(allocator);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    cma_test::flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_SharedAllocatorScopedWorkers) {
    const size_t blocks_per_page = Allocator::blocks_per_page();

    for (int round = 0; round < 3; ++round) {
        Allocator allocator;
        std::vector<std::thread> threads;

        for (int t = 0; t < 3; ++t) {
            threads.emplace_back([&]() {
                auto blocks = allocate_blocks(allocator, blocks_per_page / 3);
                deallocate_blocks(allocator, blocks);
                flush_thread_cache(allocator);
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }

        flush_thread_cache(allocator);
        EXPECT_EQ(allocator.live_block_count(), 0U);
    }
}

TEST(Concurrency_LiveBytesTrackConcurrentAllocations) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t held_per_thread = 50;
    PhaseBarrier alloc_barrier(thread_count + 1);
    PhaseBarrier free_barrier(thread_count + 1);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            auto blocks = allocate_blocks(allocator, held_per_thread);
            alloc_barrier.arrive_and_wait();
            free_barrier.arrive_and_wait();
            deallocate_blocks(allocator, blocks);
            flush_thread_cache(allocator);
        });
    }

    alloc_barrier.arrive_and_wait();
    EXPECT_EQ(allocator.live_bytes(), thread_count * held_per_thread * kBlockSize);
    EXPECT_EQ(allocator.live_block_count(), thread_count * held_per_thread);
    free_barrier.arrive_and_wait();

    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}

TEST(Concurrency_CapacityInvariantUnderParallelAlloc) {
    Allocator allocator;
    const unsigned int thread_count = 4;
    const size_t per_thread = 200;
    PhaseBarrier alloc_barrier(thread_count + 1);
    PhaseBarrier free_barrier(thread_count + 1);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            auto blocks = allocate_blocks(allocator, per_thread);
            alloc_barrier.arrive_and_wait();
            free_barrier.arrive_and_wait();
            deallocate_blocks(allocator, blocks);
            flush_thread_cache(allocator);
        });
    }

    alloc_barrier.arrive_and_wait();
    cma_test::expect_stats_consistent(allocator);
    free_barrier.arrive_and_wait();

    for (std::thread& thread : threads) {
        thread.join();
    }

    flush_thread_cache(allocator);
    EXPECT_EQ(allocator.live_block_count(), 0U);
}
