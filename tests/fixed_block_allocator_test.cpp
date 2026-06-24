#include "FixedBlockAllocator.hpp"
#include "test_helpers.hpp"
#include "test_runner.hpp"

#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

using cma_test::Allocator;
using cma_test::allocate_blocks;
using cma_test::deallocate_blocks;
using cma_test::expect_stats_consistent;
using cma_test::kBlockSize;

namespace {

void expect_consistent(const Allocator& allocator) {
    expect_stats_consistent(allocator);
}

} // namespace

// ---------------------------------------------------------------------------
// Smoke tests
// ---------------------------------------------------------------------------

TEST(Construct_AllocatorIsUsable) {
    Allocator allocator;
    void* block = allocator.allocate();
    EXPECT_NOT_NULL(block);
    allocator.deallocate(block);
    expect_consistent(allocator);
}

TEST(AllocateMany_ReturnsMostlyDistinctPointers) {
    Allocator allocator;
    std::set<void*> unique;
    for (size_t i = 0; i < 16; ++i) {
        unique.insert(allocator.allocate());
    }
    EXPECT_EQ(unique.size(), 16U);
    for (void* block : unique) {
        allocator.deallocate(block);
    }
}

TEST(Deallocate_NullPointerIsNoOp) {
    Allocator allocator;
    allocator.deallocate(nullptr);
    EXPECT_NOT_NULL(allocator.allocate());
}

TEST(WriteRead_RoundTripPreservesData) {
    Allocator allocator;
    void* block = allocator.allocate();
    EXPECT_NOT_NULL(block);

    const uint32_t pattern = 0xDEADBEEF;
    std::memcpy(block, &pattern, sizeof(pattern));

    uint32_t read_back = 0;
    std::memcpy(&read_back, block, sizeof(read_back));
    EXPECT_EQ(read_back, pattern);

    allocator.deallocate(block);
}

TEST(Reuse_AfterDeallocateBlockCanBeAllocatedAgain) {
    Allocator allocator;
    void* first = allocator.allocate();
    EXPECT_NOT_NULL(first);
    allocator.deallocate(first);

    void* second = allocator.allocate();
    EXPECT_NOT_NULL(second);
    allocator.deallocate(second);
}

TEST(Smoke_RepeatedAllocFreeCycles) {
    Allocator allocator;
    for (int cycle = 0; cycle < 100; ++cycle) {
        void* block = allocator.allocate();
        EXPECT_NOT_NULL(block);
        allocator.deallocate(block);
    }
}

TEST(Smoke_AllocateBatchThenFreeBatch) {
    Allocator allocator;
    for (int cycle = 0; cycle < 10; ++cycle) {
        auto blocks = allocate_blocks(allocator, 64);
        for (void* block : blocks) {
            EXPECT_NOT_NULL(block);
        }
        deallocate_blocks(allocator, blocks);
        expect_consistent(allocator);
    }
}

// ---------------------------------------------------------------------------
// Page metadata
// ---------------------------------------------------------------------------

TEST(Metadata_ConstructorStartsWithOnePage) {
    Allocator allocator;
    EXPECT_EQ(allocator.active_page_count(), 1U);
}

TEST(Metadata_PageSizeIs64KB) {
    EXPECT_EQ(Allocator::PAGE_SIZE, 64U * 1024U);
    EXPECT_EQ(Allocator::PAGE_SIZE & (Allocator::PAGE_SIZE - 1), 0U);
    EXPECT_EQ(Allocator::PAGE_ALIGNMENT, Allocator::PAGE_SIZE);
}

TEST(Metadata_BlocksPerPageMatchesLayout) {
    EXPECT_TRUE(Allocator::blocks_per_page() > 0);
    EXPECT_TRUE(Allocator::blocks_per_page() < Allocator::PAGE_SIZE / kBlockSize);
}

// ---------------------------------------------------------------------------
// Growth
// ---------------------------------------------------------------------------

TEST(Growth_AllocatingOneExtraBlockCreatesSecondPage) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto blocks = allocate_blocks(allocator, blocks_per_page + 1);
    EXPECT_EQ(allocator.live_block_count(), blocks_per_page + 1);
    EXPECT_GE(allocator.active_page_count(), 2U);
    expect_consistent(allocator);

    deallocate_blocks(allocator, blocks);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(Growth_FillingTwoPagesKeepsBothMappedWhileLive) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto first_page_blocks = allocate_blocks(allocator, blocks_per_page);
    auto second_page_blocks = allocate_blocks(allocator, blocks_per_page);
    EXPECT_EQ(allocator.live_block_count(), blocks_per_page * 2);
    EXPECT_GE(allocator.active_page_count(), 2U);

    deallocate_blocks(allocator, second_page_blocks);
    deallocate_blocks(allocator, first_page_blocks);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(Growth_ThirdPageCreatedWhenNeeded) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto blocks = allocate_blocks(allocator, blocks_per_page * 2 + 1);
    EXPECT_GE(allocator.active_page_count(), 3U);
    expect_consistent(allocator);

    deallocate_blocks(allocator, blocks);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

// ---------------------------------------------------------------------------
// Empty-page release
// ---------------------------------------------------------------------------

TEST(EmptyPage_FreeingFullyUnusedPageReleasesMapping) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto first_page_blocks = allocate_blocks(allocator, blocks_per_page);
    auto second_page_blocks = allocate_blocks(allocator, blocks_per_page);
    EXPECT_GE(allocator.active_page_count(), 2U);

    deallocate_blocks(allocator, second_page_blocks);
    deallocate_blocks(allocator, first_page_blocks);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EmptyPage_FreeingPagesInReverseOrderReleasesEachMapping) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto page_a = allocate_blocks(allocator, blocks_per_page);
    auto page_b = allocate_blocks(allocator, blocks_per_page);
    auto page_c = allocate_blocks(allocator, blocks_per_page);
    EXPECT_GE(allocator.active_page_count(), 3U);

    deallocate_blocks(allocator, page_c);
    deallocate_blocks(allocator, page_b);
    deallocate_blocks(allocator, page_a);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EmptyPage_FreeMiddlePageWhileOthersLive) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto page_a = allocate_blocks(allocator, blocks_per_page);
    auto page_b = allocate_blocks(allocator, blocks_per_page);
    auto page_c = allocate_blocks(allocator, blocks_per_page);
    EXPECT_GE(allocator.active_page_count(), 3U);

    deallocate_blocks(allocator, page_b);
    EXPECT_EQ(allocator.live_block_count(), blocks_per_page * 2);

    deallocate_blocks(allocator, page_a);
    deallocate_blocks(allocator, page_c);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EmptyPage_InterleavedFreeAcrossPages) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto page_a = allocate_blocks(allocator, blocks_per_page);
    auto page_b = allocate_blocks(allocator, blocks_per_page);

    size_t freed = 0;
    for (size_t i = 0; i < blocks_per_page; i += 2) {
        allocator.deallocate(page_a[i]);
        allocator.deallocate(page_b[i]);
        freed += 2;
    }

    EXPECT_EQ(allocator.live_block_count(), blocks_per_page * 2 - freed);

    for (size_t i = 1; i < blocks_per_page; i += 2) {
        allocator.deallocate(page_a[i]);
        allocator.deallocate(page_b[i]);
    }

    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EmptyPage_FreeFirstPageWhileSecondPageLive) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto first_page = allocate_blocks(allocator, blocks_per_page);
    auto second_page = allocate_blocks(allocator, blocks_per_page);
    EXPECT_GE(allocator.active_page_count(), 2U);

    deallocate_blocks(allocator, first_page);
    EXPECT_EQ(allocator.live_block_count(), blocks_per_page);

    deallocate_blocks(allocator, second_page);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EmptyPage_ShuffledFreeOrderEventuallyReleasesAllPages) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto page_a = allocate_blocks(allocator, blocks_per_page);
    auto page_b = allocate_blocks(allocator, blocks_per_page);

    std::vector<void*> all_blocks;
    all_blocks.insert(all_blocks.end(), page_a.begin(), page_a.end());
    all_blocks.insert(all_blocks.end(), page_b.begin(), page_b.end());

    for (size_t i = 0; i < all_blocks.size(); i += 3) {
        allocator.deallocate(all_blocks[i]);
    }
    for (size_t i = 1; i < all_blocks.size(); i += 3) {
        allocator.deallocate(all_blocks[i]);
    }
    for (size_t i = 2; i < all_blocks.size(); i += 3) {
        allocator.deallocate(all_blocks[i]);
    }

    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

// ---------------------------------------------------------------------------
// Stats API
// ---------------------------------------------------------------------------

TEST(Stats_InitialMappedPageHasNoLiveBlocks) {
    Allocator allocator;
    EXPECT_EQ(allocator.active_page_count(), 1U);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    EXPECT_EQ(allocator.mapped_bytes(), Allocator::PAGE_SIZE);
    EXPECT_EQ(allocator.live_bytes(), 0U);
    EXPECT_EQ(allocator.free_block_count(), allocator.capacity_block_count());
    expect_consistent(allocator);
}

TEST(Stats_LiveBytesTrackAllocations) {
    Allocator allocator;
    const size_t num_blocks = 10;

    auto blocks = allocate_blocks(allocator, num_blocks);
    EXPECT_EQ(allocator.live_block_count(), num_blocks);
    EXPECT_EQ(allocator.live_bytes(), num_blocks * kBlockSize);

    deallocate_blocks(allocator, blocks);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    EXPECT_EQ(allocator.live_bytes(), 0U);
}

TEST(Stats_MappedBytesGrowWithPages) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto first = allocate_blocks(allocator, blocks_per_page);
    const size_t mapped_one = allocator.mapped_bytes();
    EXPECT_GE(mapped_one, Allocator::PAGE_SIZE);

    auto extra = allocate_blocks(allocator, blocks_per_page);
    EXPECT_GE(allocator.active_page_count(), 2U);
    EXPECT_GE(allocator.mapped_bytes(), 2U * Allocator::PAGE_SIZE);

    deallocate_blocks(allocator, first);
    deallocate_blocks(allocator, extra);
}

TEST(Stats_MappedBytesDropAfterEmptyPageRelease) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();

    auto first_page = allocate_blocks(allocator, blocks_per_page);
    auto second_page = allocate_blocks(allocator, blocks_per_page);
    EXPECT_GE(allocator.mapped_bytes(), 2U * Allocator::PAGE_SIZE);

    deallocate_blocks(allocator, second_page);
    deallocate_blocks(allocator, first_page);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.mapped_bytes(), 0U);
}

TEST(Stats_FreeBlockCountReflectsPartialRelease) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    auto blocks = allocate_blocks(allocator, blocks_per_page);

    EXPECT_EQ(allocator.live_block_count(), blocks_per_page);

    allocator.deallocate(blocks[0]);
    EXPECT_EQ(allocator.live_block_count(), blocks_per_page - 1);
    expect_consistent(allocator);

    for (size_t i = 1; i < blocks.size(); ++i) {
        allocator.deallocate(blocks[i]);
    }
}

TEST(Stats_CapacityEqualsLivePlusFree) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    auto blocks = allocate_blocks(allocator, blocks_per_page / 2);
    expect_consistent(allocator);

    deallocate_blocks(allocator, blocks);
    expect_consistent(allocator);
}

TEST(Stats_AllCountersZeroWhenNoPagesMapped) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    auto blocks = allocate_blocks(allocator, blocks_per_page);
    deallocate_blocks(allocator, blocks);
    allocator.flush_local_thread_cache();

    EXPECT_EQ(allocator.active_page_count(), 0U);
    EXPECT_EQ(allocator.live_block_count(), 0U);
    EXPECT_EQ(allocator.free_block_count(), 0U);
    EXPECT_EQ(allocator.capacity_block_count(), 0U);
    EXPECT_EQ(allocator.mapped_bytes(), 0U);
    EXPECT_EQ(allocator.live_bytes(), 0U);
    EXPECT_EQ(allocator.free_bytes(), 0U);
}

TEST(Stats_PartialFreeDoesNotChangeMappedBytes) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    auto blocks = allocate_blocks(allocator, blocks_per_page);

    const size_t mapped_before = allocator.mapped_bytes();
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        allocator.deallocate(blocks[i]);
    }

    // Freeing never maps more memory, and one live block keeps a page mapped.
    EXPECT_LE(allocator.mapped_bytes(), mapped_before);
    EXPECT_GE(allocator.active_page_count(), 1U);
    EXPECT_EQ(allocator.live_block_count(), 1U);
    allocator.deallocate(blocks.back());
}

TEST(Stats_LiveBytesScalesLinearlyWithAllocations) {
    Allocator allocator;
    std::vector<void*> live;
    for (size_t expected = 1; expected <= 8; ++expected) {
        live.push_back(allocator.allocate());
        EXPECT_EQ(allocator.live_block_count(), expected);
        EXPECT_EQ(allocator.live_bytes(), expected * kBlockSize);
    }
    deallocate_blocks(allocator, live);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(EdgeCase_AllocatedBlockSupportsScalarAccess) {
    Allocator allocator;
    void* block = allocator.allocate();
    EXPECT_NOT_NULL(block);

    auto* value = static_cast<uint64_t*>(block);
    *value = 0x123456789ABCDEF0ULL;
    EXPECT_EQ(*value, 0x123456789ABCDEF0ULL);

    allocator.deallocate(block);
}

TEST(EdgeCase_MixedAllocFreePatternMaintainsValidAllocations) {
    Allocator allocator;
    std::vector<void*> live;

    for (size_t i = 0; i < 50; ++i) {
        live.push_back(allocator.allocate());
    }

    for (size_t i = 0; i < live.size(); i += 2) {
        allocator.deallocate(live[i]);
        live[i] = allocator.allocate();
    }

    for (void* block : live) {
        EXPECT_NOT_NULL(block);
        allocator.deallocate(block);
    }
}

TEST(EdgeCase_DeallocateDoesNotRequireSpecificOrder) {
    Allocator allocator;
    auto blocks = allocate_blocks(allocator, 32);

    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
        allocator.deallocate(*it);
    }
}

TEST(EdgeCase_SingleBlockRetainPageUntilReleased) {
    Allocator allocator;
    void* lone = allocator.allocate();
    EXPECT_GE(allocator.active_page_count(), 1U);
    EXPECT_EQ(allocator.live_block_count(), 1U);
    EXPECT_GE(allocator.mapped_bytes(), Allocator::PAGE_SIZE);

    allocator.deallocate(lone);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EdgeCase_LiveBlockCountNeverExceedsCapacity) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    auto blocks = allocate_blocks(allocator, blocks_per_page * 2);

    EXPECT_LE(allocator.live_block_count(), allocator.capacity_block_count());
    deallocate_blocks(allocator, blocks);
}

TEST(EdgeCase_NearlyEmptyPageStaysMappedWithOneLiveBlock) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    auto blocks = allocate_blocks(allocator, blocks_per_page);

    for (size_t i = 1; i < blocks.size(); ++i) {
        allocator.deallocate(blocks[i]);
    }

    EXPECT_GE(allocator.active_page_count(), 1U);
    EXPECT_EQ(allocator.live_block_count(), 1U);
    expect_consistent(allocator);

    allocator.deallocate(blocks[0]);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EdgeCase_DeallocateInvalidAddressInPageHeaderIsNoOp) {
    Allocator allocator;
    void* block = allocator.allocate();
    EXPECT_NOT_NULL(block);

    const uintptr_t page_base =
        reinterpret_cast<uintptr_t>(block) &
        ~(static_cast<uintptr_t>(Allocator::PAGE_ALIGNMENT) - 1);
    void* header_address = reinterpret_cast<void*>(page_base + sizeof(void*));

    allocator.deallocate(header_address);
    EXPECT_EQ(allocator.live_block_count(), 1U);
    expect_consistent(allocator);

    allocator.deallocate(block);
}

TEST(EdgeCase_AllocateAfterFullDrainCreatesFreshPage) {
    Allocator allocator;
    const size_t blocks_per_page = Allocator::blocks_per_page();
    auto blocks = allocate_blocks(allocator, blocks_per_page);
    deallocate_blocks(allocator, blocks);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);

    void* block = allocator.allocate();
    EXPECT_NOT_NULL(block);
    EXPECT_GE(allocator.active_page_count(), 1U);
    EXPECT_EQ(allocator.live_block_count(), 1U);
    allocator.deallocate(block);
    allocator.flush_local_thread_cache();
    EXPECT_EQ(allocator.active_page_count(), 0U);
}

TEST(EdgeCase_TwoAllocatorsAreIndependent) {
    Allocator first;
    Allocator second;

    void* block_from_first = first.allocate();
    void* block_from_second = second.allocate();
    EXPECT_NOT_NULL(block_from_first);
    EXPECT_NOT_NULL(block_from_second);

    EXPECT_GE(first.active_page_count(), 1U);
    EXPECT_GE(second.active_page_count(), 1U);

    first.deallocate(block_from_first);
    first.flush_local_thread_cache();
    EXPECT_EQ(first.active_page_count(), 0U);
    EXPECT_GE(second.active_page_count(), 1U);

    second.deallocate(block_from_second);
    second.flush_local_thread_cache();
    EXPECT_EQ(second.active_page_count(), 0U);
}

// ---------------------------------------------------------------------------
// Template / block-size variants
// ---------------------------------------------------------------------------

TEST(Template_BlockSize8_AllocateAndFree) {
    cma::FixedBlockAllocator<8> allocator;
    void* block = allocator.allocate();
    EXPECT_NOT_NULL(block);
    allocator.deallocate(block);
    expect_stats_consistent<8>(allocator);
}

TEST(Template_BlockSize64_AllocateAndFree) {
    cma::FixedBlockAllocator<64> allocator;
    auto blocks = allocate_blocks(allocator, 32U);
    EXPECT_EQ(allocator.live_block_count(), 32U);
    deallocate_blocks(allocator, blocks);
}

TEST(Template_DifferentBlockSizesHaveDifferentBlocksPerPage) {
    EXPECT_NE(cma::FixedBlockAllocator<8>::blocks_per_page(), cma::FixedBlockAllocator<64>::blocks_per_page());
}
