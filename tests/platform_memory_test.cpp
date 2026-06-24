#include "PlatformMemory.hpp"
#include "test_runner.hpp"

#include <cstring>

using cma::map_page;
using cma::unmap_page;

TEST(PlatformMemory_MapPage_MemoryIsWritable) {
    void* page = map_page(4096);
    EXPECT_NOT_NULL(page);

    std::memset(page, 0xAB, 4096);
    EXPECT_EQ(static_cast<unsigned char*>(page)[0], 0xAB);
    EXPECT_EQ(static_cast<unsigned char*>(page)[4095], 0xAB);

    unmap_page(page, 4096);
}

TEST(PlatformMemory_UnmapPage_NullPointerIsNoOp) {
    unmap_page(nullptr, 4096);
}

TEST(PlatformMemory_MapAndUnmap_MultipleRoundTrips) {
    for (int i = 0; i < 8; ++i) {
        void* page = map_page(8192);
        EXPECT_NOT_NULL(page);
        unmap_page(page, 8192);
    }
}

TEST(PlatformMemory_MapPage64KB_MatchesAllocatorPageSize) {
    constexpr size_t page_size = 64 * 1024;
    void* page = map_page(page_size);
    EXPECT_NOT_NULL(page);
    unmap_page(page, page_size);
}

TEST(PlatformMemory_TwoMappingsReturnDistinctAddresses) {
    void* first = map_page(4096);
    void* second = map_page(4096);
    EXPECT_NOT_NULL(first);
    EXPECT_NOT_NULL(second);
    EXPECT_NE(first, second);

    unmap_page(first, 4096);
    unmap_page(second, 4096);
}

TEST(PlatformMemory_RemappedRegionIsWritableAfterPriorUnmap) {
    void* first = map_page(4096);
    EXPECT_NOT_NULL(first);
    unmap_page(first, 4096);

    void* second = map_page(4096);
    EXPECT_NOT_NULL(second);
    std::memset(second, 0x5A, 4096);
    EXPECT_EQ(static_cast<unsigned char*>(second)[0], 0x5A);
    unmap_page(second, 4096);
}
