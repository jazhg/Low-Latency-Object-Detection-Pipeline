#include "llvp/metrics/summary.hpp"
#include "llvp/pipeline/frame_selector.hpp"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

struct Config {
    llvp::SelectionMode mode{llvp::SelectionMode::latest};
    std::uint64_t frames{90};
    std::uint64_t capture_period_ms{33};
    std::uint64_t service_ms{100};
    std::uint64_t capacity{8};
};

std::uint64_t parse_number(std::string_view value, std::string_view name) {
    std::uint64_t parsed = 0;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed == 0) {
        std::cerr << name << " must be a positive integer\n";
        std::exit(EXIT_FAILURE);
    }
    return parsed;
}

Config parse(int argc, char** argv) {
    Config config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--fifo") config.mode = llvp::SelectionMode::fifo;
        else if (arg == "--latest") config.mode = llvp::SelectionMode::latest;
        else if (arg == "--frames" && i + 1 < argc)
            config.frames = parse_number(argv[++i], "frames");
        else if (arg == "--capture-ms" && i + 1 < argc)
            config.capture_period_ms = parse_number(argv[++i], "capture-ms");
        else if (arg == "--service-ms" && i + 1 < argc)
            config.service_ms = parse_number(argv[++i], "service-ms");
        else if (arg == "--capacity" && i + 1 < argc)
            config.capacity = parse_number(argv[++i], "capacity");
        else {
            std::cerr << "usage: synthetic_pipeline [--fifo|--latest] "
                         "[--frames N] [--capture-ms N] [--service-ms N] "
                         "[--capacity 2|4|8|16|32]\n";
            std::exit(EXIT_FAILURE);
        }
    }
    return config;
}

template <std::size_t Capacity>
int run(const Config& config) {
    RingBuffer<llvp::Frame, Capacity> queue;
    llvp::Counters counters;
    std::atomic<bool> capture_done{false};
    std::vector<double> detection_ages_ms;
    detection_ages_ms.reserve(static_cast<std::size_t>(config.frames));
    const auto capture_period = std::chrono::milliseconds(config.capture_period_ms);
    const auto service_time = std::chrono::milliseconds(config.service_ms);
    const auto run_start = llvp::Clock::now();

    std::jthread producer([&] {
        auto deadline = llvp::Clock::now();
        for (std::uint64_t id = 0; id < config.frames; ++id) {
            std::this_thread::sleep_until(deadline);
            deadline += capture_period;
            const auto captured_at = llvp::Clock::now();
            counters.captured.fetch_add(1, std::memory_order_relaxed);
            if (queue.emplace(id, captured_at))
                counters.admitted.fetch_add(1, std::memory_order_relaxed);
            else
                counters.overflow_dropped.fetch_add(1,
                                                    std::memory_order_relaxed);
        }
        capture_done.store(true, std::memory_order_release);
    });

    std::jthread consumer([&] {
        while (true) {
            auto selection = llvp::select_frame(queue, config.mode);
            counters.stale_dropped.fetch_add(selection.stale_dropped,
                                             std::memory_order_relaxed);
            if (!selection.frame) {
                if (capture_done.load(std::memory_order_acquire)) break;
                std::this_thread::sleep_for(100us);
                continue;
            }
            std::this_thread::sleep_for(service_time);
            const auto age = std::chrono::duration<double, std::milli>(
                                 llvp::Clock::now() -
                                 selection.frame->captured_at)
                                 .count();
            detection_ages_ms.push_back(age);
            counters.processed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();
    const double elapsed =
        std::chrono::duration<double>(llvp::Clock::now() - run_start).count();
    const auto captured = counters.captured.load();
    const auto admitted = counters.admitted.load();
    const auto overflow = counters.overflow_dropped.load();
    const auto processed = counters.processed.load();
    const auto stale = counters.stale_dropped.load();
    if (captured != admitted + overflow || admitted != processed + stale) {
        std::cerr << "frame accounting invariant failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "mode="
              << (config.mode == llvp::SelectionMode::fifo ? "fifo" : "latest")
              << " capacity=" << Capacity << " captured=" << captured
              << " admitted=" << admitted << " processed=" << processed
              << " overflow_dropped=" << overflow << " stale_dropped=" << stale
              << " processing_fps=" << static_cast<double>(processed) / elapsed
              << " p50_detection_age_ms="
              << llvp::percentile_ms(detection_ages_ms, 0.50)
              << " p95_detection_age_ms="
              << llvp::percentile_ms(detection_ages_ms, 0.95)
              << " p99_detection_age_ms="
              << llvp::percentile_ms(detection_ages_ms, 0.99) << '\n';
    return EXIT_SUCCESS;
}
}  // namespace

int main(int argc, char** argv) {
    const auto config = parse(argc, argv);
    switch (config.capacity) {
        case 2: return run<2>(config);
        case 4: return run<4>(config);
        case 8: return run<8>(config);
        case 16: return run<16>(config);
        case 32: return run<32>(config);
        default:
            std::cerr << "capacity must be one of 2, 4, 8, 16, or 32\n";
            return EXIT_FAILURE;
    }
}
