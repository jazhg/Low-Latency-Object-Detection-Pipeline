#pragma once

#include "llvp/core/frame.hpp"

#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

#include <cstdint>
#include <string>

namespace llvp {
struct VideoFrame {
    std::uint64_t id{};
    Clock::time_point captured_at{};
    cv::Mat image;
};

enum class ReadStatus { frame, end_of_stream, error };

class OpenCvSource {
public:
    explicit OpenCvSource(const std::string& path);
    explicit OpenCvSource(int camera_index);

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] ReadStatus read(VideoFrame& destination);
    [[nodiscard]] std::string backend_name() const;
    [[nodiscard]] double frames_per_second() const noexcept;

private:
    cv::VideoCapture capture_;
    std::uint64_t next_id_{0};
    bool file_source_{false};
};
}  // namespace llvp
