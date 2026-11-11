#include "llvp/capture/opencv_source.hpp"
#include "llvp/inference/torch_detector.hpp"
#include "llvp/metrics/summary.hpp"
#include "llvp/pipeline/video_pipeline.hpp"
#include "llvp/pipeline/frame_selector.hpp"
#include "llvp/display/draw_detections.hpp"

#include <opencv2/highgui.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace llvp {
namespace {
struct ErrorState {
    std::mutex mutex;
    std::exception_ptr error;
    void capture() noexcept {
        // Both workers can fail. Keep the first error for the main thread to report.
        std::lock_guard lock(mutex);
        if (!error) error = std::current_exception();
    }
};

// All shared state outlives both workers. Only capture pushes; only inference pops.
class VideoPipeline {
public:
    explicit VideoPipeline(const Config& options) : config(options) {}

    int run() {
        // Workers share this object's state; the main thread handles window events.
        std::jthread producer([this] { capture_frames(); });
        std::jthread consumer;
        try {
            consumer = std::jthread([this] { process_frames(); });
            display_results();
        } catch (...) {
            stop.store(true, std::memory_order_release);
            throw; // jthread joins workers before their shared state is destroyed.
        }
        // Wait before reading final counts or destroying any queued frames.
        producer.join();
        consumer.join();
        if (config.display) cv::destroyAllWindows();
        if (errors.error) std::rethrow_exception(errors.error);
        return print_summary();
    }

private:
    const Config& config;

    static constexpr std::size_t capacity = 8;
    RingBuffer<llvp::VideoFrame, capacity> queue;
    llvp::Counters counters;
    std::atomic<bool> producer_done{false}, stop{false};
    std::atomic<bool> consumer_done{false};
    ErrorState errors;
    std::vector<double> ages;
    std::mutex result_mutex;
    std::optional<cv::Mat> latest_result;
    std::atomic<std::uint64_t> result_replaced{0};
    const llvp::Clock::time_point started = llvp::Clock::now();

    std::unique_ptr<OpenCvSource> open_source() const {
        // camera:N selects a device; other strings are passed to the file backend.
        if (config.source.rfind("camera:", 0) == 0) {
            return std::make_unique<OpenCvSource>(std::stoi(config.source.substr(7)));
        }
        return std::make_unique<OpenCvSource>(config.source);
    }

    bool time_limit_reached() const {
        // A zero duration disables the limit. The clock includes startup time.
        const double elapsed =
            std::chrono::duration<double>(Clock::now() - started).count();
        return config.duration_seconds > 0.0 && elapsed >= config.duration_seconds;
    }

    void enqueue_frame(VideoFrame&& frame) {
        // A full queue rejects the new frame; capture must not remove old frames.
        counters.captured.fetch_add(1, std::memory_order_relaxed);
        if (queue.push(std::move(frame))) {
            counters.admitted.fetch_add(1, std::memory_order_relaxed);
        } else {
            counters.overflow_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // The capture worker owns the source and publishes owned images.
    void capture_frames() {
        try {
            auto source = open_source();
            const double fps = source->frames_per_second();
            // Pace file playback at the reported FPS instead of reading it all at once.
            const auto period = fps > 0.0
                                    ? std::chrono::duration_cast<llvp::Clock::duration>(
                                          std::chrono::duration<double>(1.0 / fps))
                                    : llvp::Clock::duration::zero();
            auto deadline = llvp::Clock::now();
            while (!stop.load(std::memory_order_acquire)) {
                if (time_limit_reached()) {
                    // Request shutdown; queued leftovers are counted in the summary.
                    stop.store(true, std::memory_order_release);
                    break;
                }
                llvp::VideoFrame frame;
                const auto status = source->read(frame);
                if (status == llvp::ReadStatus::end_of_stream) break;
                if (status == llvp::ReadStatus::error)
                    throw std::runtime_error("capture read failed");
                enqueue_frame(std::move(frame));
                if (period != llvp::Clock::duration::zero()) {
                    deadline += period;
                    std::this_thread::sleep_until(deadline);
                }
            }
        } catch (...) {
            errors.capture();
            stop.store(true, std::memory_order_release);
        }
        producer_done.store(true, std::memory_order_release);
    }

    // The inference worker releases queue slots before running the model.
    void process_frames() {
        try {
            llvp::TorchDetector detector(config.model, config.ops);
            // Load the model once, then reuse it for every selected image.
            while (!stop.load(std::memory_order_acquire)) {
                auto selection = select_frame(queue, config.latest ? SelectionMode::latest
                                                                  : SelectionMode::fifo);
                counters.stale_dropped.fetch_add(selection.stale_dropped,
                                                 std::memory_order_relaxed);
                auto& selected = selection.frame;
                if (!selected) {
                    // An empty queue can be temporary while capture is still running.
                    if (producer_done.load(std::memory_order_acquire)) break;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    continue;
                }
                const auto detections = detector.detect(selected->image);
                if (config.display) {
                    // Replace an unseen result so a slow display cannot build a backlog.
                    cv::Mat annotated = draw_detections(selected->image, detections);
                    std::lock_guard lock(result_mutex);
                    if (latest_result) result_replaced.fetch_add(1);
                    latest_result.emplace(std::move(annotated));
                }
                // Measure from application receipt, including waiting and annotation.
                // Only this worker writes ages; the main thread reads it after join.
                ages.push_back(std::chrono::duration<double, std::milli>(
                                   llvp::Clock::now() - selected->captured_at)
                                   .count());
                counters.processed.fetch_add(1, std::memory_order_relaxed);
            }
        } catch (...) {
            errors.capture();
            stop.store(true, std::memory_order_release);
        }
        consumer_done.store(true, std::memory_order_release);
    }

    // macOS requires window events to be handled on the main thread.
    void display_results() {
        while (!consumer_done.load(std::memory_order_acquire)) {
            if (!config.display) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            std::optional<cv::Mat> result;
            // Take the image under the lock, but draw it after releasing the lock.
            {
                std::lock_guard lock(result_mutex);
                result.swap(latest_result);
            }
            if (result) cv::imshow("Low-Latency Object Detection", *result);
            const int key = cv::waitKey(1);
            if (key == 'q' || key == 27) stop.store(true, std::memory_order_release);
        }
    }

    int print_summary() {
        // Workers have stopped, so the main thread can safely drain leftover frames.
        const auto shutdown_dropped = queue.consume_snapshot(
            [](llvp::VideoFrame&) noexcept {});
        const auto captured = counters.captured.load(), admitted = counters.admitted.load();
        const auto overflow = counters.overflow_dropped.load();
        const auto processed = counters.processed.load(), stale = counters.stale_dropped.load();
        // Every captured frame must have exactly one final outcome.
        if (captured != admitted + overflow ||
            admitted != processed + stale + shutdown_dropped)
            throw std::runtime_error("final frame accounting invariant failed");
        const double elapsed =
            std::chrono::duration<double>(llvp::Clock::now() - started).count();
        std::cout << "mode=" << (config.latest ? "latest" : "fifo")
                  << " capacity=" << capacity << " captured=" << captured
                  << " admitted=" << admitted << " processed=" << processed
                  << " overflow_dropped=" << overflow << " stale_dropped=" << stale
                  << " shutdown_dropped=" << shutdown_dropped
                  << " result_replaced=" << result_replaced.load()
                  << " processing_fps=" << static_cast<double>(processed) / elapsed
                  << " p50_detection_age_ms=" << llvp::percentile_ms(ages, 0.50)
                  << " p95_detection_age_ms=" << llvp::percentile_ms(ages, 0.95)
                  << " p99_detection_age_ms=" << llvp::percentile_ms(ages, 0.99) << '\n';
        return EXIT_SUCCESS;
    }
};
}  // namespace

int run_video_pipeline(const Config& config) {
    return VideoPipeline(config).run();
}
}  // namespace llvp
