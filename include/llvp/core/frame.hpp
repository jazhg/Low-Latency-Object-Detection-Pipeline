#pragma once

#include <chrono>
#include <cstdint>

namespace llvp {
using Clock = std::chrono::steady_clock;

struct Frame {
    std::uint64_t id{};
    Clock::time_point captured_at{};

    Frame() = default;
    Frame(std::uint64_t frame_id, Clock::time_point timestamp) noexcept
        : id(frame_id), captured_at(timestamp) {}
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&) noexcept = default;
    Frame& operator=(Frame&&) noexcept = default;
    ~Frame() noexcept = default;
};
}  // namespace llvp
