#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template <typename T, std::size_t Size>
class RingBuffer;

template <typename T, std::size_t Size>
struct RingBufferTestAccess;

/**
 * A bounded single-producer/single-consumer queue.
 *
 * Exactly one thread may call producer operations and exactly one other thread
 * may call consumer operations. References supplied to consume callbacks are
 * valid only for the duration of the callback. Callbacks must not throw or
 * re-enter the queue. Both threads must be stopped before destruction.
 *
 * Whether the queue is lock-free depends on the target implementation of
 * std::atomic<std::size_t>.
 */
template <typename T, std::size_t Size>
class RingBuffer {
    static_assert(Size > 0, "RingBuffer capacity must be greater than zero");
    static_assert((Size & (Size - 1)) == 0,
                  "RingBuffer capacity must be a power of two");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "RingBuffer elements must have a noexcept destructor");

    static constexpr std::size_t kSpatialPadding = 256;
    static constexpr std::size_t kMask = Size - 1;

    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];

        [[nodiscard]] T* raw_ptr() noexcept {
            return reinterpret_cast<T*>(storage);
        }

        [[nodiscard]] T* object_ptr() noexcept {
            // A slot can hold many successive objects; access the current lifetime.
            return std::launder(raw_ptr());
        }
    };

    alignas(kSpatialPadding) struct ProducerState {
        std::atomic<std::size_t> head{0};
        std::size_t tail_cache{0};
    } producer_;

    alignas(kSpatialPadding) struct ConsumerState {
        std::atomic<std::size_t> tail{0};
        std::size_t head_cache{0};
    } consumer_;

    alignas(kSpatialPadding) std::array<Slot, Size> buffer_;

    friend struct RingBufferTestAccess<T, Size>;

public:
    RingBuffer() = default;

    ~RingBuffer() {
        // Destruction is only valid after producer and consumer have stopped.
        auto tail = consumer_.tail.load(std::memory_order_relaxed);
        const auto head = producer_.head.load(std::memory_order_relaxed);
        while (tail != head) {
            std::destroy_at(buffer_[tail & kMask].object_ptr());
            ++tail;
        }
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    template <typename... Args>
    [[nodiscard]] bool emplace(Args&&... args) {
        // Only the producer writes head, so reading its own position needs no acquire.
        const auto head = producer_.head.load(std::memory_order_relaxed);

        if (head - producer_.tail_cache >= Size) [[unlikely]] {
            // Refresh the cached tail only when the queue appears full.
            // Acquire observes the consumer's completed destruction before slot reuse.
            producer_.tail_cache =
                consumer_.tail.load(std::memory_order_acquire);
            if (head - producer_.tail_cache >= Size) {
                return false;
            }
        }

        // Masking wraps the index. Construct first so a thrown constructor publishes nothing.
        std::construct_at(buffer_[head & kMask].raw_ptr(),
                          std::forward<Args>(args)...);
        // Release makes the completed object visible to the consumer's acquire load.
        producer_.head.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool push(const T& item) { return emplace(item); }
    [[nodiscard]] bool push(T&& item) { return emplace(std::move(item)); }

    template <typename F>
    [[nodiscard]] bool consume(F&& callback) noexcept {
        static_assert(std::is_nothrow_invocable_v<F, T&>,
                      "RingBuffer consume callbacks must be noexcept");

        const auto tail = consumer_.tail.load(std::memory_order_relaxed);

        if (tail == consumer_.head_cache) [[unlikely]] {
            // Refresh only when apparently empty; acquire makes published objects visible.
            consumer_.head_cache =
                producer_.head.load(std::memory_order_acquire);
            if (tail == consumer_.head_cache) {
                return false;
            }
        }

        // Finish the callback and destruction before allowing the producer to reuse this slot.
        T* item = buffer_[tail & kMask].object_ptr();
        std::forward<F>(callback)(*item);
        std::destroy_at(item);
        consumer_.tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    /** Consume exactly the elements visible in one acquired producer snapshot. */
    template <typename F>
    [[nodiscard]] std::size_t consume_snapshot(F&& callback) noexcept {
        static_assert(std::is_nothrow_invocable_v<F, T&>,
                      "RingBuffer snapshot callbacks must be noexcept");

        auto tail = consumer_.tail.load(std::memory_order_relaxed);
        // Fix the stopping point so new arrivals cannot extend this loop indefinitely.
        const auto snapshot = producer_.head.load(std::memory_order_acquire);
        std::size_t consumed = 0;
        while (tail != snapshot) {
            T* item = buffer_[tail & kMask].object_ptr();
            std::forward<F>(callback)(*item);
            std::destroy_at(item);
            ++tail;
            ++consumed;
            consumer_.tail.store(tail, std::memory_order_release);
        }
        consumer_.head_cache = snapshot;
        return consumed;
    }
};
