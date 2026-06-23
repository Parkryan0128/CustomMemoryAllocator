#pragma once

#include <cstddef>

namespace cma {

/**
 * Maps a virtual memory region from the operating system.
 * @return The starting address, or nullptr on failure.
 */
void* map_page(size_t size);

/**
 * Unmaps a previously mapped region. No-op when @p ptr is nullptr.
 */
void unmap_page(void* ptr, size_t size);

} // namespace cma
