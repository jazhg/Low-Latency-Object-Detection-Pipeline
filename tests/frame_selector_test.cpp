#include "llvp/pipeline/frame_selector.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    int failures = 0;
    RingBuffer<llvp::Frame, 8> queue;
    const auto now = llvp::Clock::now();
    for (std::uint64_t id = 1; id <= 4; ++id) {
        if (!queue.emplace(id, now)) ++failures;
    }
    auto latest = llvp::select_frame(queue, llvp::SelectionMode::latest);
    if (!latest.frame || latest.frame->id != 4 || latest.stale_dropped != 3)
        ++failures;
    if (llvp::select_frame(queue, llvp::SelectionMode::latest).frame) ++failures;
    if (!queue.emplace(5, now) || !queue.emplace(6, now)) ++failures;
    auto fifo = llvp::select_frame(queue, llvp::SelectionMode::fifo);
    if (!fifo.frame || fifo.frame->id != 5 || fifo.stale_dropped != 0) ++failures;
    auto next = llvp::select_frame(queue, llvp::SelectionMode::fifo);
    if (!next.frame || next.frame->id != 6) ++failures;
    if (failures != 0) {
        std::cerr << failures << " selector checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All frame selector tests passed\n";
    return EXIT_SUCCESS;
}
