#include "llvp/core/ring_buffer.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
struct Result {
    std::size_t capacity;
    std::uint64_t items;
    double elapsed_seconds;
    double items_per_second;
    double amortized_ns_per_item;
    std::uint64_t checksum;
};

template <std::size_t Capacity>
Result run(std::uint64_t items) {
    RingBuffer<std::uint64_t, Capacity> ring;
    std::atomic<bool> start{false};
    std::uint64_t checksum = 0;
    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::uint64_t value = 1; value <= items; ++value) {
            while (!ring.emplace(value)) std::this_thread::yield();
        }
    });
    const auto begin = std::chrono::steady_clock::now();
    std::thread consumer([&] {
        start.store(true, std::memory_order_release);
        std::uint64_t consumed = 0;
        while (consumed < items) {
            if (!ring.consume([&](std::uint64_t& value) noexcept {
                    checksum += value;
                    ++consumed;
                })) {
                std::this_thread::yield();
            }
        }
    });
    producer.join();
    consumer.join();
    const double elapsed = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - begin)
                               .count();
    return {Capacity, items, elapsed, static_cast<double>(items) / elapsed,
            elapsed * 1'000'000'000.0 / static_cast<double>(items), checksum};
}

std::uint64_t parse_items(int argc, char** argv) {
    if (argc == 1) return 10'000'000;
    if (argc != 2) {
        std::cerr << "usage: ring_benchmark [positive-item-count]\n";
        std::exit(EXIT_FAILURE);
    }
    const std::string_view input(argv[1]);
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(),
                                        result);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
        result == 0) {
        std::cerr << "item count must be a positive integer\n";
        std::exit(EXIT_FAILURE);
    }
    return result;
}

void print(const Result& result) {
    const auto expected = result.items * (result.items + 1) / 2;
    if (result.checksum != expected) {
        std::cerr << "checksum failure for capacity " << result.capacity << '\n';
        std::exit(EXIT_FAILURE);
    }
    std::cout << result.capacity << ',' << result.items << ','
              << result.elapsed_seconds << ',' << result.items_per_second << ','
              << result.amortized_ns_per_item << ',' << result.checksum << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    const auto items = parse_items(argc, argv);
    std::cout << "capacity_entries,items,elapsed_seconds,items_per_second,"
                 "amortized_ns_per_item,checksum\n";
    print(run<1U << 10U>(items));
    print(run<1U << 14U>(items));
    print(run<1U << 18U>(items));
    return EXIT_SUCCESS;
}
