#include "llvp/core/ring_buffer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>

template <typename T, std::size_t Size>
struct RingBufferTestAccess {
    static void set_empty_position(RingBuffer<T, Size>& ring,
                                   std::size_t position) noexcept {
        ring.producer_.head.store(position, std::memory_order_relaxed);
        ring.producer_.tail_cache = position;
        ring.consumer_.tail.store(position, std::memory_order_relaxed);
        ring.consumer_.head_cache = position;
    }
};

namespace {

int failures = 0;

void expect(bool condition, std::string_view expression, int line) {
    if (!condition) {
        std::cerr << "line " << line << ": expectation failed: "
                  << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

struct LifetimeValue {
    explicit LifetimeValue(int input) noexcept : value(input) { ++live; }
    LifetimeValue(const LifetimeValue&) = delete;
    LifetimeValue& operator=(const LifetimeValue&) = delete;
    LifetimeValue(LifetimeValue&& other) noexcept : value(other.value) {
        other.value = -1;
    }
    LifetimeValue& operator=(LifetimeValue&&) = delete;
    ~LifetimeValue() noexcept {
        --live;
        ++destroyed;
    }

    static inline int live = 0;
    static inline int destroyed = 0;
    int value;
};

struct alignas(128) OverAligned {
    explicit OverAligned(std::uintptr_t input) noexcept : value(input) {}
    ~OverAligned() noexcept = default;
    std::uintptr_t value;
};

struct ThrowingConstructor {
    explicit ThrowingConstructor(bool should_throw) {
        if (should_throw) {
            throw std::runtime_error("expected construction failure");
        }
        ++live;
    }
    ~ThrowingConstructor() noexcept { --live; }
    static inline int live = 0;
};

void test_empty_full_and_fifo() {
    RingBuffer<int, 4> ring;
    EXPECT(!ring.consume([](int&) noexcept {}));
    EXPECT(ring.emplace(10));
    EXPECT(ring.emplace(20));
    EXPECT(ring.emplace(30));
    EXPECT(ring.emplace(40));
    EXPECT(!ring.emplace(50));

    std::array<int, 4> values{};
    std::size_t index = 0;
    while (ring.consume([&](int& value) noexcept { values[index++] = value; })) {
    }
    EXPECT(index == values.size());
    EXPECT((values == std::array<int, 4>{10, 20, 30, 40}));
}

void test_capacity_one_and_repeated_wrap() {
    RingBuffer<std::size_t, 1> ring;
    for (std::size_t i = 0; i < 10'000; ++i) {
        EXPECT(ring.emplace(i));
        EXPECT(!ring.emplace(i + 1));
        std::size_t observed = 0;
        EXPECT(ring.consume(
            [&](std::size_t& value) noexcept { observed = value; }));
        EXPECT(observed == i);
    }
}

void test_move_only_and_lifetime() {
    LifetimeValue::live = 0;
    LifetimeValue::destroyed = 0;
    {
        RingBuffer<LifetimeValue, 4> ring;
        EXPECT(ring.emplace(1));
        EXPECT(ring.emplace(2));
        EXPECT(LifetimeValue::live == 2);
        EXPECT(ring.consume([](LifetimeValue& value) noexcept {
            EXPECT(value.value == 1);
        }));
        EXPECT(LifetimeValue::live == 1);
    }
    EXPECT(LifetimeValue::live == 0);
    EXPECT(LifetimeValue::destroyed == 2);

    RingBuffer<std::unique_ptr<int>, 2> pointers;
    EXPECT(pointers.push(std::make_unique<int>(42)));
    EXPECT(pointers.consume([](std::unique_ptr<int>& value) noexcept {
        EXPECT(value && *value == 42);
    }));

    RingBuffer<std::unique_ptr<int>, 1> full;
    EXPECT(full.push(std::make_unique<int>(1)));
    auto rejected = std::make_unique<int>(2);
    EXPECT(!full.push(std::move(rejected)));
    EXPECT(rejected && *rejected == 2);
}

void test_constructor_exception_does_not_publish() {
    ThrowingConstructor::live = 0;
    RingBuffer<ThrowingConstructor, 2> ring;
    bool threw = false;
    try {
        static_cast<void>(ring.emplace(true));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT(threw);
    EXPECT(ThrowingConstructor::live == 0);
    EXPECT(!ring.consume([](ThrowingConstructor&) noexcept {}));
    EXPECT(ring.emplace(false));
    EXPECT(ring.consume([](ThrowingConstructor&) noexcept {}));
    EXPECT(ThrowingConstructor::live == 0);
}

void test_over_aligned_storage() {
    RingBuffer<OverAligned, 2> ring;
    EXPECT(ring.emplace(123));
    EXPECT(ring.consume([](OverAligned& value) noexcept {
        EXPECT(reinterpret_cast<std::uintptr_t>(&value) % alignof(OverAligned) ==
               0);
        EXPECT(value.value == 123);
    }));
}

void test_counter_wrap_and_cleanup() {
    LifetimeValue::live = 0;
    LifetimeValue::destroyed = 0;
    {
        RingBuffer<LifetimeValue, 4> ring;
        constexpr auto near_wrap = std::numeric_limits<std::size_t>::max() - 1;
        RingBufferTestAccess<LifetimeValue, 4>::set_empty_position(ring,
                                                                  near_wrap);
        EXPECT(ring.emplace(1));
        EXPECT(ring.emplace(2));
        EXPECT(ring.emplace(3));
    }
    EXPECT(LifetimeValue::live == 0);
    EXPECT(LifetimeValue::destroyed == 3);
}

void test_concurrent_sequence() {
    constexpr std::uint64_t count = 500'000;
    RingBuffer<std::uint64_t, 1024> ring;
    std::atomic<bool> start{false};
    std::atomic<bool> valid{true};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint64_t value = 0; value < count; ++value) {
            while (!ring.emplace(value)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        start.store(true, std::memory_order_release);
        for (std::uint64_t expected = 0; expected < count;) {
            if (!ring.consume([&](std::uint64_t& value) noexcept {
                    if (value != expected) {
                        valid.store(false, std::memory_order_relaxed);
                    }
                    ++expected;
                })) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT(valid.load(std::memory_order_relaxed));
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<RingBuffer<int, 2>>);
    static_assert(!std::is_move_constructible_v<RingBuffer<int, 2>>);

    test_empty_full_and_fifo();
    test_capacity_one_and_repeated_wrap();
    test_move_only_and_lifetime();
    test_constructor_exception_does_not_publish();
    test_over_aligned_storage();
    test_counter_wrap_and_cleanup();
    test_concurrent_sequence();

    if (failures != 0) {
        std::cerr << failures << " test expectation(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ring buffer tests passed\n";
    return EXIT_SUCCESS;
}
