#include "FixedBlockAllocator.hpp"
#include "test_helpers.hpp"
#include "test_runner.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using cma_test::Allocator;
using cma_test::allocate_blocks;
using cma_test::deallocate_blocks;
using cma_test::expect_stats_consistent;
using cma_test::kBlockSize;

// ---------------------------------------------------------------------------
// Integration: allocator + platform memory lifecycle
// ---------------------------------------------------------------------------

TEST(Integration_FillDrainRefillCycle) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    for (int cycle = 0; cycle < 3; ++cycle) {
        auto blocks = allocate_blocks(allocator, blocks_per_page);
        EXPECT_EQ(allocator.live_block_count(), blocks_per_page);
        EXPECT_GE(allocator.mapped_bytes(), Allocator::PAGE_SIZE);

        deallocate_blocks(allocator, blocks);
        allocator.flush_local_thread_cache();
        EXPECT_EQ(allocator.active_page_count(), 0U);
        EXPECT_EQ(allocator.mapped_bytes(), 0U);

        void* block = allocator.allocate();
        EXPECT_NOT_NULL(block);
        allocator.deallocate(block);
        cma_test::expect_stats_consistent(allocator);
    }
}

TEST(Integration_SawtoothWorkloadUpdatesStats) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    std::vector<void*> live;

    for (size_t target = 1; target <= blocks_per_page; target *= 2) {
        while (live.size() < target) {
            live.push_back(allocator.allocate());
        }
        EXPECT_EQ(allocator.live_block_count(), live.size());
        cma_test::expect_stats_consistent(allocator);
    }

    while (!live.empty()) {
        allocator.deallocate(live.back());
        live.pop_back();
        cma_test::expect_stats_consistent(allocator);
    }

    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.mapped_bytes(), 0U);
}

TEST(Integration_MultiPageWorkloadThenPartialRelease) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto page_one = allocate_blocks(allocator, blocks_per_page);
    auto page_two = allocate_blocks(allocator, blocks_per_page);
    auto page_three = allocate_blocks(allocator, blocks_per_page / 2);

    EXPECT_GE(allocator.active_page_count(), 3U);
    EXPECT_EQ(allocator.live_block_count(), blocks_per_page * 2 + blocks_per_page / 2);

    deallocate_blocks(allocator, page_two);
    EXPECT_EQ(allocator.live_block_count(), blocks_per_page + blocks_per_page / 2);

    deallocate_blocks(allocator, page_one);
    deallocate_blocks(allocator, page_three);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(Integration_BlockDataSurvivesUntilDeallocation) {
    Allocator allocator;
    const size_t count = 16;
    auto blocks = allocate_blocks(allocator, count);

    for (size_t i = 0; i < blocks.size(); ++i) {
        std::memset(blocks[i], static_cast<int>(i), kBlockSize);
    }

    for (size_t i = 0; i < blocks.size(); ++i) {
        EXPECT_EQ(static_cast<unsigned char*>(blocks[i])[0], static_cast<unsigned char>(i));
    }

    deallocate_blocks(allocator, blocks);
}

TEST(Integration_LargeBatchAllocFreeMaintainsConsistency) {
    Allocator allocator;
    const size_t total = Allocator::blocks_per_page() * 4;
    auto blocks = allocate_blocks(allocator, total);

    EXPECT_GE(allocator.active_page_count(), 4U);
    cma_test::expect_stats_consistent(allocator);

    deallocate_blocks(allocator, blocks);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
    cma_test::expect_stats_consistent(allocator);
}

TEST(Integration_ScopedDestructorWithLiveBlocksDoesNotCrash) {
    {
        Allocator allocator;
        (void)allocator.allocate();
        (void)allocator.allocate();
    }
}

TEST(Integration_AllocateFreeRhythmAcrossMultiplePages) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    for (size_t round = 0; round < 4; ++round) {
        std::vector<void*> live;
        for (size_t i = 0; i < blocks_per_page; ++i) {
            live.push_back(allocator.allocate());
            if ((i + round) % 3 == 0 && !live.empty()) {
                allocator.deallocate(live.back());
                live.pop_back();
            }
        }
        deallocate_blocks(allocator, live);
        cma_test::expect_stats_consistent(allocator);
    }
}

TEST(Integration_RepeatedPartialPageUsage) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    for (int cycle = 0; cycle < 5; ++cycle) {
        auto blocks = allocate_blocks(allocator, blocks_per_page / 4);
        EXPECT_EQ(allocator.active_page_count(), 1U);
        deallocate_blocks(allocator, blocks);
        cma_test::expect_stats_consistent(allocator);
    }
}
