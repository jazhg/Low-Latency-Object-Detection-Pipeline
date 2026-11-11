#pragma once

#include "llvp/core/frame.hpp"
#include "llvp/core/ring_buffer.hpp"

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace llvp {
enum class SelectionMode { fifo, latest };

template <typename FrameType>
struct Selection {
    std::optional<FrameType> frame;
    std::size_t stale_dropped{};
};

template <typename FrameType, std::size_t Capacity>
Selection<FrameType> select_frame(RingBuffer<FrameType, Capacity>& queue,
                                  SelectionMode mode) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<FrameType>);
    Selection<FrameType> result;
    if (mode == SelectionMode::fifo) {
        static_cast<void>(queue.consume([&](FrameType& frame) noexcept {
            result.frame.emplace(std::move(frame));
        }));
        return result;
    }

    // One snapshot bounds the drain even while capture keeps publishing frames.
    const auto consumed = queue.consume_snapshot([&](FrameType& frame) noexcept {
        result.frame.emplace(std::move(frame));
    });
    result.stale_dropped = consumed > 0 ? consumed - 1 : 0;
    return result;
}
}  // namespace llvp
