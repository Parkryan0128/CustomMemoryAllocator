#pragma once

#include <cstddef>

namespace workload {

inline size_t random_mix_salt(size_t op, unsigned int seed = 0U) {
    return (static_cast<size_t>(seed) + 1U) * 1315423911U + op * 2654435761U;
}

// ~50% alloc / ~50% free when live is non-empty. Uses upper hash bits so the
// decision does not correlate with op parity (which broke salt & 1).
inline bool random_mix_should_alloc(size_t live_count, size_t salt) {
    if (live_count == 0U) {
        return true;
    }
    return ((salt >> 16U) & 1U) == 0U;
}

} // namespace workload
