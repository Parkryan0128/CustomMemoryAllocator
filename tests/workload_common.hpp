#pragma once

#include <cstddef>
#include <thread>
#include <vector>

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

inline unsigned int default_thread_count() {
    const unsigned int hw = std::thread::hardware_concurrency();
    const unsigned int count = hw > 0 ? hw : 4U;
#ifdef CMA_TSAN_BUILD
    return count > 2 ? 2U : count;
#else
    return count;
#endif
}

template <typename Alloc, typename Free>
void run_random_mix(size_t operations, unsigned int seed, Alloc alloc, Free free_fn) {
    std::vector<void*> live;
    live.reserve(64);

    for (size_t op = 0; op < operations; ++op) {
        const size_t salt = random_mix_salt(op, seed);
        if (random_mix_should_alloc(live.size(), salt)) {
            live.push_back(alloc(op));
        } else {
            const size_t index = salt % live.size();
            void* block = live[index];
            live[index] = live.back();
            live.pop_back();
            free_fn(block);
        }
    }

    for (void* block : live) {
        free_fn(block);
    }
}

} // namespace workload
