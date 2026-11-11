#include "llvp/capture/opencv_source.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    int failures = 0;
    try {
        llvp::OpenCvSource invalid("definitely-not-a-video.llvp");
        ++failures;
    } catch (const std::runtime_error&) {
    }

    const auto path = std::filesystem::temp_directory_path() / "llvp-source.avi";
    cv::VideoWriter writer(path.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                           30.0, cv::Size(32, 24));
    if (!writer.isOpened()) {
        std::cerr << "test video writer unavailable\n";
        return EXIT_FAILURE;
    }
    cv::Mat original(24, 32, CV_8UC3, cv::Scalar(10, 20, 30));
    writer.write(original);
    original.setTo(cv::Scalar(200, 200, 200));
    writer.release();

    try {
        llvp::OpenCvSource source(path.string());
        llvp::VideoFrame frame;
        if (source.read(frame) != llvp::ReadStatus::frame) ++failures;
        if (frame.id != 0 || frame.image.empty()) ++failures;
        const cv::Vec3b pixel = frame.image.at<cv::Vec3b>(0, 0);
        if (pixel[0] > 50 || pixel[1] > 50 || pixel[2] > 50) ++failures;
        if (source.read(frame) != llvp::ReadStatus::end_of_stream) ++failures;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ++failures;
    }
    std::filesystem::remove(path);
    if (failures != 0) {
        std::cerr << failures << " OpenCV source checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All OpenCV source tests passed\n";
    return EXIT_SUCCESS;
}
