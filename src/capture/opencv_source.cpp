#include "llvp/capture/opencv_source.hpp"

#include <stdexcept>
#include <utility>

namespace llvp {
OpenCvSource::OpenCvSource(const std::string& path)
    : capture_(path), file_source_(true) {
    if (!capture_.isOpened()) throw std::runtime_error("unable to open video: " + path);
}

OpenCvSource::OpenCvSource(int camera_index) : capture_(camera_index) {
    if (!capture_.isOpened())
        throw std::runtime_error("unable to open camera index " +
                                 std::to_string(camera_index));
}

bool OpenCvSource::is_open() const noexcept { return capture_.isOpened(); }

ReadStatus OpenCvSource::read(VideoFrame& destination) {
    // A file can end normally; a camera that stops delivering frames is an error.
    cv::Mat backend_frame;
    if (!capture_.read(backend_frame) || backend_frame.empty())
        return file_source_ ? ReadStatus::end_of_stream : ReadStatus::error;

    // Own the pixels before sharing them: the backend may reuse its capture buffer.
    // This timestamp excludes any time already spent waiting inside the camera.
    destination = VideoFrame{next_id_++, Clock::now(), backend_frame.clone()};
    return ReadStatus::frame;
}

std::string OpenCvSource::backend_name() const {
    return capture_.getBackendName();
}

double OpenCvSource::frames_per_second() const noexcept {
    return capture_.get(cv::CAP_PROP_FPS);
}
}  // namespace llvp
