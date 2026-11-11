#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace llvp {
struct Counters {
    std::atomic<std::uint64_t> captured{0};
    std::atomic<std::uint64_t> admitted{0};
    std::atomic<std::uint64_t> overflow_dropped{0};
    std::atomic<std::uint64_t> processed{0};
    std::atomic<std::uint64_t> stale_dropped{0};
};

inline double percentile_ms(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        fraction * static_cast<double>(values.size() - 1));
    return values[index];
}
}  // namespace llvp
